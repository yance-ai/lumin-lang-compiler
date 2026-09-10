#!/usr/bin/env python3
"""
Lumyr 代码格式化工具
用法: python lumyr_format.py <input.lm> [output.lm] [--indent N] [--check]
"""
import re
import sys
import argparse
from pathlib import Path

class LumyrFormatter:
    def __init__(self, indent_size=4):
        self.indent_size = indent_size
        self.keywords = {
            'func', 'if', 'else', 'for', 'while', 'switch', 'match',
            'case', 'default', 'break', 'continue', 'return', 'throw',
            'try', 'catch', 'finally', 'import', 'export', 'from',
            'class', 'struct', 'enum', 'type', 'interface', 'trait',
            'impl', 'macro', 'const', 'let', 'var', 'static', 'public',
            'private', 'protected', 'new', 'delete', 'this', 'super',
            'true', 'false', 'none', 'null', 'nil', 'and', 'or', 'not',
            'in', 'is', 'as', 'do', 'then', 'end', 'begin', 'until',
            'loop', 'unless', 'elif', 'elsif', 'when', 'where', 'with',
            'async', 'await', 'yield', 'defer', 'go', 'chan', 'select',
            'package', 'module', 'use', 'namespace', 'template', 'typename'
        }

    def format(self, code):
        # 预处理：统一换行符
        code = code.replace('\r\n', '\n').replace('\r', '\n')

        # 分词
        tokens = self.tokenize(code)

        # 格式化
        result = self.format_tokens(tokens)

        return result

    def tokenize(self, code):
        tokens = []
        i = 0
        n = len(code)
        while i < n:
            # 跳过空白（但记录换行）
            if code[i] in ' \t':
                i += 1
                continue
            if code[i] == '\n':
                tokens.append(('NEWLINE', '\n'))
                i += 1
                continue

            # 行注释
            if code[i:i+2] == '//':
                j = code.find('\n', i)
                if j == -1: j = n
                tokens.append(('COMMENT', code[i:j]))
                i = j
                continue

            # 块注释
            if code[i:i+2] == '/*':
                j = code.find('*/', i+2)
                if j == -1: j = n
                else: j += 2
                tokens.append(('COMMENT', code[i:j]))
                i = j
                continue

            # 字符串
            if code[i] == '"':
                j = i + 1
                while j < n:
                    if code[j] == '\\':
                        j += 2
                        continue
                    if code[j] == '"':
                        j += 1
                        break
                    j += 1
                tokens.append(('STRING', code[i:j]))
                i = j
                continue

            # f-string
            if code[i] == 'f' and i+1 < n and code[i+1] == '"':
                j = i + 2
                while j < n:
                    if code[j] == '\\':
                        j += 2
                        continue
                    if code[j] == '"':
                        j += 1
                        break
                    j += 1
                tokens.append(('STRING', code[i:j]))
                i = j
                continue

            # 字符
            if code[i] == "'":
                j = i + 1
                if j < n and code[j] == '\\':
                    j += 2
                else:
                    j += 1
                if j < n and code[j] == "'":
                    j += 1
                tokens.append(('STRING', code[i:j]))
                i = j
                continue

            # 数字
            if code[i].isdigit() or (code[i] == '.' and i+1 < n and code[i+1].isdigit()):
                j = i
                while j < n and (code[j].isdigit() or code[j] in '.eE+-_xXabcdefABCDEF'):
                    j += 1
                tokens.append(('NUMBER', code[i:j]))
                i = j
                continue

            # 标识符/关键字
            if code[i].isalpha() or code[i] == '_':
                j = i
                while j < n and (code[j].isalnum() or code[j] == '_'):
                    j += 1
                word = code[i:j]
                if word in self.keywords:
                    tokens.append(('KEYWORD', word))
                else:
                    tokens.append(('IDENT', word))
                i = j
                continue

            # 多字符运算符
            two_char = code[i:i+2]
            three_char = code[i:i+3]
            if three_char in ('===', '!==', '...', '<<=', '>>=', '>>>='):
                tokens.append(('OP', three_char))
                i += 3
                continue
            if two_char in ('==', '!=', '<=', '>=', '&&', '||', '++', '--',
                           '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=',
                           '<<', '>>', '->', '=>', '::', '..', '?:', '??'):
                tokens.append(('OP', two_char))
                i += 2
                continue

            # 单字符运算符/标点
            if code[i] in '+-*/%=<>!&|^~?:;,.()[]{}@#$\\':
                tokens.append(('PUNCT', code[i]))
                i += 1
                continue

            # 其他字符
            tokens.append(('OTHER', code[i]))
            i += 1

        return tokens

    def format_tokens(self, tokens):
        result = []
        indent = 0
        i = 0
        n = len(tokens)
        need_newline = False
        blank_line_pending = False

        while i < n:
            ttype, tval = tokens[i]

            # 处理换行
            if ttype == 'NEWLINE':
                # 统计连续换行数
                newline_count = 1
                while i+1 < n and tokens[i+1][0] == 'NEWLINE':
                    newline_count += 1
                    i += 1
                # 保留最多 2 个连续换行（即最多 1 个空行）
                if newline_count >= 2:
                    blank_line_pending = True
                else:
                    need_newline = True
                i += 1
                continue

            # 处理注释
            if ttype == 'COMMENT':
                if need_newline or blank_line_pending:
                    result.append('\n')
                    if blank_line_pending:
                        result.append('\n')
                    need_newline = False
                    blank_line_pending = False
                result.append(' ' * (indent * self.indent_size))
                result.append(tval)
                need_newline = True
                i += 1
                continue

            # 处理 }
            if ttype == 'PUNCT' and tval == '}':
                indent = max(0, indent - 1)
                if need_newline or blank_line_pending:
                    result.append('\n')
                    if blank_line_pending:
                        result.append('\n')
                    need_newline = False
                    blank_line_pending = False
                else:
                    # 如果前面不是换行，可能是 } else { 这种情况
                    if result and result[-1] not in ('\n', ' '):
                        result.append(' ')
                result.append(' ' * (indent * self.indent_size))
                result.append('}')
                need_newline = True
                i += 1
                continue

            # 处理 {
            if ttype == 'PUNCT' and tval == '{':
                if result and result[-1] not in ('\n', ' '):
                    result.append(' ')
                result.append('{')
                indent += 1
                need_newline = True
                i += 1
                continue

            # 处理分号
            if ttype == 'PUNCT' and tval == ';':
                result.append(';')
                need_newline = True
                i += 1
                continue

            # 处理逗号
            if ttype == 'PUNCT' and tval == ',':
                result.append(',')
                # 逗号后加空格（除非是换行）
                if i+1 < n and tokens[i+1][0] != 'NEWLINE':
                    result.append(' ')
                i += 1
                continue

            # 处理冒号（case 标签等）
            if ttype == 'PUNCT' and tval == ':':
                result.append(':')
                if i+1 < n and tokens[i+1][0] != 'NEWLINE':
                    result.append(' ')
                i += 1
                continue

            # 处理其他标点
            if ttype == 'PUNCT':
                if tval in '([{':
                    result.append(tval)
                elif tval in ')]}':
                    # 去掉前面多余的空格
                    if result and result[-1] == ' ':
                        result.pop()
                    result.append(tval)
                else:
                    result.append(tval)
                i += 1
                continue

            # 处理运算符
            if ttype == 'OP':
                # 运算符前后加空格
                if result and result[-1] not in ('\n', ' ', '('):
                    result.append(' ')
                result.append(tval)
                if i+1 < n and tokens[i+1][0] not in ('NEWLINE', 'PUNCT', ')'):
                    result.append(' ')
                i += 1
                continue

            # 处理普通 token
            if need_newline or blank_line_pending:
                result.append('\n')
                if blank_line_pending:
                    result.append('\n')
                need_newline = False
                blank_line_pending = False
                result.append(' ' * (indent * self.indent_size))
            elif result and result[-1] not in ('\n', ' ', '(', '['):
                # 非标点后加空格
                if ttype in ('KEYWORD', 'IDENT', 'NUMBER', 'STRING'):
                    prev = tokens[i-1] if i > 0 else None
                    if prev and prev[0] not in ('PUNCT', 'OP'):
                        result.append(' ')

            result.append(tval)
            i += 1

        # 末尾加换行
        if result and result[-1] != '\n':
            result.append('\n')

        return ''.join(result)

def main():
    parser = argparse.ArgumentParser(description="Lumyr 代码格式化工具")
    parser.add_argument("input", help="输入文件")
    parser.add_argument("output", nargs="?", help="输出文件（默认覆盖输入文件）")
    parser.add_argument("--indent", type=int, default=4, help="缩进空格数（默认 4）")
    parser.add_argument("--check", action="store_true", help="只检查不修改")
    args = parser.parse_args()

    input_path = Path(args.input)
    if not input_path.exists():
        print(f"Error: file not found: {args.input}")
        sys.exit(1)

    code = input_path.read_text(encoding='utf-8')
    formatter = LumyrFormatter(indent_size=args.indent)
    formatted = formatter.format(code)

    if args.check:
        if code == formatted:
            print("OK: file is already formatted")
            sys.exit(0)
        else:
            print("DIFF: file needs formatting")
            sys.exit(1)

    output_path = Path(args.output) if args.output else input_path
    output_path.write_text(formatted, encoding='utf-8')
    print(f"Formatted: {input_path} -> {output_path}")

if __name__ == "__main__":
    main()
