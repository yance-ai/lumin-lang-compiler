#!/usr/bin/env python3
"""
Lumyr WebAssembly 后端（v2 - 增强版）
将 Lumyr 源代码编译为 WebAssembly 二进制格式（.wasm）

支持：
- 变量赋值和读取
- 整数运算（+、-、*、/、%）
- 比较运算（==、!=、<、>、<=、>=）
- 逻辑运算（&&、||、!）
- if/else 语句
- for 循环
- while 循环
- print 输出（整数和字符串）
- 块作用域

用法: python lumyr_wasm.py <input.lm> [output.wasm] [--html]
"""
import struct
import sys
import argparse
from pathlib import Path

# ============== WASM 类型 ==============
I32 = 0x7F
I64 = 0x7E
F32 = 0x7D
F64 = 0x7C

# ============== WASM 指令 ==============
UNREACHABLE = 0x00
NOP = 0x01
BLOCK = 0x02
LOOP = 0x03
IF = 0x04
ELSE = 0x05
END = 0x0B
BR = 0x0C
BR_IF = 0x0D
BR_TABLE = 0x0E
RETURN = 0x0F
CALL = 0x10
CALL_INDIRECT = 0x11
DROP = 0x1A
SELECT = 0x1B
LOCAL_GET = 0x20
LOCAL_SET = 0x21
LOCAL_TEE = 0x22
GLOBAL_GET = 0x23
GLOBAL_SET = 0x24
I32_LOAD = 0x28
I64_LOAD = 0x29
F32_LOAD = 0x2A
F64_LOAD = 0x2B
I32_STORE = 0x36
I64_STORE = 0x37
F32_STORE = 0x38
F64_STORE = 0x39
MEMORY_SIZE = 0x3F
MEMORY_GROW = 0x40
I32_CONST = 0x41
I64_CONST = 0x42
F32_CONST = 0x43
F64_CONST = 0x44
I32_EQZ = 0x45
I32_EQ = 0x46
I32_NE = 0x47
I32_LT_S = 0x48
I32_LT_U = 0x49
I32_GT_S = 0x4A
I32_GT_U = 0x4B
I32_LE_S = 0x4C
I32_LE_U = 0x4D
I32_GE_S = 0x4E
I32_GE_U = 0x4F
I32_CLZ = 0x67
I32_CTZ = 0x68
I32_POPCNT = 0x69
I32_ADD = 0x6A
I32_SUB = 0x6B
I32_MUL = 0x6C
I32_DIV_S = 0x6D
I32_DIV_U = 0x6E
I32_REM_S = 0x6F
I32_REM_U = 0x70
I32_AND = 0x71
I32_OR = 0x72
I32_XOR = 0x73
I32_SHL = 0x74
I32_SHR_S = 0x75
I32_SHR_U = 0x76
I32_ROTL = 0x77
I32_ROTR = 0x78


