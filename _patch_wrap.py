import re

# 读取文件
with open('src/ir/ir_cgen.c', 'r', encoding='utf-8') as f:
    content = f.read()

# 查找 lum_wrap_N 函数中调用 lumyr_func 的代码
old_wrap = '''        } else {
            fprintf(out, "    return lumyr_func_%s(", fn->name);
            int total = fn->param_cnt + (fn->has_variadic ? 1 : 0);
            if(has_caps)
                fprintf(out, "(Value**)__ctx");
            for(int k = 0; k < total; k++) {
                if(has_caps || k) fprintf(out, ", ");
                if(k < fn->param_cnt) fprintf(out, "p%d", k);
                else {
                    fprintf(out, "__rest");
                }
            }
            fprintf(out, ");\\n}\\n\\n");
        }'''

new_wrap = '''        } else {
            fprintf(out, "    Value __wrap_ret = lumyr_func_%s(", fn->name);
            int total = fn->param_cnt + (fn->has_variadic ? 1 : 0);
            if(has_caps)
                fprintf(out, "(Value**)__ctx");
            for(int k = 0; k < total; k++) {
                if(has_caps || k) fprintf(out, ", ");
                if(k < fn->param_cnt) {
                    /* ref 参数：传递指针（引用传递） */
                    if(fn->param_is_ref && fn->param_is_ref[k])
                        fprintf(out, "&p%d", k);
                    else
                        fprintf(out, "p%d", k);
                }
                else {
                    fprintf(out, "__rest");
                }
            }
            fprintf(out, ");\\n");
            /* ref 参数：函数返回后把修改写回 a[] 数组 */
            for(int k = 0; k < fn->param_cnt; k++) {
                if(fn->param_is_ref && fn->param_is_ref[k]) {
                    fprintf(out, "    if(n > %d) a[%d] = p%d;\\n", k, k, k);
                }
            }
            fprintf(out, "    return __wrap_ret;\\n}\\n\\n");
        }'''

if old_wrap in content:
    content = content.replace(old_wrap, new_wrap)
    print("修改 lum_wrap_N 函数成功")
else:
    print("未找到 lum_wrap_N 函数代码")

# 写入文件
with open('src/ir/ir_cgen.c', 'w', encoding='utf-8') as f:
    f.write(content)
