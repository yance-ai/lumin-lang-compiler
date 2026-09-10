# -*- coding: utf-8 -*-
"""
Lumyr 语言分析引擎。

本模块是 Lumyr LSP 的核心：
  - LumyrLexer  : 将 src/lex/lex.l 中的词法规则移植为纯 Python 实现。
  - Symbol      : 源文件中定义的符号（函数/类型/枚举/常量/变量/宏/参数）。
  - AnalysisResult: 一次分析的产物（符号 + 诊断 + token 流），提供 hover/
                    跳转定义/补全所需的查询方法。
  - LumyrAnalyzer: 编排 分词 -> 符号提取 -> 诊断 全流程；可选地调用本地
                   lumyr 编译器做语义/语法级检查（带超时，失败静默降级）。

注意：本文件不依赖 src/ 下的编译器核心，仅读取其词法规则作为参考。
所有行号/列号均为 LSP 约定的 0-based。
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
from dataclasses import dataclass, field

# 由同包内 protocol 模块提供（另一个子代理实现）。若导入失败，仍允许本模块
# 独立运行（例如单元测试），此时使用轻量占位类型。
try:  # pragma: no cover - 仅在 protocol 尚未就绪时兜底
    from .protocol import (
        Position, Range,
        Diagnostic,
        DiagnosticSeverity,
        CompletionItem,
        CompletionItemKind,
    )
except Exception:  # pragma: no cover
    class _Sev:  # type: ignore
        Error = 1
        Warning = 2
        Information = 3
        Hint = 4

    class _Pos:  # type: ignore
        def __init__(self, line=0, character=0):
            self.line = line
            self.character = character

    class _Range:  # type: ignore
        def __init__(self, start=None, end=None):
            self.start = start
            self.end = end

    class _Diag:  # type: ignore
        def __init__(self, range=None, severity=None, code=None,
                     source=None, message=None):
            self.range = range
            self.severity = severity
            self.code = code
            self.source = source
            self.message = message

    class _Kind:  # type: ignore
        Function = 3
        Variable = 6
        Class = 7
        Module = 9
        Property = 10
        Enum = 13
        Keyword = 14
        Constant = 21
        Struct = 22
        Method = 2
        Field = 5

    Position = _Pos  # type: ignore
    Range = _Range  # type: ignore
    Diagnostic = _Diag  # type: ignore
    DiagnosticSeverity = _Sev  # type: ignore
    CompletionItem = None  # type: ignore
    CompletionItemKind = _Kind  # type: ignore


# ---------------------------------------------------------------------------
# 关键字表（与 src/lex/lex.l 中的关键字规则一一对应）
# ---------------------------------------------------------------------------

# 控制流 / 声明关键字
_KEYWORD_DECL = (
    "type", "enum", "func", "const", "macro",
    "print", "true", "false", "null",
    "if", "elseif", "else", "while", "do", "for", "in",
    "switch", "match", "case", "default",
    "try", "throw", "catch", "finally",
    "break", "continue", "return",
    "read", "write",
)

# 类型关键字
_KEYWORD_TYPES = (
    "int", "double", "char", "string", "bool", "ascii", "byte",
    "int8", "int16", "int32", "int64",
    "uint8", "uint16", "uint32", "uint64",
    "uint", "long", "float",
)

KEYWORDS = frozenset(_KEYWORD_DECL + _KEYWORD_TYPES)
TYPE_KEYWORDS = frozenset(_KEYWORD_TYPES)

# 用于补全展示的关键字（去掉类型关键字以减少噪音？保留全部，便于补全）
_COMPLETION_KEYWORDS = sorted(KEYWORDS)

# 内置函数 / 标准库函数名（用于补全）
BUILTIN_FUNCTIONS = [
    "print", "len", "type", "str", "int", "double", "bool",
    "read", "write", "range",
    "append", "push", "pop", "shift", "unshift", "sort", "reverse",
    "map", "filter", "reduce", "keys", "values", "has", "delete",
    "clone", "copy", "json_parse", "json_stringify",
    "regex_match", "regex_replace", "split", "join", "trim",
    "upper", "lower", "starts_with", "ends_with", "contains",
    "find", "replace", "substring", "char_at",
    "to_int", "to_double", "to_string",
    "floor", "ceil", "round", "abs", "sqrt", "pow", "min", "max",
    "random", "sleep", "now", "time", "date", "format",
    "open", "read_file", "write_file", "file_exists", "mkdir", "list_dir",
    "gc_collect", "gc_stats",
    "thread_spawn", "thread_join", "mutex_create", "mutex_lock", "mutex_unlock",
    "http_get", "http_post",
]


# ---------------------------------------------------------------------------
# Token
# ---------------------------------------------------------------------------

@dataclass
class Token:
    """词法单元。

    type 取值: ID / KEYWORD / INTEGER / NUMBER / STRING / FSTRING / CHAR /
               COMMENT / OP / ERROR
    """
    type: str
    value: str
    line: int
    column: int
    end_line: int
    end_column: int


# ---------------------------------------------------------------------------
# 词法分析器
# ---------------------------------------------------------------------------

# 多字符运算符（按长度优先匹配）。键为运算符文本，值为长度。
_MULTI_CHAR_OPS = {
    "...": 3,
    "++": 2, "--": 2, "?.": 2, "??": 2,
    ">=": 2, "<=": 2, "==": 2, "!=": 2,
    "&&": 2, "||": 2,
    "+=": 2, "-=": 2, "*=": 2, "/=": 2,
}

# 单字符运算符 / 标点
_SINGLE_CHAR_OPS = set(
    "?.!=%+-*/:;(){}[],@><"
)
# 注意：'.' 单独处理（数字小数点 vs DOT），'?' 单独处理（三元 vs ?.）


class LumyrLexer:
    """手写 Lumyr 词法分析器，逐字符扫描。

    与 lex.l 的对应关系：
      - 关键字      -> KEYWORD
      - 标识符      -> ID
      - 整数        -> INTEGER
      - 浮点数      -> NUMBER
      - 字符串      -> STRING（f-string 记为 FSTRING）
      - 字符        -> CHAR
      - 注释        -> COMMENT（lex.l 丢弃，LSP 需要用于文档提取，故保留）
      - 运算符/标点 -> OP
      - 无法识别    -> ERROR（同时供分析器生成诊断）
    """

    def tokenize(self, text: str) -> list[Token]:
        """将源码分词为 Token 列表。不抛异常：非法字符产出 ERROR token。"""
        tokens: list[Token] = []
        n = len(text)
        i = 0
        line = 0
        col = 0

        def peek(k: int = 0) -> str:
            return text[i + k] if i + k < n else ""

        def consume(k: int = 1) -> None:
            """前进 k 个字符，同步更新 line/col。"""
            nonlocal i, line, col
            for _ in range(k):
                if i >= n:
                    return
                ch = text[i]
                i += 1
                if ch == "\n":
                    line += 1
                    col = 0
                else:
                    col += 1

        while i < n:
            ch = peek()
            start_line, start_col = line, col

            # --- 空白：跳过（不产出 token） ---
            if ch in " \t\r\n":
                consume()
                continue

            # --- 行注释 //... ---
            if ch == "/" and peek(1) == "/":
                consume(2)
                while i < n and peek() != "\n":
                    consume()
                end_line, end_col = line, col
                raw = text[self._offset_of(text, start_line, start_col):
                           self._offset_of(text, end_line, end_col)]
                tokens.append(Token("COMMENT", raw, start_line, start_col,
                                    end_line, end_col))
                continue

            # --- 块注释 /* ... */ ---
            if ch == "/" and peek(1) == "*":
                consume(2)
                closed = False
                while i < n:
                    if peek() == "*" and peek(1) == "/":
                        consume(2)
                        closed = True
                        break
                    consume()
                end_line, end_col = line, col
                raw = text[self._offset_of(text, start_line, start_col):
                           self._offset_of(text, end_line, end_col)]
                if not closed:
                    tokens.append(Token("ERROR", raw, start_line, start_col,
                                        end_line, end_col))
                else:
                    tokens.append(Token("COMMENT", raw, start_line, start_col,
                                        end_line, end_col))
                continue

            # --- f-string f"..." ---
            if ch == "f" and peek(1) == '"':
                consume(2)  # 吃掉 f 和 "
                closed = False
                while i < n:
                    c = peek()
                    if c == "\\":
                        consume(2)  # 转义序列整体吞掉（不影响行号）
                        continue
                    if c == '"':
                        consume()
                        closed = True
                        break
                    consume()
                end_line, end_col = line, col
                raw = text[self._offset_of(text, start_line, start_col):
                           self._offset_of(text, end_line, end_col)]
                if not closed:
                    tokens.append(Token("ERROR", raw, start_line, start_col,
                                         end_line, end_col))
                else:
                    tokens.append(Token("FSTRING", raw, start_line, start_col,
                                         end_line, end_col))
                continue

            # --- 字符串 "..." ---
            if ch == '"':
                consume()
                closed = False
                while i < n:
                    c = peek()
                    if c == "\\":
                        consume(2)
                        continue
                    if c == '"':
                        consume()
                        closed = True
                        break
                    consume()
                end_line, end_col = line, col
                raw = text[self._offset_of(text, start_line, start_col):
                           self._offset_of(text, end_line, end_col)]
                if not closed:
                    tokens.append(Token("ERROR", raw, start_line, start_col,
                                         end_line, end_col))
                else:
                    tokens.append(Token("STRING", raw, start_line, start_col,
                                        end_line, end_col))
                continue

            # --- 字符字面量 'x' ---
            if ch == "'":
                consume()
                # lex.l: '[^'\n]' —— 恰好一个非换行非引号字符
                if i < n and peek() != "\n" and peek() != "'":
                    consume()
                if i < n and peek() == "'":
                    consume()
                    end_line, end_col = line, col
                    raw = text[self._offset_of(text, start_line, start_col):
                               self._offset_of(text, end_line, end_col)]
                    tokens.append(Token("CHAR", raw, start_line, start_col,
                                        end_line, end_col))
                else:
                    end_line, end_col = line, col
                    raw = text[self._offset_of(text, start_line, start_col):
                               self._offset_of(text, end_line, end_col)]
                    tokens.append(Token("ERROR", raw, start_line, start_col,
                                         end_line, end_col))
                continue

            # --- 数字：先判断浮点数，再整数 ---
            if ch.isdigit():
                consume()
                is_float = False
                if peek() == "." and peek(1).isdigit():
                    is_float = True
                    consume()  # 吃掉 '.'
                    while i < n and peek().isdigit():
                        consume()
                while i < n and peek().isdigit():
                    consume()
                end_line, end_col = line, col
                raw = text[self._offset_of(text, start_line, start_col):
                           self._offset_of(text, end_line, end_col)]
                tokens.append(Token("NUMBER" if is_float else "INTEGER", raw,
                                    start_line, start_col, end_line, end_col))
                continue

            # --- 标识符 / 关键字 ---
            if ch == "_" or ch.isalpha():
                consume()
                while i < n and (peek() == "_" or peek().isalnum()):
                    consume()
                end_line, end_col = line, col
                raw = text[self._offset_of(text, start_line, start_col):
                           self._offset_of(text, end_line, end_col)]
                if raw in KEYWORDS:
                    tokens.append(Token("KEYWORD", raw, start_line, start_col,
                                        end_line, end_col))
                else:
                    tokens.append(Token("ID", raw, start_line, start_col,
                                        end_line, end_col))
                continue

            # --- 多字符运算符（最长匹配） ---
            matched_op = False
            # 先试 3 字符
            three = text[i:i + 3]
            if three in _MULTI_CHAR_OPS:
                consume(3)
                end_line, end_col = line, col
                tokens.append(Token("OP", three, start_line, start_col,
                                    end_line, end_col))
                continue
            two = text[i:i + 2]
            if two in _MULTI_CHAR_OPS:
                consume(2)
                end_line, end_col = line, col
                tokens.append(Token("OP", two, start_line, start_col,
                                    end_line, end_col))
                continue

            # --- 单字符运算符 / 标点 ---
            if ch in _SINGLE_CHAR_OPS or ch == ".":
                consume()
                end_line, end_col = line, col
                tokens.append(Token("OP", ch, start_line, start_col,
                                    end_line, end_col))
                continue

            # --- 无法识别：ERROR ---
            consume()
            end_line, end_col = line, col
            tokens.append(Token("ERROR", ch, start_line, start_col,
                                end_line, end_col))

        return tokens

    # --- 坐标 <-> 偏移 辅助 ---

    @staticmethod
    def _line_offsets(text: str) -> list[int]:
        """返回每一行起始字符偏移的列表。"""
        offs = [0]
        for idx, c in enumerate(text):
            if c == "\n":
                offs.append(idx + 1)
        return offs

    def _offset_of(self, text: str, line: int, col: int) -> int:
        offs = self._line_offsets(text)
        if line < len(offs):
            return offs[line] + col
        return len(text)


# ---------------------------------------------------------------------------
# Symbol / AnalysisResult
# ---------------------------------------------------------------------------

@dataclass
class Symbol:
    """源文件中定义的一个符号。"""
    name: str
    kind: str           # function/type/variable/constant/macro/parameter/enum
    line: int
    column: int
    end_line: int
    end_column: int
    detail: str = ""
    doc: str = ""
    scope_level: int = 0  # 0 = 全局


@dataclass
class AnalysisResult:
    """一次文档分析的完整产物。"""
    symbols: list[Symbol] = field(default_factory=list)
    diagnostics: list = field(default_factory=list)
    tokens: list[Token] = field(default_factory=list)

    # -- 查询方法 ----------------------------------------------------------

    def find_symbol_at(self, line: int, column: int) -> Symbol | None:
        """找到覆盖 (line, column) 的符号（用于 hover 命中）。

        若多个符号同时包含该位置（嵌套），返回最后加入者（通常更内层）。
        """
        best: Symbol | None = None
        for s in self.symbols:
            if s.line > line or (s.line == line and s.column > column):
                continue
            if s.end_line < line or (s.end_line == line and s.end_column < column):
                continue
            best = s  # 内层后加入，覆盖即可
        return best

    def find_definition(self, name: str, line: int) -> Symbol | None:
        """按名称查找定义。

        简化作用域规则：
          1. 优先取在 line 之前定义的局部符号（scope_level >= 1），
             取其中行号最大者（最近的局部定义）；
          2. 否则取在 line 之前定义的全局符号；
          3. 都没有则退回任意同名符号。
        """
        candidates = [s for s in self.symbols if s.name == name]
        if not candidates:
            return None

        locals_before = [
            s for s in candidates
            if s.scope_level >= 1 and s.line <= line
        ]
        if locals_before:
            return max(locals_before, key=lambda s: s.line)

        globals_before = [
            s for s in candidates
            if s.scope_level == 0 and s.line <= line
        ]
        if globals_before:
            return globals_before[0]

        return candidates[0]

    def get_completions(self, line: int, column: int, prefix: str):
        """返回以 prefix 开头的补全项：关键字 + 内置函数 + 文件内符号。"""
        items = []
        prefix = prefix or ""
        pl = prefix.lower()

        # 1) 关键字
        for kw in _COMPLETION_KEYWORDS:
            if not pl or kw.startswith(prefix):
                items.append(CompletionItem(
                    label=kw,
                    kind=CompletionItemKind.Keyword,
                    detail="keyword",
                    documentation=None,
                    insertText=kw,
                ))

        # 2) 内置函数
        for fn in BUILTIN_FUNCTIONS:
            if not pl or fn.startswith(prefix):
                items.append(CompletionItem(
                    label=fn,
                    kind=CompletionItemKind.Function,
                    detail="built-in",
                    documentation=None,
                    insertText=fn,
                ))

        # 3) 文件内符号（去重：同名取最近定义）
        seen: dict[str, Symbol] = {}
        for s in self.symbols:
            if not pl or s.name.startswith(prefix):
                # 同名保留靠后的（更内层/更近）
                seen[s.name] = s
        for name, s in seen.items():
            items.append(CompletionItem(
                label=s.name,
                kind=self._kind_for(s.kind),
                detail=s.detail or s.kind,
                documentation=s.doc or None,
                insertText=s.name,
            ))
        return items

    @staticmethod
    def _kind_for(kind: str):
        mapping = {
            "function": CompletionItemKind.Function,
            "type": CompletionItemKind.Struct,
            "variable": CompletionItemKind.Variable,
            "constant": CompletionItemKind.Constant,
            "macro": CompletionItemKind.Module,
            "parameter": CompletionItemKind.Variable,
            "enum": CompletionItemKind.Enum,
        }
        return mapping.get(kind, CompletionItemKind.Variable)


# ---------------------------------------------------------------------------
# 分析器
# ---------------------------------------------------------------------------

# 诊断行号解析：兼容 "语法错误(第N行): ..." / "line N: ..." / "file:line: ..."
_COMPILER_LINE_RE = re.compile(r"第\s*(\d+)\s*行")
_COMPILER_LINE_RE2 = re.compile(r"line\s+(\d+)", re.IGNORECASE)
_COMPILER_LINE_RE3 = re.compile(r":(\d+)(?::\d+)?:")


class LumyrAnalyzer:
    """编排：分词 -> 符号提取 -> 诊断（含可选编译器检查）。"""

    #: 候选编译器可执行文件名（相对项目根）。
    #: 优先选择稳定的 bin/lumyr.exe 与 lumyr.exe；_fresh*.exe 为实验性
    #: 调试二进制，在某些输入下会直接段错误，故放在最后。
    _COMPILER_CANDIDATES = (
        os.path.join("bin", "lumyr.exe"),
        os.path.join("bin", "lumyr"),
        "lumyr.exe", "lumyr",
        "_fresh.exe", "_fresh_dbg.exe",
    )

    def __init__(self) -> None:
        self._compiler_path: str | None = self._find_compiler()
        self._lexer = LumyrLexer()

    # -- 编译器定位 --------------------------------------------------------

    def _find_compiler(self) -> str | None:
        """在项目根目录与 PATH 中查找编译器可执行文件。"""
        # 从本文件位置向上推导：tools/lsp/lumyr_lsp/analysis.py
        # 逐级上溯，每一级都尝试候选名。
        here = os.path.dirname(os.path.abspath(__file__))
        seen_bases: list[str] = []
        cur = here
        for _ in range(6):
            seen_bases.append(cur)
            parent = os.path.dirname(cur)
            if parent == cur:
                break
            cur = parent
        seen_bases.append(os.getcwd())

        for base in seen_bases:
            for name in self._COMPILER_CANDIDATES:
                p = os.path.join(base, name)
                if os.path.isfile(p):
                    return p

        # PATH 中查找
        for exe in ("lumyr.exe", "lumyr"):
            found = shutil.which(exe)
            if found:
                return found
        return None

    # -- 主入口 ------------------------------------------------------------

    def analyze(self, uri: str, text: str) -> AnalysisResult:
        """分析给定文本，返回 AnalysisResult。绝不抛异常。"""
        try:
            tokens = self._lexer.tokenize(text)
        except Exception:
            tokens = []

        diagnostics: list = []

        # 1) 词法错误
        for tok in tokens:
            if tok.type == "ERROR":
                msg = f"意外字符: {tok.value!r}" if tok.value.strip() else "未闭合的字符串或注释"
                diagnostics.append(self._diag(tok, msg))

        # 2) 符号提取
        try:
            symbols = self._extract_symbols(tokens)
        except Exception:
            symbols = []

        # 3) 可选：调用编译器做语法/语义检查
        if self._compiler_path and text.strip():
            try:
                diagnostics.extend(self._run_compiler_check(text))
            except Exception:
                pass  # 静默降级

        return AnalysisResult(
            symbols=symbols,
            diagnostics=diagnostics,
            tokens=tokens,
        )

    # -- 诊断构造 ----------------------------------------------------------

    @staticmethod
    def _diag(tok: Token, message: str):
        return Diagnostic(
            range=Range(
                start=Position(line=tok.line, character=tok.column),
                end=Position(line=tok.end_line, character=tok.end_column),
            ),
            severity=DiagnosticSeverity.Error,
            code=None,
            source="lumyr-lsp",
            message=message,
        )

    # -- 编译器调用 --------------------------------------------------------

    def _run_compiler_check(self, text: str) -> list:
        """把 text 写入临时文件并调用编译器，解析错误输出。失败返回 []。"""
        tmp = None
        try:
            with tempfile.NamedTemporaryFile(
                mode="w", suffix=".lm", delete=False, encoding="utf-8"
            ) as f:
                f.write(text)
                tmp = f.name

            proc = subprocess.run(
                [self._compiler_path, tmp],
                capture_output=True,
                text=True,
                timeout=5,
                encoding="utf-8",
                errors="replace",
            )
            if proc.returncode == 0:
                return []
            out = (proc.stderr or "") + "\n" + (proc.stdout or "")
            return self._parse_compiler_output(out)
        except Exception:
            return []
        finally:
            if tmp:
                try:
                    os.unlink(tmp)
                except OSError:
                    pass

    def _parse_compiler_output(self, output: str) -> list:
        """从编译器输出中提取 (0-based line, message) 并构造 Diagnostic。"""
        results = []
        for line in output.splitlines():
            line = line.strip()
            if not line:
                continue
            m = _COMPILER_LINE_RE.search(line)
            if not m:
                m = _COMPILER_LINE_RE2.search(line)
            if not m:
                m = _COMPILER_LINE_RE3.search(line)
            if not m:
                continue
            lineno = int(m.group(1)) - 1  # 1-based -> 0-based
            # 去掉行号前缀，保留消息正文。
            # 形如 "语法错误(第1行): msg" / "line 1: msg" / "file:1: msg"
            msg = re.sub(
                r"^.*?(第\s*\d+\s*行\s*\)?\s*|line\s+\d+\s*|:\d+(?::\d+)?\s*):\s*",
                "", line, flags=re.IGNORECASE,
            )
            if not msg:
                msg = line
            pos = Position(line=lineno, character=0)
            results.append(Diagnostic(
                range=Range(start=pos, end=pos),
                severity=DiagnosticSeverity.Error,
                code=None,
                source="lumyr-lsp",
                message=msg,
            ))
        return results

    # -- 符号提取 ----------------------------------------------------------

    def _extract_symbols(self, tokens: list[Token]) -> list[Symbol]:
        """基于 token 流的模式匹配提取符号。不需要完整 parser。"""
        symbols: list[Symbol] = []
        # 过滤掉注释 token（注释不参与语法模式）
        toks = [t for t in tokens if t.type != "COMMENT"]
        n = len(toks)
        depth = 0  # 当前花括号深度

        def kw(i: int, name: str) -> bool:
            return (i < n and toks[i].type == "KEYWORD"
                    and toks[i].value == name)

        def op(i: int, value: str) -> bool:
            return i < n and toks[i].type == "OP" and toks[i].value == value

        def idname(i: int) -> str | None:
            return toks[i].value if (i < n and toks[i].type == "ID") else None

        added: set[tuple[str, int, int]] = set()

        def add(sym: Symbol):
            key = (sym.name, sym.line, sym.column)
            if key in added:
                return
            added.add(key)
            symbols.append(sym)

        i = 0
        while i < n:
            t = toks[i]

            # 花括号深度跟踪
            if t.type == "OP" and t.value == "{":
                depth += 1
                i += 1
                continue
            if t.type == "OP" and t.value == "}":
                depth = max(0, depth - 1)
                i += 1
                continue

            # --- func ID (params) { ---
            if kw(i, "func"):
                name = idname(i + 1)
                if name is not None:
                    name_tok = toks[i + 1]
                    # 收集参数
                    params: list[tuple[str, str]] = []  # (name, type)
                    j = i + 2
                    if op(j, "("):
                        j += 1
                        while j < n and not op(j, ")"):
                            pname = idname(j)
                            if pname is not None:
                                ptype = ""
                                k = j + 1
                                if op(k, ":"):
                                    k += 1
                                    tparts = []
                                    while k < n and not op(k, ",") and not op(k, ")"):
                                        tparts.append(toks[k].value)
                                        k += 1
                                    ptype = " ".join(tparts)
                                params.append((pname, ptype))
                                # 参数本身作为 parameter 符号
                                pt = toks[j]
                                add(Symbol(
                                    name=pname, kind="parameter",
                                    line=pt.line, column=pt.column,
                                    end_line=pt.end_line, end_column=pt.end_column,
                                    detail=ptype, doc="",
                                    scope_level=max(depth, 1),
                                ))
                            j += 1
                    detail = "func {name}({params})".format(
                        name=name,
                        params=", ".join(
                            p if not ty else f"{p}: {ty}" for p, ty in params
                        ),
                    )
                    add(Symbol(
                        name=name, kind="function",
                        line=name_tok.line, column=name_tok.column,
                        end_line=name_tok.end_line, end_column=name_tok.end_column,
                        detail=detail, doc="",
                        scope_level=depth,
                    ))
                    # j 当前停在 ')'；让主循环继续处理 '{' 与函数体，
                    # 这样花括号深度会自然递增，体内部变量可正确归到局部作用域。
                    i = j + 1
                    continue

            # --- type ID { ... } ---
            if kw(i, "type"):
                name = idname(i + 1)
                if name is not None:
                    name_tok = toks[i + 1]
                    add(Symbol(
                        name=name, kind="type",
                        line=name_tok.line, column=name_tok.column,
                        end_line=name_tok.end_line, end_column=name_tok.end_column,
                        detail=f"type {name}", doc="",
                        scope_level=depth,
                    ))
                    # 扫描类型体收集字段
                    j = i + 2
                    # 跳到 '{'
                    while j < n and not op(j, "{"):
                        j += 1
                    j += 1  # 吃掉 '{'
                    inner = 1
                    while j < n and inner > 0:
                        if op(j, "{"):
                            inner += 1
                            j += 1
                        elif op(j, "}"):
                            inner -= 1
                            j += 1
                            if inner == 0:
                                break
                        elif toks[j].type == "ID" and op(j + 1, ":"):
                            fname = toks[j].value
                            ft = toks[j]
                            k = j + 2
                            tparts = []
                            # 类型表达式终止于 , ; } 或 下一字段（ID 紧跟 ':'）
                            while k < n:
                                if (op(k, ",") or op(k, ";") or op(k, "}")
                                        or (toks[k].type == "ID"
                                            and op(k + 1, ":"))):
                                    break
                                tparts.append(toks[k].value)
                                k += 1
                            add(Symbol(
                                name=fname, kind="variable",
                                line=ft.line, column=ft.column,
                                end_line=ft.end_line, end_column=ft.end_column,
                                detail=": ".join(tparts) if tparts else "field",
                                doc="", scope_level=depth + 1,
                            ))
                            j = k
                        else:
                            j += 1
                    # j 正好在匹配的 '}' 之后
                    i = j
                    continue

            # --- enum ID { ... } ---
            if kw(i, "enum"):
                name = idname(i + 1)
                if name is not None:
                    name_tok = toks[i + 1]
                    add(Symbol(
                        name=name, kind="enum",
                        line=name_tok.line, column=name_tok.column,
                        end_line=name_tok.end_line, end_column=name_tok.end_column,
                        detail=f"enum {name}", doc="",
                        scope_level=depth,
                    ))
                    j = i + 2
                    while j < n and not op(j, "{"):
                        j += 1
                    j += 1
                    inner = 1
                    while j < n and inner > 0:
                        if op(j, "{"):
                            inner += 1
                            j += 1
                        elif op(j, "}"):
                            inner -= 1
                            j += 1
                            if inner == 0:
                                break
                        elif toks[j].type == "ID":
                            mt = toks[j]
                            add(Symbol(
                                name=mt.value, kind="constant",
                                line=mt.line, column=mt.column,
                                end_line=mt.end_line, end_column=mt.end_column,
                                detail=f"{name}.{mt.value}", doc="",
                                scope_level=depth + 1,
                            ))
                            j += 1
                        else:
                            j += 1
                    i = j
                    continue

            # --- const ID [: type] = ... ---
            if kw(i, "const"):
                name = idname(i + 1)
                if name is not None:
                    name_tok = toks[i + 1]
                    detail = "constant"
                    j = i + 2
                    if op(j, ":"):
                        k = j + 1
                        tparts = []
                        while k < n and not op(k, "=") and not op(k, ";"):
                            tparts.append(toks[k].value)
                            k += 1
                        if tparts:
                            detail = ": ".join(tparts)
                        j = k
                    add(Symbol(
                        name=name, kind="constant",
                        line=name_tok.line, column=name_tok.column,
                        end_line=name_tok.end_line, end_column=name_tok.end_column,
                        detail=detail, doc="",
                        scope_level=depth,
                    ))
                    i = j
                    continue

            # --- macro ID( ... ) ---
            if kw(i, "macro"):
                name = idname(i + 1)
                if name is not None:
                    name_tok = toks[i + 1]
                    j = i + 2
                    params = []
                    if op(j, "("):
                        j += 1
                        while j < n and not op(j, ")"):
                            if toks[j].type == "ID":
                                params.append(toks[j].value)
                            j += 1
                    add(Symbol(
                        name=name, kind="macro",
                        line=name_tok.line, column=name_tok.column,
                        end_line=name_tok.end_line, end_column=name_tok.end_column,
                        detail=f"macro {name}({', '.join(params)})",
                        doc="", scope_level=depth,
                    ))
                    i = j + 1
                    continue

            # --- 变量定义: ID = ...  或  ID: type = ... ---
            if t.type == "ID":
                # 排除常见误判：ID 后面紧跟 ( 是函数调用/定义，不处理
                if op(i + 1, "="):
                    # ID = ...
                    add(Symbol(
                        name=t.value, kind="variable",
                        line=t.line, column=t.column,
                        end_line=t.end_line, end_column=t.end_column,
                        detail="variable", doc="",
                        scope_level=depth,
                    ))
                    i += 1
                    continue
                if op(i + 1, ":"):
                    # ID : type [= ...] —— 只有当出现了单独的 '=' 才视为变量定义，
                    # 以避免把 case label / 字典字面量 误判为定义。
                    k = i + 2
                    tparts = []
                    seen_assign = False
                    while k < n and not op(k, ";") and not op(k, "}"):
                        if op(k, "="):
                            seen_assign = True
                            break
                        tparts.append(toks[k].value)
                        k += 1
                    if seen_assign:
                        add(Symbol(
                            name=t.value, kind="variable",
                            line=t.line, column=t.column,
                            end_line=t.end_line, end_column=t.end_column,
                            detail=": ".join(tparts) if tparts else "variable",
                            doc="", scope_level=depth,
                        ))
                        i = k
                        continue

            i += 1

        return symbols


__all__ = [
    "Token", "Symbol", "AnalysisResult",
    "LumyrLexer", "LumyrAnalyzer",
    "KEYWORDS", "TYPE_KEYWORDS", "BUILTIN_FUNCTIONS",
]