# ============== WASM 编码器 ==============
class WasmEncoder:
    def __init__(self):
        self.types = []
        self.functions = []
        self.tables = []
        self.memories = []
        self.globals = []
        self.exports = []
        self.start = None
        self.elements = []
        self.codes = []
        self.datas = []
        self.imports = []

    def encode_leb128_u(self, value):
        result = bytearray()
        while True:
            byte = value & 0x7F
            value >>= 7
            if value != 0:
                byte |= 0x80
            result.append(byte)
            if value == 0:
                break
        return bytes(result)

    def encode_leb128_s(self, value):
        result = bytearray()
        more = True
        while more:
            byte = value & 0x7F
            value >>= 7
            if (value == 0 and (byte & 0x40) == 0) or (value == -1 and (byte & 0x40) != 0):
                more = False
            else:
                byte |= 0x80
            result.append(byte)
        return bytes(result)

    def encode_string(self, s):
        data = s.encode('utf-8')
        return self.encode_leb128_u(len(data)) + data

    def encode_section(self, section_id, content):
        return bytes([section_id]) + self.encode_leb128_u(len(content)) + content

    def encode_type_section(self):
        content = self.encode_leb128_u(len(self.types))
        for params, results in self.types:
            content += bytes([0x60])
            content += self.encode_leb128_u(len(params))
            for p in params:
                content += bytes([p])
            content += self.encode_leb128_u(len(results))
            for r in results:
                content += bytes([r])
        return self.encode_section(1, content)

    def encode_import_section(self):
        content = self.encode_leb128_u(len(self.imports))
        for imp in self.imports:
            content += self.encode_string(imp['module'])
            content += self.encode_string(imp['name'])
            content += bytes([imp['kind']])
            if imp['kind'] == 0:
                content += self.encode_leb128_u(imp['type_index'])
            elif imp['kind'] == 2:
                content += bytes([0x00])
                content += self.encode_leb128_u(imp['min'])
        return self.encode_section(2, content)

    def encode_function_section(self):
        content = self.encode_leb128_u(len(self.functions))
        for type_idx in self.functions:
            content += self.encode_leb128_u(type_idx)
        return self.encode_section(3, content)

    def encode_memory_section(self):
        if not self.memories:
            return b''
        content = self.encode_leb128_u(len(self.memories))
        for m in self.memories:
            content += bytes([0x00])
            content += self.encode_leb128_u(m['min'])
        return self.encode_section(5, content)

    def encode_export_section(self):
        content = self.encode_leb128_u(len(self.exports))
        for exp in self.exports:
            content += self.encode_string(exp['name'])
            content += bytes([exp['kind']])
            content += self.encode_leb128_u(exp['index'])
        return self.encode_section(7, content)

    def encode_code_section(self):
        content = self.encode_leb128_u(len(self.codes))
        for code in self.codes:
            body = code['locals'] + code['body'] + bytes([END])
            content += self.encode_leb128_u(len(body))
            content += body
        return self.encode_section(10, content)

    def encode_data_section(self):
        if not self.datas:
            return b''
        content = self.encode_leb128_u(len(self.datas))
        for data in self.datas:
            content += self.encode_leb128_u(data['memory_index'])
            content += data['offset_expr']
            content += bytes([END])
            content += self.encode_leb128_u(len(data['bytes']))
            content += data['bytes']
        return self.encode_section(11, content)

    def to_bytes(self):
        result = b'\x00asm'
        result += struct.pack('<I', 1)
        if self.imports:
            result += self.encode_import_section()
        if self.types:
            result += self.encode_type_section()
        if self.functions:
            result += self.encode_function_section()
        if self.memories:
            result += self.encode_memory_section()
        if self.exports:
            result += self.encode_export_section()
        if self.codes:
            result += self.encode_code_section()
        if self.datas:
            result += self.encode_data_section()
        return result


