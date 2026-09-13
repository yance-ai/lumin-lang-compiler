import re

# 读取文件
with open('src/ir/ir_cgen.c', 'r', encoding='utf-8') as f:
    content = f.read()

# 查找函数定义时参数声明的代码
old_code = '''    for(int i = 0; i < total; i++) {
        if(has_caps || i) fprintf(out, ", ");
        /* 装箱参数：入参用 _in 后缀，函数入口处再装箱为 lmloc_<name>(Value*) */
        if(ns_has(&g_boxed, fn->params[i]))
            fprintf(out, "Value lmloc_%s_in", fn->params[i]);
        else
            fprintf(out, "Value lmloc_%s", fn->params[i]);
    }'''

new_code = '''    for(int i = 0; i < total; i++) {
        if(has_caps || i) fprintf(out, ", ");
        /* ref 参数：声明为 Value* 指针（引用传递） */
        if(i < fn->param_cnt && fn->param_is_ref && fn->param_is_ref[i]) {
            fprintf(out, "Value* lmloc_%s", fn->params[i]);
        }
        /* 装箱参数：入参用 _in 后缀，函数入口处再装箱为 lmloc_<name>(Value*) */
        else if(ns_has(&g_boxed, fn->params[i]))
            fprintf(out, "Value lmloc_%s_in", fn->params[i]);
        else
            fprintf(out, "Value lmloc_%s", fn->params[i]);
    }'''

if old_code in content:
    content = content.replace(old_code, new_code)
    print("替换成功")
else:
    print("未找到目标代码")
    # 尝试查找类似的代码
    lines = content.split('\n')
    for i, line in enumerate(lines):
        if '装箱参数：入参用 _in 后缀' in line:
            print(f"找到类似代码在第 {i+1} 行: {line}")
            for j in range(max(0, i-2), min(len(lines), i+8)):
                print(f"  {j+1}: {lines[j]}")

# 写入文件
with open('src/ir/ir_cgen.c', 'w', encoding='utf-8') as f:
    f.write(content)
