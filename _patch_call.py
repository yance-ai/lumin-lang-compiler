import re

# 读取文件
with open('src/ir/ir_cgen_emit.c', 'r', encoding='utf-8') as f:
    content = f.read()

# 查找函数调用的代码（非变参版本）
old_call = '''                } else {
                    fprintf(out, "        __stk[__sp++] = lumyr_func_%s(", nm);
                    for(int k = 0; k < fixed; k++) {
                        if(k) fprintf(out, ", ");
                        if(k < nbind) fprintf(out, "__args[%d]", k);
                        else fprintf(out, "val_none()");
                    }
                    fprintf(out, ");\n");
                }
                fprintf(out, "    }\n");'''

new_call = '''                } else {
                    fprintf(out, "        __stk[__sp++] = lumyr_func_%s(", nm);
                    for(int k = 0; k < fixed; k++) {
                        if(k) fprintf(out, ", ");
                        if(k < nbind) {
                            /* ref 参数：传递指针（引用传递） */
                            if(callee->param_is_ref && callee->param_is_ref[k])
                                fprintf(out, "&__args[%d]", k);
                            else
                                fprintf(out, "__args[%d]", k);
                        }
                        else fprintf(out, "val_none()");
                    }
                    fprintf(out, ");\n");
                    /* ref 参数：函数返回后把修改写回栈上原来的位置 */
                    for(int k = 0; k < fixed && k < nbind; k++) {
                        if(callee->param_is_ref && callee->param_is_ref[k]) {
                            fprintf(out, "        __stk[__sp - 1 - %d] = __args[%d];\n", nbind - k, k);
                        }
                    }
                }
                fprintf(out, "    }\n");'''

if old_call in content:
    content = content.replace(old_call, new_call)
    print("替换函数调用代码成功")
else:
    print("未找到函数调用代码")

# 写入文件
with open('src/ir/ir_cgen_emit.c', 'w', encoding='utf-8') as f:
    f.write(content)