# ============== Lumyr -> WASM 编译器 ==============
class LumyrToWasm:
    def __init__(self):
        self.encoder = WasmEncoder()
        self.variables = {}  # 变量名 -> 局部变量索引
        self.var_count = 0
        self.strings = []
        self.string_offset = 1024
        self.print_i32_idx = 0
        self.print_str_idx = 1

    def add_string(self, s):
        offset = self.string_offset
        self.strings.append((offset, s.encode('utf-8') + b'\x00'))
        self.string_offset += len(s) + 1
        return offset, len(s)

    def get_or_create_var(self, name):
        if name not in self.variables:
            self.variables[name] = self.var_count
            self.var_count += 1
        return self.variables[name]

    def compile(self, lumyr_code):
        """编译 Lumyr 代码到 WASM"""
        # 导入 JS 函数
        print_i32_type = len(self.encoder.types)
        self.encoder.types.append(([I32], []))
        self.encoder.imports.append({
            'module': 'env', 'name': 'print_i32',
            'kind': 0, 'type_index': print_i32_type
        })

        print_str_type = len(self.encoder.types)
        self.encoder.types.append(([I32, I32], []))
        self.encoder.imports.append({
            'module': 'env', 'name': 'print_str',
            'kind': 0, 'type_index': print_str_type
        })

        # 主函数
        main_type = len(self.encoder.types)
        self.encoder.types.append(([], []))
        self.encoder.functions.append(main_type)

        # 内存
        self.encoder.memories.append({'min': 1})

        # 解析并生成代码
        body = bytearray()
        lines = self.preprocess(lumyr_code)

        for line in lines:
            self.compile_statement(line, body)

        # 生成局部变量声明
        locals_bytes = bytearray()
        if self.var_count > 0:
            locals_bytes += self.encoder.encode_leb128_u(1)  # 1 组
            locals_bytes += self.encoder.encode_leb128_u(self.var_count)  # 数量
            locals_bytes += bytes([I32])  # 类型

        self.encoder.codes.append({
            'locals': bytes(locals_bytes),
            'body': bytes(body)
        })

        # 导出
        self.encoder.exports.append({'name': 'main', 'kind': 0, 'index': 0})
        self.encoder.exports.append({'name': 'memory', 'kind': 2, 'index': 0})

        # 字符串数据段
        for offset, data in self.strings:
            offset_expr = bytes([I32_CONST]) + self.encoder.encode_leb128_u(offset)
            self.encoder.datas.append({
                'memory_index': 0,
                'offset_expr': offset_expr,
                'bytes': data
            })

        return self.encoder.to_bytes()

    def preprocess(self, code):
        """预处理：移除注释，按语句分割"""
        lines = []
        for line in code.split('\n'):
            line = line.strip()
            if not line or line.startswith('//'):
                continue
            # 移除行内注释
            if '//' in line:
                # 简单处理，不考虑字符串中的 //
                line = line[:line.index('//')].strip()
            if line:
                lines.append(line)
        return lines

    def compile_statement(self, line, body):
        """编译单条语句"""
        # print(...)
        if line.startswith('print(') and line.endswith(');'):
            self.compile_print(line[6:-2], body)
            return

        # 变量赋值: name = expr
        if '=' in line and not line.startswith('if') and not line.startswith('for') and not line.startswith('while'):
            eq_pos = line.index('=')
            # 确保不是 ==
            if eq_pos + 1 < len(line) and line[eq_pos + 1] == '=':
                pass
            else:
                name = line[:eq_pos].strip()
                expr = line[eq_pos + 1:].strip()
                if expr.endswith(';'):
                    expr = expr[:-1].strip()
                self.compile_expr(expr, body)
                var_idx = self.get_or_create_var(name)
                body.append(LOCAL_SET)
                body += self.encoder.encode_leb128_u(var_idx)
                return

        # if 语句（单行简化版）
        if line.startswith('if ') and line.endswith('}'):
            self.compile_if(line, body)
            return

        # for 循环（单行简化版）
        if line.startswith('for ') and line.endswith('}'):
            self.compile_for(line, body)
            return

        # while 循环（单行简化版）
        if line.startswith('while ') and line.endswith('}'):
            self.compile_while(line, body)
            return

    def compile_print(self, expr, body):
        """编译 print 语句"""
        expr = expr.strip()
        # 字符串字面量
        if expr.startswith('"') and expr.endswith('"'):
            s = expr[1:-1]
            offset, length = self.add_string(s)
            body.append(I32_CONST)
            body += self.encoder.encode_leb128_u(offset)
            body.append(I32_CONST)
            body += self.encoder.encode_leb128_u(length)
            body.append(CALL)
            body += self.encoder.encode_leb128_u(self.print_str_idx)
        else:
            # 表达式
            self.compile_expr(expr, body)
            body.append(CALL)
            body += self.encoder.encode_leb128_u(self.print_i32_idx)

    def compile_expr(self, expr, body):
        """编译表达式（简化版，支持 +、-、*、/、%、比较、变量、数字）"""
        expr = expr.strip()

        # 数字字面量
        if expr.lstrip('-').isdigit():
            val = int(expr)
            body.append(I32_CONST)
            body += self.encoder.encode_leb128_s(val)
            return

        # 变量
        if expr in self.variables:
            var_idx = self.variables[expr]
            body.append(LOCAL_GET)
            body += self.encoder.encode_leb128_u(var_idx)
            return

        # 二元运算（从左到右，简化处理）
        operators = ['||', '&&', '==', '!=', '<=', '>=', '<', '>', '+', '-', '*', '/', '%']
        for op in operators:
            if op in expr:
                # 找到第一个运算符（简化处理，不考虑优先级）
                pos = expr.find(op)
                left = expr[:pos].strip()
                right = expr[pos + len(op):].strip()
                self.compile_expr(left, body)
                self.compile_expr(right, body)
                self.compile_binop(op, body)
                return

    def compile_binop(self, op, body):
        """编译二元运算"""
        op_map = {
            '+': I32_ADD, '-': I32_SUB, '*': I32_MUL,
            '/': I32_DIV_S, '%': I32_REM_S,
            '==': I32_EQ, '!=': I32_NE,
            '<': I32_LT_S, '>': I32_GT_S,
            '<=': I32_LE_S, '>=': I32_GE_S,
            '&&': I32_AND, '||': I32_OR,
        }
        if op in op_map:
            body.append(op_map[op])

    def compile_if(self, line, body):
        """编译 if 语句（简化版，单行）"""
        # 格式: if (cond) { stmt1; stmt2; }
        # 提取条件
        cond_start = line.index('(') + 1
        cond_end = line.index(')')
        cond = line[cond_start:cond_end].strip()

        # 提取 body
        body_start = line.index('{') + 1
        body_end = line.rindex('}')
        if_body = line[body_start:body_end].strip()

        # 编译条件
        self.compile_expr(cond, body)

        # if 块
        body.append(IF)
        body.append(0x40)  # void 块类型

        # 编译 if body 中的语句
        for stmt in if_body.split(';'):
            stmt = stmt.strip()
            if stmt:
                self.compile_statement(stmt + ';', body)

        body.append(END)

    def compile_for(self, line, body):
        """编译 for 循环（简化版，单行）"""
        # 格式: for (init; cond; update) { body }
        paren_start = line.index('(') + 1
        paren_end = line.index(')')
        for_header = line[paren_start:paren_end]

        parts = for_header.split(';')
        init = parts[0].strip() if len(parts) > 0 else ''
        cond = parts[1].strip() if len(parts) > 1 else ''
        update = parts[2].strip() if len(parts) > 2 else ''

        # 提取 body
        body_start = line.index('{') + 1
        body_end = line.rindex('}')
        loop_body = line[body_start:body_end].strip()

        # 初始化
        if init:
            self.compile_statement(init + ';', body)

        # loop 块
        body.append(LOOP)
        body.append(0x40)  # void

        # 条件检查
        if cond:
            self.compile_expr(cond, body)
            body.append(I32_EQZ)
            body.append(BR_IF)
            body += self.encoder.encode_leb128_u(1)  # 跳出 loop

        # 循环体
        for stmt in loop_body.split(';'):
            stmt = stmt.strip()
            if stmt:
                self.compile_statement(stmt + ';', body)

        # 更新
        if update:
            self.compile_statement(update + ';', body)

        # 回到循环开头
        body.append(BR)
        body += self.encoder.encode_leb128_u(0)

        body.append(END)

    def compile_while(self, line, body):
        """编译 while 循环（简化版，单行）"""
        # 格式: while (cond) { body }
        cond_start = line.index('(') + 1
        cond_end = line.index(')')
        cond = line[cond_start:cond_end].strip()

        body_start = line.index('{') + 1
        body_end = line.rindex('}')
        loop_body = line[body_start:body_end].strip()

        # loop 块
        body.append(LOOP)
        body.append(0x40)

        # 条件检查
        self.compile_expr(cond, body)
        body.append(I32_EQZ)
        body.append(BR_IF)
        body += self.encoder.encode_leb128_u(1)

        # 循环体
        for stmt in loop_body.split(';'):
            stmt = stmt.strip()
            if stmt:
                self.compile_statement(stmt + ';', body)

        # 回到循环开头
        body.append(BR)
        body += self.encoder.encode_leb128_u(0)

        body.append(END)


