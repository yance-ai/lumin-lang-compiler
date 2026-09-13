import re

# 读取文件
with open('src/ir/ir_cgen.c', 'r', encoding='utf-8') as f:
    content = f.read()

# 1. 添加 fn_param_index 函数（在 fn_has_param 后面）
old_fn_has = '''int fn_has_param(const BytecodeFunc* fn, const char* name)
{
    int total = fn->param_cnt + (fn->has_variadic ? 1 : 0);
    for(int i = 0; i < total; i++)
        if(fn->params[i] && strcmp(fn->params[i], name) == 0) return 1;
    return 0;
}'''

new_fn_has = '''int fn_has_param(const BytecodeFunc* fn, const char* name)
{
    int total = fn->param_cnt + (fn->has_variadic ? 1 : 0);
    for(int i = 0; i < total; i++)
        if(fn->params[i] && strcmp(fn->params[i], name) == 0) return 1;
    return 0;
}

// 查找参数名在参数数组中的下标；未找到返回 -1
int fn_param_index(const BytecodeFunc* fn, const char* name)
{
    int total = fn->param_cnt + (fn->has_variadic ? 1 : 0);
    for(int i = 0; i < total; i++)
        if(fn->params[i] && strcmp(fn->params[i], name) == 0) return i;
    return -1;
}'''

if old_fn_has in content:
    content = content.replace(old_fn_has, new_fn_has)
    print("添加 fn_param_index 函数成功")
else:
    print("未找到 fn_has_param 函数")

# 2. 修改 cvar_rw 函数，对于 ref 参数，返回 *lmloc_<name>
old_cvar = '''    if(g_cur_fn && (fn_has_param(g_cur_fn, name) || ns_has(&fn_locals, name))) {
        if(ns_has(&g_boxed, name))
            snprintf(buf, sizeof(buf), g_is_generator ? "*g->lmloc_%s" : "*lmloc_%s", name);
        else
            snprintf(buf, sizeof(buf), g_is_generator ? "g->lmloc_%s" : "lmloc_%s", name);
    } else {'''

new_cvar = '''    if(g_cur_fn && (fn_has_param(g_cur_fn, name) || ns_has(&fn_locals, name))) {
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

if old_cvar in content:
    content = content.replace(old_cvar, new_cvar)
    print("修改 cvar_rw 函数成功")
else:
    print("未找到 cvar_rw 函数")

# 写入文件
with open('src/ir/ir_cgen.c', 'w', encoding='utf-8') as f:
    f.write(content)
