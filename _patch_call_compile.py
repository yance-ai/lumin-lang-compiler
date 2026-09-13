import re

# 读取文件
with open('src/ir/ir_compile.c', 'r', encoding='utf-8') as f:
    content = f.read()

# 修改函数调用的编译代码，在编译参数时传递 param_is_ref 数组
old_call = '''            } else {
                c_args(c, node->u.call.args, &argc);
            }
            // 用户函数优先；否则内置函数（len/type/input/range/substr）'''

new_call = '''            } else {
                /* 查找被调用函数，获取 param_is_ref 数组 */
                BytecodeFunc* _callee_fn = ir_func_table_lookup(node->u.call.name);
                int* _call_param_is_ref = (_callee_fn && _callee_fn->param_is_ref) ? _callee_fn->param_is_ref : NULL;
                int _call_ref_idx = 0;
                c_args_ref(c, node->u.call.args, &argc, _call_param_is_ref, &_call_ref_idx);
            }
            // 用户函数优先；否则内置函数（len/type/input/range/substr）'''

if old_call in content:
    content = content.replace(old_call, new_call)
    print("修改函数调用编译代码成功")
else:
    print("未找到函数调用编译代码")

# 写入文件
with open('src/ir/ir_compile.c', 'w', encoding='utf-8') as f:
    f.write(content)
