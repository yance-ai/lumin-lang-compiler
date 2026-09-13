import re

# 读取文件
with open('src/ir/vm.c', 'r', encoding='utf-8') as f:
    content = f.read()

# 在 OPC_LOAD_VAR 后面添加 OPC_LOAD_VAR_REF
old_load_var = '''            case OPC_LOAD_VAR: {
                const char* name = bf->syms[in.a];
                _Bool fnd = 0;
                Value vv = stackframe_get(frame, name, &fnd);
                if(!fnd) runtime_undefined("变量", name);
                stack[sp++] = vv;
                break;
            }
            case OPC_STORE_VAR: {'''

new_load_var = '''            case OPC_LOAD_VAR: {
                const char* name = bf->syms[in.a];
                _Bool fnd = 0;
                Value vv = stackframe_get(frame, name, &fnd);
                if(!fnd) runtime_undefined("变量", name);
                stack[sp++] = vv;
                break;
            }
            case OPC_LOAD_VAR_REF: {
                /* ref 参数：和 OPC_LOAD_VAR 行为相同（VM 模式下 struct 本来就是 Value(map)） */
                const char* name = bf->syms[in.a];
                _Bool fnd = 0;
                Value vv = stackframe_get(frame, name, &fnd);
                if(!fnd) runtime_undefined("变量", name);
                stack[sp++] = vv;
                break;
            }
            case OPC_STORE_VAR: {'''

if old_load_var in content:
    content = content.replace(old_load_var, new_load_var)
    print("添加 OPC_LOAD_VAR_REF VM 模式实现成功")
else:
    print("未找到 OPC_LOAD_VAR VM 模式实现")

# 写入文件
with open('src/ir/vm.c', 'w', encoding='utf-8') as f:
    f.write(content)
