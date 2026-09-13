import re

# 读取文件
with open('src/ir/bytecode.h', 'r', encoding='utf-8') as f:
    content = f.read()

# 添加 OPC_LOAD_VAR_REF 操作码
old_opc = '''    OPC_LOAD_VAR,       // a=符号表下标
    OPC_STORE_VAR,      // a=符号表下标；弹值写变量（深拷贝入帧），原值压回（表达式值）'''

new_opc = '''    OPC_LOAD_VAR,       // a=符号表下标
    OPC_LOAD_VAR_REF,   // a=符号表下标；加载 ref 参数（struct 不转 Map，直接传递 VAL_STRUCT_PTR）
    OPC_STORE_VAR,      // a=符号表下标；弹值写变量（深拷贝入帧），原值压回（表达式值）'''

if old_opc in content:
    content = content.replace(old_opc, new_opc)
    print("添加 OPC_LOAD_VAR_REF 操作码成功")
else:
    print("未找到 OPC_LOAD_VAR 操作码")

# 写入文件
with open('src/ir/bytecode.h', 'w', encoding='utf-8') as f:
    f.write(content)
