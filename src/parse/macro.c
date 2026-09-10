#include "macro.h"
#include "ast/ast_node.h"
#include <stdlib.h>
#include <string.h>

// ---- 全局宏表 ----
#define MACRO_MAX 256
static struct {
    char* name;
    AstNode* params;  // AST_PARAM 链表
    AstNode* body;    // 宏体（block_stmt）
} g_macro_table[MACRO_MAX];
static int g_macro_cnt = 0;

// 展开深度上限（防止无限递归）
#define MACRO_EXPAND_DEPTH_MAX 64
static int g_expand_depth = 0;

void macro_register(const char* name, AstNode* params, AstNode* body)
{
    if(g_macro_cnt >= MACRO_MAX) return;
    // 检查是否已注册（重复定义覆盖）
    for(int i = 0; i < g_macro_cnt; i++) {
        if(strcmp(g_macro_table[i].name, name) == 0) {
            g_macro_table[i].params = params;
            g_macro_table[i].body = body;
            return;
        }
    }
    g_macro_table[g_macro_cnt].name = strdup(name);
    g_macro_table[g_macro_cnt].params = params;
    g_macro_table[g_macro_cnt].body = body;
    g_macro_cnt++;
}

AstNode* macro_lookup(const char* name)
{
    for(int i = 0; i < g_macro_cnt; i++) {
        if(strcmp(g_macro_table[i].name, name) == 0) {
            // 返回一个临时的 AST_MACRO_DEF 节点（不持有所有权，仅用于展开）
            AstNode* n = ast_macro_def(g_macro_table[i].name,
                                         g_macro_table[i].params,
                                         g_macro_table[i].body);
            return n;
        }
    }
    return NULL;
}

int macro_is_defined(const char* name)
{
    for(int i = 0; i < g_macro_cnt; i++) {
        if(strcmp(g_macro_table[i].name, name) == 0) return 1;
    }
    return 0;
}

// 检查变量名是否是宏的参数
static int is_macro_param(AstNode* params, const char* name)
{
    AstNode* p = params;
    while(p) {
        if(p->u.param.name && strcmp(p->u.param.name, name) == 0) return 1;
        p = p->u.param.next;
    }
    return 0;
}

// 获取参数的实际参数（按索引）
static AstNode* get_arg_by_index(AstNode* args, int idx)
{
    AstNode* a = args;
    int i = 0;
    while(a) {
        if(i == idx) return a;
        a = a->u.seq.second;
        i++;
    }
    return NULL;
}

// 获取参数名的索引
static int get_param_index(AstNode* params, const char* name)
{
    AstNode* p = params;
    int i = 0;
    while(p) {
        if(p->u.param.name && strcmp(p->u.param.name, name) == 0) return i;
        p = p->u.param.next;
        i++;
    }
    return -1;
}

