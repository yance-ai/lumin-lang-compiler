import re

# 读取文件
with open('src/ir/ir_cgen.c', 'r', encoding='utf-8') as f:
    content = f.read()

# 查找函数前向声明的代码
old_forward = '''    int has_caps = lambda_has_captures(fn->name);
    fprintf(out, "static Value lumyr_func_%s(", fn->name);
    if(has_caps) fprintf(out, "Value** __caps");
    int total = fn->param_cnt + (fn->has_variadic ? 1 : 0);
    for(int i = 0; i < total; i++) {
        if(has_caps || i) fprintf(out, ", ");
        fprintf(out, "Value");
    }
    fprintf(out, ");\\n");
}'''

new_forward = '''    int has_caps = lambda_has_captures(fn->name);
    fprintf(out, "static Value lumyr_func_%s(", fn->name);
    if(has_caps) fprintf(out, "Value** __caps");
    int total = fn->param_cnt + (fn->has_variadic ? 1 : 0);
    for(int i = 0; i < total; i++) {
        if(has_caps || i) fprintf(out, ", ");
        /* ref 参数：声明为 Value* 指针（引用传递） */
        if(i < fn->param_cnt && fn->param_is_ref && fn->param_is_ref[i])
            fprintf(out, "Value*");
        else
            fprintf(out, "Value");
    }
    fprintf(out, ");\\n");
}'''

if old_forward in content:
    content = content.replace(old_forward, new_forward)
    print("修改函数前向声明成功")
else:
    print("未找到函数前向声明代码")

# 写入文件
with open('src/ir/ir_cgen.c', 'w', encoding='utf-8') as f:
    f.write(content)
