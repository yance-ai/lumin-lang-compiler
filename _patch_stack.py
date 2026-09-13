import re

# 读取文件
with open('src/ir/bytecode.c', 'r', encoding='utf-8') as f:
    content = f.read()

# 修改第一个地方（第 132 行附近）
old1 = '''        case OPC_LOAD_CONST:
        case OPC_LOAD_VAR:
        case OPC_GETFUNC:
        case OPC_MKCLOSURE:
        case OPC_PRE_INC: case OPC_POST_INC: case OPC_PRE_DEC: case OPC_POST_DEC:
        case OPC_DUP:
            return +1;'''

new1 = '''        case OPC_LOAD_CONST:
        case OPC_LOAD_VAR:
        case OPC_LOAD_VAR_REF:
        case OPC_GETFUNC:
        case OPC_MKCLOSURE:
        case OPC_PRE_INC: case OPC_POST_INC: case OPC_PRE_DEC: case OPC_POST_DEC:
        case OPC_DUP:
            return +1;'''

if old1 in content:
    content = content.replace(old1, new1)
    print("修改第一个地方成功")
else:
    print("未找到第一个地方")

# 修改第二个地方（第 208 行附近）
old2 = '''        case OPC_LOAD_CONST:
        case OPC_LOAD_VAR:
        case OPC_GETFUNC:
        case OPC_MKCLOSURE:
        case OPC_PRE_INC: case OPC_POST_INC: case OPC_PRE_DEC: case OPC_POST_DEC:
        case OPC_DUP:
        case OPC_LOAD_FIELD:
            return 1;'''

new2 = '''        case OPC_LOAD_CONST:
        case OPC_LOAD_VAR:
        case OPC_LOAD_VAR_REF:
        case OPC_GETFUNC:
        case OPC_MKCLOSURE:
        case OPC_PRE_INC: case OPC_POST_INC: case OPC_PRE_DEC: case OPC_POST_DEC:
        case OPC_DUP:
        case OPC_LOAD_FIELD:
            return 1;'''

if old2 in content:
    content = content.replace(old2, new2)
    print("修改第二个地方成功")
else:
    print("未找到第二个地方")

# 写入文件
with open('src/ir/bytecode.c', 'w', encoding='utf-8') as f:
    f.write(content)
