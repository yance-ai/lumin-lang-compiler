import re

# 读取文件
with open('src/ir/ir_compile.c', 'r', encoding='utf-8') as f:
    content = f.read()

# 修改 c_args 函数，让它接受 param_is_ref 数组
old_c_args = '''static void c_args(Ctx* c, AstNode* args, int* argc)
{
    if(!args) return;
    if(args->type != AST_SEQ) {
        c_expr(c, args);
        (*argc)++;
        return;
    }
    c_args(c, args->u.seq.first, argc);
    c_args(c, args->u.seq.second, argc);
}'''

new_c_args = '''static void c_args_ref(Ctx* c, AstNode* args, int* argc, int* param_is_ref, int* ref_idx)
{
    if(!args) return;
    if(args->type != AST_SEQ) {
        /* ref 参数的变量引用：使用 OPC_LOAD_VAR_REF（struct 不转 Map） */
        if(args->type == AST_VAR && param_is_ref && *ref_idx >= 0 &&
           *ref_idx < 1024 && param_is_ref[*ref_idx]) {
            emit(c, OPC_LOAD_VAR_REF, bf_sym(c->fn, args->u.varname), 0);
        } else {
            c_expr(c, args);
        }
        (*argc)++;
        (*ref_idx)++;
        return;
    }
    c_args_ref(c, args->u.seq.first, argc, param_is_ref, ref_idx);
    c_args_ref(c, args->u.seq.second, argc, param_is_ref, ref_idx);
}

static void c_args(Ctx* c, AstNode* args, int* argc)
{
    int ref_idx = 0;
    c_args_ref(c, args, argc, NULL, &ref_idx);
}'''

if old_c_args in content:
    content = content.replace(old_c_args, new_c_args)
    print("修改 c_args 函数成功")
else:
    print("未找到 c_args 函数")

# 写入文件
with open('src/ir/ir_compile.c', 'w', encoding='utf-8') as f:
    f.write(content)
