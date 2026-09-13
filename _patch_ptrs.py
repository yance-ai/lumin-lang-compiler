import re

# 读取文件
with open('src/ir/ir_cgen.c', 'r', encoding='utf-8') as f:
    content = f.read()

# 查找 __local_ptrs 数组初始化中参数处理的代码
old_ptrs = '''        for(int i = 0; i < _total_params; i++) {
            if(_idx) fprintf(out, ", ");
            /* 装箱参数：lmloc_<name> 已是 Value*（堆 cell）；普通参数取栈地址 */
            if(ns_has(&g_boxed, fn->params[i]))
                fprintf(out, "lmloc_%s", fn->params[i]);
            else
                fprintf(out, "&lmloc_%s", fn->params[i]);
            _idx++;
        }'''

new_ptrs = '''        for(int i = 0; i < _total_params; i++) {
            if(_idx) fprintf(out, ", ");
            /* ref 参数：lmloc_<name> 已是 Value*（引用传递）；
               装箱参数：lmloc_<name> 已是 Value*（堆 cell）；
               普通参数取栈地址 */
            if((i < fn->param_cnt && fn->param_is_ref && fn->param_is_ref[i]) ||
               ns_has(&g_boxed, fn->params[i]))
                fprintf(out, "lmloc_%s", fn->params[i]);
            else
                fprintf(out, "&lmloc_%s", fn->params[i]);
            _idx++;
        }'''

if old_ptrs in content:
    content = content.replace(old_ptrs, new_ptrs)
    print("修改 __local_ptrs 数组初始化成功")
else:
    print("未找到 __local_ptrs 数组初始化代码")

# 写入文件
with open('src/ir/ir_cgen.c', 'w', encoding='utf-8') as f:
    f.write(content)
