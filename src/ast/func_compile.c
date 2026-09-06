#include "func_compile.h"
#include "ast_interp.h"
#include "stackframe.h"
#include "lumin_value.h"
#include <stdlib.h>
#include <string.h>
#include "ast_node.h"
#include "ir/ir_compile.h"
#include "ast_runtime_sym.h"
#include "ir/vm.h"

// ---- 当前被调函数：解释器entry入口处查询自身payload用 ----
// 调用点先set、entry入口立即读取到局部变量，之后嵌套调用不影响
// _Thread_local：多线程 VM 通道（thread 启动的线程各自调用函数）需要每线程隔离
static _Thread_local RuntimeFunc* s_current_rf = NULL;

RuntimeFunc* interp_set_current_rf(RuntimeFunc* rf)
{
    RuntimeFunc* old = s_current_rf;
    s_current_rf = rf;
    return old;
}

RuntimeFunc* interp_current_rf(void)
{
    return s_current_rf;
}

_Bool interp_func_is_payload(const RuntimeFunc* rf)
{
    return rf && rf->capture_count == -1;
}

int interp_func_param_cnt(const RuntimeFunc* rf)
{
    if(!interp_func_is_payload(rf)) return 0;
    InterpFuncPayload* pl = (InterpFuncPayload*)rf->captures;
    return pl->param_cnt;
}

_Bool interp_func_has_variadic(const RuntimeFunc* rf)
{
    if(!interp_func_is_payload(rf)) return 0;
    InterpFuncPayload* pl = (InterpFuncPayload*)rf->captures;
    return pl->has_variadic;
}

const char* interp_func_param_name(const RuntimeFunc* rf, int idx)
{
    if(!interp_func_is_payload(rf)) return NULL;
    InterpFuncPayload* pl = (InterpFuncPayload*)rf->captures;
    if(idx < 0 || idx >= pl->param_cnt + pl->has_variadic) return NULL;
    return pl->param_names[idx];
}

RuntimeFunc* compile_func_from_ast(AstNode* func_def_ast)
{
    if(func_def_ast->type != AST_FUNC_DEF) return NULL;

    // 1. 解析参数链表 AST_PARAM，拷贝参数名，**只拷贝字符串，不存AST指针**
    int normal_cnt = 0;
    int has_var = 0;
    AstNode* p = func_def_ast->u.func_def.params;
    while(p) {
        if(p->u.param.is_ellipsis) {
            has_var = 1;
        } else {
            normal_cnt ++;
        }
        p = p->u.param.next;
    }

    // 分配解释器负载（不修改RuntimeFunc原有结构体！）
    InterpFuncPayload* payload = malloc(sizeof(InterpFuncPayload));
    payload->param_cnt = normal_cnt;
    payload->has_variadic = has_var;
    payload->param_names = malloc(sizeof(char*)*(normal_cnt + (has_var?1:0)));

    // 拷贝参数名字
    p = func_def_ast->u.func_def.params;
    int idx = 0;
    while(p) {
        payload->param_names[idx] = strdup(p->u.param.name);
        idx++;
        p = p->u.param.next;
    }
    // 编译函数体为字节码 IR（VM 执行；不再直接求值 AST）
    payload->body = func_def_ast->u.func_def.body;
    payload->bytecode = ir_compile_function(func_def_ast->u.func_def.name,
                                            func_def_ast->u.func_def.params,
                                            func_def_ast->u.func_def.body);

    // 构造RuntimeFunc，原有字段一个不动
    RuntimeFunc* rf = malloc(sizeof(RuntimeFunc));
    rf->entry = vm_func_entry;              // VM 执行字节码
    rf->param_count = normal_cnt;
    rf->has_variadic = has_var;
    rf->captures = NULL;
    rf->capture_count = 0;

    // trick：RuntimeFunc没有payload字段，用captures临时存payload指针（或者包装一层wrapper，不修改原有结构体）
    // 【重要】生产环境建议外层包一层wrapper，这里为不改结构体，用captures指针存payload，仅解释器模式使用；编译模式完全不用。
    rf->captures = (Value*)payload;
    rf->capture_count = -1; // 标记这是解释器payload，不是真实捕获变量

    return rf;
}

// typecheck 把 AST_VAR（函数名）就地转成 AST_FUNCREF 后，重新编译该函数的
// 字节码并替换（parse 期生成的旧字节码里函数名引用还是 LOAD_VAR）。
void func_compile_recompile(AstNode* def)
{
    if(!def || def->type != AST_FUNC_DEF) return;
    const char* name = def->u.func_def.name;
    BytecodeFunc* nb = ir_func_table_recompile(name, def->u.func_def.params, def->u.func_def.body);
    Value fv = sym_get(name);
    if(fv.type == VAL_FUNC) {
        RuntimeFunc* rf = (RuntimeFunc*)fv.v.func.func_obj;
        InterpFuncPayload* pl = (InterpFuncPayload*)rf->captures;
        /* 旧字节码已由 ir_func_table_recompile 释放（原位替换），这里只更新指针 */
        if(pl) pl->bytecode = nb;
    }
}

void runtime_func_destroy(RuntimeFunc* f)
{
    if(!f) return;
    if(f->capture_count == -1) {
        InterpFuncPayload* pl = (InterpFuncPayload*)f->captures;
        for(int i=0;i<pl->param_cnt + pl->has_variadic;i++) {
            free(pl->param_names[i]);
        }
        free(pl->param_names);
        bytecode_func_free(pl->bytecode);
        free(pl);
    } else {
        for(int i=0;i<f->capture_count;i++) {
            val_destroy(&f->captures[i]);
        }
        free(f->captures);
    }
    free(f);
}

