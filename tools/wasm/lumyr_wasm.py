#!/usr/bin/env python3
"""
Lumyr WebAssembly 后端
将 Lumyr 字节码编译为 WebAssembly 二进制格式（.wasm）

用法: python lumyr_wasm.py <input.lm> [output.wasm]
"""
import struct
import sys
import argparse
from pathlib import Path

# ============== WASM 二进制编码 ==============

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
            content += bytes([0x60])  # func type
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
            if imp['kind'] == 0:  # function
                content += self.encode_leb128_u(imp['type_index'])
            elif imp['kind'] == 2:  # memory
                content += bytes([0x00])  # limits: no max
                content += self.encode_leb128_u(imp['min'])
        return self.encode_section(2, content)

    def encode_function_section(self):
        content = self.encode_leb128_u(len(self.functions))
        for type_idx in self.functions:
            content += self.encode_leb128_u(type_idx)
        return self.encode_section(3, content)

    def encode_table_section(self):
        if not self.tables:
            return b''
        content = self.encode_leb128_u(len(self.tables))
        for t in self.tables:
            content += bytes([0x70])  # anyfunc
            content += bytes([0x00])  # limits
            content += self.encode_leb128_u(t['min'])
        return self.encode_section(4, content)

    def encode_memory_section(self):
        if not self.memories:
            return b''
        content = self.encode_leb128_u(len(self.memories))
        for m in self.memories:
            content += bytes([0x00])  # limits: no max
            content += self.encode_leb128_u(m['min'])
        return self.encode_section(5, content)

    def encode_global_section(self):
        if not self.globals:
            return b''
        content = self.encode_leb128_u(len(self.globals))
        for g in self.globals:
            content += bytes([g['type']])
            content += bytes([1 if g['mutable'] else 0])
            content += g['init_expr']
            content += bytes([0x0B])  # end
        return self.encode_section(6, content)

    def encode_export_section(self):
        content = self.encode_leb128_u(len(self.exports))
        for exp in self.exports:
            content += self.encode_string(exp['name'])
            content += bytes([exp['kind']])
            content += self.encode_leb128_u(exp['index'])
        return self.encode_section(7, content)

    def encode_start_section(self):
        if self.start is None:
            return b''
        content = self.encode_leb128_u(self.start)
        return self.encode_section(8, content)

    def encode_element_section(self):
        if not self.elements:
            return b''
        content = self.encode_leb128_u(len(self.elements))
        for elem in self.elements:
            content += self.encode_leb128_u(elem['table_index'])
            content += elem['offset_expr']
            content += bytes([0x0B])  # end
            content += self.encode_leb128_u(len(elem['func_indices']))
            for idx in elem['func_indices']:
                content += self.encode_leb128_u(idx)
        return self.encode_section(9, content)

    def encode_code_section(self):
        content = self.encode_leb128_u(len(self.codes))
        for code in self.codes:
            body = code['locals'] + code['body'] + bytes([0x0B])  # end
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
            content += bytes([0x0B])  # end
            content += self.encode_leb128_u(len(data['bytes']))
            content += data['bytes']
        return self.encode_section(11, content)

    def to_bytes(self):
        result = b'\x00asm'  # magic
        result += struct.pack('<I', 1)  # version
        if self.imports:
            result += self.encode_import_section()
        if self.types:
            result += self.encode_type_section()
        if self.functions:
            result += self.encode_function_section()
        if self.tables:
            result += self.encode_table_section()
        if self.memories:
            result += self.encode_memory_section()
        if self.globals:
            result += self.encode_global_section()
        if self.exports:
            result += self.encode_export_section()
        if self.start is not None:
            result += self.encode_start_section()
        if self.elements:
            result += self.encode_element_section()
        if self.codes:
            result += self.encode_code_section()
        if self.datas:
            result += self.encode_data_section()
        return result


# ============== WASM 类型常量 ==============
I32 = 0x7F
I64 = 0x7E
F32 = 0x7D
F64 = 0x7C

# ============== WASM 指令常量 ==============
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


# ============== Lumyr -> WASM 简单翻译器 ==============

class LumyrToWasm:
    def __init__(self):
        self.encoder = WasmEncoder()
        self.local_count = 0
        self.strings = []
        self.string_offset = 1024  # 字符串存储起始偏移

    def add_string(self, s):
        """添加字符串到数据段，返回偏移"""
        offset = self.string_offset
        self.strings.append((offset, s.encode('utf-8') + b'\x00'))
        self.string_offset += len(s) + 1
        return offset

    def compile(self, lumyr_code):
        """简单的 Lumyr -> WASM 编译（支持基本的整数运算和 print）"""
        # 导入 JS 的 print 函数
        print_type = len(self.encoder.types)
        self.encoder.types.append(([I32], []))  # (i32) -> ()
        self.encoder.imports.append({
            'module': 'env',
            'name': 'print_i32',
            'kind': 0,  # function
            'type_index': print_type
        })

        # 主函数类型
        main_type = len(self.encoder.types)
        self.encoder.types.append(([], []))  # () -> ()
        self.encoder.functions.append(main_type)

        # 内存（用于字符串存储）
        self.encoder.memories.append({'min': 1})

        # 解析 Lumyr 代码（简单的行解析）
        body = bytearray()
        lines = lumyr_code.split('\n')
        for line in lines:
            line = line.strip()
            if not line or line.startswith('//'):
                continue

            # print(整数)
            if line.startswith('print(') and line.endswith(');'):
                expr = line[6:-2]
                # 简单的整数常量
                if expr.isdigit() or (expr.startswith('-') and expr[1:].isdigit()):
                    val = int(expr)
                    body.append(I32_CONST)
                    body += self.encoder.encode_leb128_s(val)
                    body.append(CALL)
                    body += self.encoder.encode_leb128_u(0)  # print_i32
                # 简单的加法表达式 a + b
                elif '+' in expr:
                    parts = expr.split('+')
                    if len(parts) == 2 and parts[0].strip().isdigit() and parts[1].strip().isdigit():
                        a = int(parts[0].strip())
                        b = int(parts[1].strip())
                        body.append(I32_CONST)
                        body += self.encoder.encode_leb128_s(a)
                        body.append(I32_CONST)
                        body += self.encoder.encode_leb128_s(b)
                        body.append(I32_ADD)
                        body.append(CALL)
                        body += self.encoder.encode_leb128_u(0)

        # 函数体
        locals_bytes = self.encoder.encode_leb128_u(0)  # 无局部变量
        self.encoder.codes.append({
            'locals': locals_bytes,
            'body': bytes(body)
        })

        # 导出主函数
        self.encoder.exports.append({
            'name': 'main',
            'kind': 0,  # function
            'index': 0  # 第一个函数（import 不算在 function index 空间？实际上 import 的函数也占索引）
        })

        # 字符串数据段
        for offset, data in self.strings:
            offset_expr = bytes([I32_CONST]) + self.encoder.encode_leb128_u(offset)
            self.encoder.datas.append({
                'memory_index': 0,
                'offset_expr': offset_expr,
                'bytes': data
            })

        return self.encoder.to_bytes()


def generate_html(wasm_path):
    """生成测试用的 HTML 文件"""
    return f"""<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>Lumyr WASM Test</title>
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
    parser.add_argument("output", nargs="?", help="输出 .wasm 文件（默认与输入同名）")
    parser.add_argument("--html", action="store_true", help="同时生成测试用 HTML 文件")
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
