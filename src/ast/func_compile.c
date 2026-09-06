#include "func_compile.h"
#include "ast_interp.h"
#include "stackframe.h"
#include "lumin_value.h"
#include <stdlib.h>
#include <string.h>
#include "ast_node.h"

// ========== 解释器IR（运行时，完全脱离AST）==========
typedef enum {
    IR_EVAL_STMT,   // 执行一条语句（这里简化实现：编译阶段把AST序列扁平化保存为IR指令流；真正产品会做字节码）
    IR_RETURN
} IrOp;

typedef struct IRInst {
    IrOp op;
    // 解释模式：编译期把AST节点序列序列化，这里不能存AstNode*；
    // 为演示思路：你可以把AST遍历生成自定义字节码数组；
    // 【重点】正式实现：编译期递归AST，输出操作码+操作数，全部堆拷贝，不保留原AstNode指针。
} IRInst;

// 解释模式专用闭包对象，挂在RuntimeFunc外面，不修改RuntimeFunc结构体！
typedef struct InterpFuncPayload {
    AstNode* body;            // 函数体AST（解释模式直接求值；IR生成后可为NULL）
    IRInst* ir;
    int ir_len;
    char** param_names;       // 参数名拷贝，编译期从AST读出，字符串拷贝，不指向AST内部
    int param_cnt;
    int has_variadic;
} InterpFuncPayload;

// ---- 当前被调函数：解释器entry入口处查询自身payload用 ----
// 单线程解释器，调用点先set、entry入口立即读取到局部变量，之后嵌套调用不影响
static RuntimeFunc* s_current_rf = NULL;

// 解释器函数入口回调：FuncEntry 签名必须严格匹配
static Value interp_func_entry(int arg_cnt, const Value* args, EvalCtx* ctx, StackFrame* frame);

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
    // 解释模式：持有函数体AST指针，运行时由 interp_func_entry 直接求值
    // （AST 树由 ast_free 统一释放，此处只借用，不释放）
    payload->body = func_def_ast->u.func_def.body;
    payload->ir = NULL;
    payload->ir_len = 0;
    // ✅ 这里：编译期遍历函数体AST，递归生成IR指令流，把所有信息拷贝到IR，之后AST可以释放。
    // generate_ir_from_ast(payload, func_def_ast->u.func_def.body);

    // 构造RuntimeFunc，原有字段一个不动
    RuntimeFunc* rf = malloc(sizeof(RuntimeFunc));
    rf->entry = interp_func_entry;
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

void runtime_func_destroy(RuntimeFunc* f)
{
    if(!f) return;
    if(f->capture_count == -1) {
        InterpFuncPayload* pl = (InterpFuncPayload*)f->captures;
        for(int i=0;i<pl->param_cnt + pl->has_variadic;i++) {
            free(pl->param_names[i]);
        }
        free(pl->param_names);
        free(pl->ir);
        free(pl);
    } else {
        for(int i=0;i<f->capture_count;i++) {
            val_destroy(&f->captures[i]);
        }
        free(f->captures);
    }
    free(f);
}

// 解释器函数入口：FuncEntry 签名（4参），运行时执行，完全看不到AST之外的IR
static Value interp_func_entry(int arg_cnt, const Value* args, EvalCtx* ctx, StackFrame* frame)
{
    (void)arg_cnt;
    (void)args;
    // 调用点已通过 interp_set_current_rf 设置当前函数；入口立即读取到局部变量
    RuntimeFunc* self = s_current_rf;
    if(!self || !interp_func_is_payload(self)) {
        runtime_error("interp_func_entry: 缺少当前函数上下文");
        return val_none();
    }
    InterpFuncPayload* pl = (InterpFuncPayload*)self->captures;
    if(!pl->body) return val_none();

    // 执行函数体（参数已由调用方绑定进 frame）
    Value ret = ast_eval_ctx(pl->body, ctx, frame);

    // 函数体内 return：消费 hit_return，返回 ctx->ret_val（AST_RETURN 已克隆，独立于栈帧）
    if(ctx->hit_return) {
        ctx->hit_return = 0;
        Value rv = ctx->ret_val;
        ctx->ret_val = val_none();
        return rv;
    }
    // 无 return：body返回值不带走（可能指向帧内资源），默认返回 nil
    (void)ret;
    return val_none();
}