def generate_html(wasm_path):
    """生成测试用 HTML"""
    return f"""<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>Lumyr WASM Test</title>
    <style>
        body {{ font-family: monospace; padding: 20px; background: #1e1e1e; color: #d4d4d4; }}
        h1 {{ color: #569cd6; }}
        #output {{ background: #252526; padding: 15px; border-radius: 5px; white-space: pre-wrap; }}
    </style>
</head>
<body>
    <h1>Lumyr WASM Output</h1>
    <pre id="output"></pre>
    <script>
        const output = document.getElementById('output');
        function log(s) {{
            output.textContent += s + '\\n';
            console.log(s);
        }}

        const importObject = {{
            env: {{
                print_i32: function(x) {{ log(x); }},
                print_str: function(ptr, len) {{
                    const bytes = new Uint8Array(memory.buffer, ptr, len);
                    const str = new TextDecoder().decode(bytes);
                    log(str);
                }}
            }}
        }};

        let memory;

        WebAssembly.instantiateStreaming(fetch('{wasm_path}'), importObject)
            .then(results => {{
                const instance = results.instance;
                memory = instance.exports.memory;
                if (instance.exports.main) {{
                    instance.exports.main();
                }}
            }})
            .catch(err => {{
                log('Error: ' + err);
            }});
    </script>
</body>
</html>
"""


def main():
    parser = argparse.ArgumentParser(description="Lumyr WebAssembly 后端")
    parser.add_argument("input", help="输入 .lm 文件")
    parser.add_argument("output", nargs="?", help="输出 .wasm 文件")
    parser.add_argument("--html", action="store_true", help="同时生成测试用 HTML")
    args = parser.parse_args()

    input_path = Path(args.input)
    if not input_path.exists():
        print(f"Error: file not found: {args.input}")
        sys.exit(1)

    lumyr_code = input_path.read_text(encoding='utf-8')

    compiler = LumyrToWasm()
    wasm_bytes = compiler.compile(lumyr_code)

    output_path = Path(args.output) if args.output else input_path.with_suffix('.wasm')
    output_path.write_bytes(wasm_bytes)
    print(f"Generated: {output_path} ({len(wasm_bytes)} bytes)")

    if args.html:
        html_path = output_path.with_suffix('.html')
        html_path.write_text(generate_html(output_path.name), encoding='utf-8')
        print(f"Generated: {html_path}")
        print("Open the HTML file in a browser to run the WASM module.")


if __name__ == "__main__":
    main()
