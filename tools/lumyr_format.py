#!/usr/bin/env python3
"""
Lumyr 代码格式化工具（v3 - 修复版）
用法: python lumyr_format.py <input.lm> [output.lm] [--indent N] [--check]
"""
import re
import sys
import argparse
from pathlib import Path

class LumyrFormatter:
    def __init__(self, indent_size=4):
        self.indent_size = indent_size
        self.space_before_paren_keywords = {'if', 'for', 'while', 'switch', 'match', 'catch', 'return', 'func'}

    def format(self, code):
        code = code.replace('\r\n', '\n').replace('\r', '\n')
        tokens = self.tokenize(code)
        result = self.format_tokens(tokens)
        return result

    def tokenize(self, code):
        tokens = []
        i = 0
        n = len(code)
        while i < n:
            if code[i] in ' \t':
                i += 1
                continue
            if code[i] == '\n':
                tokens.append(('NEWLINE', '\n'))
                i += 1
                continue
            if code[i:i+2] == '//':
                j = code.find('\n', i)
                if j == -1: j = n
                tokens.append(('COMMENT', code[i:j]))
                i = j
                continue
            if code[i:i+2] == '/*':
                j = code.find('*/', i+2)
                if j == -1: j = n
                else: j += 2
                tokens.append(('COMMENT', code[i:j]))
                i = j
                continue
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
            if code[i].isdigit() or (code[i] == '.' and i+1 < n and code[i+1].isdigit()):
                j = i
                while j < n and (code[j].isdigit() or code[j] in '.eE+-_xXabcdefABCDEF'):
                    j += 1
                tokens.append(('NUMBER', code[i:j]))
                i = j
                continue
            if code[i].isalpha() or code[i] == '_':
                j = i
                while j < n and (code[j].isalnum() or code[j] == '_'):
                    j += 1
                tokens.append(('IDENT', code[i:j]))
                i = j
                continue
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
            if code[i] in '+-*/%=<>!&|^~?:;,.()[]{}@#$\\':
                tokens.append(('PUNCT', code[i]))
                i += 1
                continue
            tokens.append(('OTHER', code[i]))
            i += 1
        return tokens

    def is_keyword(self, word):
        return word in {'func', 'if', 'else', 'for', 'while', 'do', 'switch', 'match',
                    'case', 'default', 'break', 'continue', 'return', 'throw', 'try',
                    'catch', 'finally', 'import', 'export', 'from', 'class', 'struct',
                    'enum', 'type', 'interface', 'trait', 'impl', 'macro', 'const',
                    'let', 'var', 'static', 'public', 'private', 'protected', 'new',
                    'delete', 'this', 'super', 'true', 'false', 'none', 'null', 'nil',
                    'and', 'or', 'not', 'in', 'is', 'as', 'then', 'end', 'begin', 'until',
                    'loop', 'unless', 'elif', 'elsif', 'when', 'where', 'with', 'async',
                    'await', 'yield', 'defer', 'go', 'chan', 'select', 'package', 'module',
                    'use', 'namespace', 'template', 'typename', 'int', 'double', 'string',
                    'bool', 'char', 'byte'}

    def format_tokens(self, tokens):
        result = []
        indent = 0
        paren_depth = 0  # 括号深度，用于判断 for 循环中的分号
        i = 0
        n = len(tokens)
        need_newline = False
        blank_line_pending = False
        prev_token = None

        while i < n:
            ttype, tval = tokens[i]

            if ttype == 'NEWLINE':
                newline_count = 1
                while i+1 < n and tokens[i+1][0] == 'NEWLINE':
                    newline_count += 1
                    i += 1
                if newline_count >= 2:
                    blank_line_pending = True
                else:
                    need_newline = True
                i += 1
                prev_token = ('NEWLINE', '\n')
                continue

            if ttype == 'COMMENT':
                if need_newline or blank_line_pending:
                    result.append('\n')
                    if blank_line_pending:
                        result.append('\n')
                    need_newline = False
                    blank_line_pending = False
                    result.append(' ' * (indent * self.indent_size))
                elif result and result[-1] not in ('\n', ' '):
                    result.append(' ')
                result.append(tval)
                need_newline = True
                prev_token = (ttype, tval)
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
                    result.append(' ' * (indent * self.indent_size))
                elif result and result[-1] not in ('\n', ' '):
                    if prev_token and prev_token[0] == 'IDENT' and prev_token[1] == 'else':
                        result.append(' ')
                    elif result and result[-1] != ' ':
                        result.append(' ')
                result.append('}')
                need_newline = True
                prev_token = (ttype, tval)
                i += 1
                continue

            # 处理 {
            if ttype == 'PUNCT' and tval == '{':
                if result and result[-1] not in ('\n', ' '):
                    result.append(' ')
                result.append('{')
                indent += 1
                need_newline = True
                prev_token = (ttype, tval)
                i += 1
                continue

            # 处理分号（只有在括号深度为 0 时才换行）
            if ttype == 'PUNCT' and tval == ';':
                result.append(';')
                if paren_depth == 0:
                    need_newline = True
                else:
                    # for 循环中的分号，后面加空格
                    if i+1 < n and tokens[i+1][0] != 'NEWLINE':
                        result.append(' ')
                prev_token = (ttype, tval)
                i += 1
                continue

            # 处理逗号
            if ttype == 'PUNCT' and tval == ',':
                result.append(',')
                if i+1 < n and tokens[i+1][0] != 'NEWLINE':
                    result.append(' ')
                prev_token = (ttype, tval)
                i += 1
                continue

            # 处理冒号
            if ttype == 'PUNCT' and tval == ':':
                result.append(':')
                if i+1 < n and tokens[i+1][0] != 'NEWLINE':
                    result.append(' ')
                prev_token = (ttype, tval)
                i += 1
                continue

            # 处理点号
            if ttype == 'PUNCT' and tval == '.':
                result.append('.')
                prev_token = (ttype, tval)
                i += 1
                continue

            # 处理左括号/左方括号
            if ttype == 'PUNCT' and tval in ('(', '['):
                if prev_token and prev_token[0] == 'IDENT' and self.is_keyword(prev_token[1]):
                    if prev_token[1] in self.space_before_paren_keywords:
                        if result and result[-1] not in ('\n', ' '):
                            result.append(' ')
                result.append(tval)
                paren_depth += 1
                prev_token = (ttype, tval)
                i += 1
                continue

            # 处理右括号/右方括号
            if ttype == 'PUNCT' and tval in (')', ']'):
                result.append(tval)
                paren_depth = max(0, paren_depth - 1)
                prev_token = (ttype, tval)
                i += 1
                continue

            # 处理单字符运算符（+-*/%=<>!&|^~）
            if ttype == 'PUNCT' and tval in '+-*/%=<>!&|^~':
                # 负号特殊处理：前面是左括号/逗号/分号/等号时不加空格
                is_negative = (tval == '-' and prev_token and
                              prev_token[0] == 'PUNCT' and
                              prev_token[1] in ('(', ',', ';', '=', '[', '{'))
                if not is_negative:
                    if result and result[-1] not in ('\n', ' ', '(', '[', '{', ',', ';'):
                        result.append(' ')
                result.append(tval)
                # 后面加空格（除非是自增自减或右括号）
                if i+1 < n and tokens[i+1][0] not in ('NEWLINE', 'PUNCT', ')'):
                    if tokens[i+1][1] not in ('+', '-', '++', '--'):
                        result.append(' ')
                prev_token = (ttype, tval)
                i += 1
                continue

            # 处理其他标点
            if ttype == 'PUNCT':
                result.append(tval)
                prev_token = (ttype, tval)
                i += 1
                continue

            # 处理运算符
            if ttype == 'OP':
                if result and result[-1] not in ('\n', ' ', '(', '['):
                    result.append(' ')
                result.append(tval)
                if i+1 < n and tokens[i+1][0] not in ('NEWLINE', 'PUNCT', ')'):
                    if tokens[i+1][1] not in ('++', '--'):
                        result.append(' ')
                prev_token = (ttype, tval)
                i += 1
                continue

            # 处理普通 token（IDENT/NUMBER/STRING）
            if need_newline or blank_line_pending:
                result.append('\n')
                if blank_line_pending:
                    result.append('\n')
                need_newline = False
                blank_line_pending = False
                result.append(' ' * (indent * self.indent_size))
            else:
                # 判断是否需要空格
                need_space = False
                if prev_token:
                    pt, pv = prev_token
                    # 运算符后需要空格
                    if pt == 'OP' and tval not in ('++', '--'):
                        need_space = True
                    # 单字符运算符后需要空格
                    elif pt == 'PUNCT' and pv in '+-*/%=<>!&|^~':
                        need_space = True
                    # 关键字后需要空格（除非后面是左括号）
                    elif pt == 'IDENT' and self.is_keyword(pv):
                        if tval != '(' and tval != ';' and tval != '{':
                            need_space = True
                        elif tval == '(' and pv in self.space_before_paren_keywords:
                            need_space = True
                    # 标识符/数字/字符串之间需要空格
                    elif pt in ('IDENT', 'NUMBER', 'STRING') and ttype in ('IDENT', 'NUMBER', 'STRING'):
                        need_space = True
                    # 右括号后需要空格
                    elif pv in (')', ']') and tval not in (';', ',', ')', ']', '.', '}', '{'):
                        need_space = True
                    # 当前是运算符，前面需要空格
                    if (ttype == 'OP' or (ttype == 'PUNCT' and tval in '+-*/%=<>!&|^~')):
                        if pt not in ('PUNCT', 'OP', 'NEWLINE') and pv not in ('(', '[', ',', ';'):
                            need_space = True
                        # 负号特殊处理
                        if tval == '-' and pv in ('(', ',', ';', '=', '['):
                            need_space = False

                if need_space and result and result[-1] not in ('\n', ' '):
                    result.append(' ')

            result.append(tval)
            prev_token = (ttype, tval)
            i += 1

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
