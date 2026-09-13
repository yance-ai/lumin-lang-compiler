import re

# 读取文件
with open('src/ir/ir_cgen.c', 'r', encoding='utf-8') as f:
    content = f.read()

# 查找 cvar_rw 函数
old_code = '''    if(g_cur_fn && (fn_has_param(g_cur_fn, name) || ns_has(&fn_locals, name))) {
        if(ns_has(&g_boxed, name))
            snprintf(buf, sizeof(buf), g_is_generator ? "*g->lmloc_%s" : "*lmloc_%s", name);
        else
            snprintf(buf, sizeof(buf), g_is_generator ? "g->lmloc_%s" : "lmloc_%s", name);
    } else {'''

new_code = '''    if(g_cur_fn && (fn_has_param(g_cur_fn, name) || ns_has(&fn_locals, name))) {
        /* ref 参数：通过指针解引用访问 */
        int _ref_idx = fn_param_index(g_cur_fn, name);
        if(_ref_idx >= 0 && g_cur_fn->param_is_ref && g_cur_fn->param_is_ref[_ref_idx]) {
            snprintf(buf, sizeof(buf), g_is_generator ? "*g->lmloc_%s" : "*lmloc_%s", name);
        }
        else if(ns_has(&g_boxed, name))
            snprintf(buf, sizeof(buf), g_is_generator ? "*g->lmloc_%s" : "*lmloc_%s", name);
        else
            snprintf(buf, sizeof(buf), g_is_generator ? "g->lmloc_%s" : "lmloc_%s", name);
    } else {'''

if old_code in content:
    content = content.replace(old_code, new_code)
    print("替换成功")
else:
    print("未找到目标代码")

# 写入文件
with open('src/ir/ir_cgen.c', 'w', encoding='utf-8') as f:
    f.write(content)