// 递归替换 AST 中的宏参数为实际参数
static AstNode* substitute_params(AstNode* node, AstNode* params, AstNode* args)
{
    if(!node) return NULL;

    // 如果是变量引用，且变量名是宏参数，则替换为实际参数的深拷贝
    if(node->type == AST_VAR && node->u.varname &&
       is_macro_param(params, node->u.varname)) {
        int idx = get_param_index(params, node->u.varname);
        AstNode* arg = get_arg_by_index(args, idx);
        if(arg) {
            return ast_clone_node(arg);
        }
        return ast_none();  // 参数缺失，返回 null
    }

    // 递归处理子节点
    switch(node->type) {
        case AST_SEQ:
            node->u.seq.first = substitute_params(node->u.seq.first, params, args);
            node->u.seq.second = substitute_params(node->u.seq.second, params, args);
            break;
        case AST_BINOP:
            node->u.bin.left = substitute_params(node->u.bin.left, params, args);
            node->u.bin.right = substitute_params(node->u.bin.right, params, args);
            break;
        case AST_UNARY:
            node->u.uny.child = substitute_params(node->u.uny.child, params, args);
            break;
        case AST_CALL:
            node->u.call.args = substitute_params(node->u.call.args, params, args);
            break;
        case AST_DYN_CALL:
            node->u.dyn_call.callee = substitute_params(node->u.dyn_call.callee, params, args);
            node->u.dyn_call.args = substitute_params(node->u.dyn_call.args, params, args);
            break;
        case AST_INDEX:
            node->u.index.arr = substitute_params(node->u.index.arr, params, args);
            node->u.index.idx = substitute_params(node->u.index.idx, params, args);
            break;
        case AST_ASSIGN:
            node->u.assign.expr = substitute_params(node->u.assign.expr, params, args);
            break;
        case AST_IF:
            node->u.ifnode.cond = substitute_params(node->u.ifnode.cond, params, args);
            node->u.ifnode.then_stmt = substitute_params(node->u.ifnode.then_stmt, params, args);
            node->u.ifnode.elif_chain = substitute_params(node->u.ifnode.elif_chain, params, args);
            node->u.ifnode.else_stmt = substitute_params(node->u.ifnode.else_stmt, params, args);
            break;
        case AST_WHILE:
            node->u.while_node.cond = substitute_params(node->u.while_node.cond, params, args);
            node->u.while_node.body = substitute_params(node->u.while_node.body, params, args);
            break;
        case AST_FOR:
            node->u.for_node.init = substitute_params(node->u.for_node.init, params, args);
            node->u.for_node.cond = substitute_params(node->u.for_node.cond, params, args);
            node->u.for_node.update = substitute_params(node->u.for_node.update, params, args);
            node->u.for_node.body = substitute_params(node->u.for_node.body, params, args);
            break;
        case AST_RETURN:
            node->u.ret.ret_val = substitute_params(node->u.ret.ret_val, params, args);
            break;
        case AST_PRINT:
            node->u.print.expr = substitute_params(node->u.print.expr, params, args);
            break;
        case AST_BLOCK:
            node->u.block.stmts = substitute_params(node->u.block.stmts, params, args);
            break;
        case AST_TERNARY:
            node->u.ternary.cond = substitute_params(node->u.ternary.cond, params, args);
            node->u.ternary.true_expr = substitute_params(node->u.ternary.true_expr, params, args);
            node->u.ternary.false_expr = substitute_params(node->u.ternary.false_expr, params, args);
            break;
        default:
            break;
    }

    return node;
}

AstNode* macro_expand(AstNode* macro_def, AstNode* args)
{
    if(!macro_def || macro_def->type != AST_MACRO_DEF) return NULL;

    // 防止无限递归
    if(g_expand_depth >= MACRO_EXPAND_DEPTH_MAX) {
        fprintf(stderr, "宏展开深度超过上限（%d），可能存在无限递归\n", MACRO_EXPAND_DEPTH_MAX);
        return ast_none();
    }

    g_expand_depth++;

    // 深拷贝宏体
    AstNode* body_copy = ast_clone_node(macro_def->u.macro_def.body);

    // 替换参数
    AstNode* expanded = substitute_params(body_copy,
                                            macro_def->u.macro_def.params,
                                            args);

    g_expand_depth--;

    // 释放临时的 macro_def 节点（由 macro_lookup 创建）
    // 注意：不要释放 params 和 body，它们属于全局宏表
    free(macro_def->u.macro_def.name);
    free(macro_def);

    // 如果宏体是 block_stmt，返回其中的语句列表（单语句直接返回，多语句返回 AST_SEQ）
    if(expanded && expanded->type == AST_BLOCK) {
        AstNode* stmts = expanded->u.block.stmts;
        // 释放 block 节点本身（但不释放语句列表）
        expanded->u.block.stmts = NULL;
        ast_free(expanded);
        return stmts ? stmts : ast_none();
    }

    return expanded;
}
