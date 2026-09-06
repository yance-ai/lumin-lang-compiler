// AST → 字节码 IR 编译器
// 遍历结构与 ast_typecheck.c / codegen.c 对齐（用户建议复用其递归结构）。
#include "ir_compile.h"
#include "ast/lumin_types.h"
#include "runtime/lm_value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------- 全局函数表 ----------------
// yacc 期注册每个函数（ir_compile_function），main.c 注册 main（ir_compile_main）。
#define IR_FUNC_TABLE_MAX 64
static BytecodeFunc* ir_func_table[IR_FUNC_TABLE_MAX];
static int ir_func_count = 0;

void ir_func_table_reset(void)
{
    ir_func_count = 0;
}

int ir_func_table_count(void) { return ir_func_count; }

BytecodeFunc* ir_func_table_get(int i)
{
    return (i >= 0 && i < ir_func_count) ? ir_func_table[i] : NULL;
}

BytecodeFunc* ir_func_table_lookup(const char* name)
{
    if(!name) return NULL;
    for(int i = 0; i < ir_func_count; i++) {
        if(ir_func_table[i]->name && strcmp(ir_func_table[i]->name, name) == 0)
            return ir_func_table[i];
    }
    return NULL;
}

static void ir_func_table_add(BytecodeFunc* fn)
{
    if(ir_func_count >= IR_FUNC_TABLE_MAX) {
        fprintf(stderr, "IR: 函数数量超限\n");
        exit(EXIT_FAILURE);
    }
    ir_func_table[ir_func_count++] = fn;
}

// ---------------- 编译上下文 ----------------

typedef struct {
    int kind;              // 0=循环 1=switch
    int* brk; int brk_cnt, brk_cap;    // 未定 break 跳转位置
    int* cont; int cont_cnt, cont_cap; // 未定 continue 跳转位置（循环）
    int cont_target;       // 已知 continue 目标（while 的 cond 开头）或 -1
} Layer;

typedef struct {
    BytecodeFunc* fn;
    Layer layers[32];
    int layer_depth;
} Ctx;

static int here(Ctx* c) { return c->fn->code_len; }

static void emit(Ctx* c, OpCode op, int a, int b) { bf_emit(c->fn, op, a, b); }

static int emit_here(Ctx* c, OpCode op, int a, int b) { return bf_emit_here(c->fn, op, a, b); }

static void patch_to(Ctx* c, int pos) { bf_patch(c->fn, pos, here(c)); }

// ---------------- 控制层（break/continue） ----------------

static void layer_push(Ctx* c, int kind, int cont_target)
{
    if(c->layer_depth >= 32) { fprintf(stderr, "IR: 控制层过深\n"); exit(EXIT_FAILURE); }
    Layer* l = &c->layers[c->layer_depth++];
    memset(l, 0, sizeof(Layer));
    l->kind = kind;
    l->cont_target = cont_target;
}

static Layer layer_pop(Ctx* c)
{
    return c->layers[--c->layer_depth];
}

static void layer_brk_add(Ctx* c, int pos)
{
    Layer* l = &c->layers[c->layer_depth - 1];
    if(l->brk_cnt >= l->brk_cap) {
        l->brk_cap = l->brk_cap ? l->brk_cap * 2 : 4;
        l->brk = (int*)realloc(l->brk, sizeof(int) * l->brk_cap);
    }
    l->brk[l->brk_cnt++] = pos;
}

static void layer_cont_add(Ctx* c, int pos)
{
    for(int i = c->layer_depth - 1; i >= 0; i--) {
        if(c->layers[i].kind == 0) {
            if(c->layers[i].cont_target >= 0) {
                bf_patch(c->fn, pos, c->layers[i].cont_target);
            } else {
                Layer* l = &c->layers[i];
                if(l->cont_cnt >= l->cont_cap) {
                    l->cont_cap = l->cont_cap ? l->cont_cap * 2 : 4;
                    l->cont = (int*)realloc(l->cont, sizeof(int) * l->cont_cap);
                }
                l->cont[l->cont_cnt++] = pos;
            }
            return;
        }
    }
    fprintf(stderr, "IR: continue 不在循环内\n");
    exit(EXIT_FAILURE);
}

// ---------------- 表达式 / 语句编译 ----------------

static void c_stmt(Ctx* c, AstNode* node);
static void c_expr(Ctx* c, AstNode* node);

static void c_args(Ctx* c, AstNode* args, int* argc)
{
    if(!args) return;
    if(args->type != AST_SEQ) {
        c_expr(c, args);
        (*argc)++;
        return;
    }
    c_args(c, args->u.seq.first, argc);
    c_args(c, args->u.seq.second, argc);
}

static void c_expr(Ctx* c, AstNode* node)
{
    if(!node) { emit(c, OPC_LOAD_CONST, bf_const(c->fn, val_none()), 0); return; }
    switch(node->type) {
        case AST_INT:
            emit(c, OPC_LOAD_CONST, bf_const(c->fn, lumin_make_int(node->u.inum)), 0);
            break;
        case AST_NUM:
            emit(c, OPC_LOAD_CONST, bf_const(c->fn, lumin_make_double(node->u.num)), 0);
            break;
        case AST_BOOL:
            emit(c, OPC_LOAD_CONST, bf_const(c->fn, lumin_make_bool(node->u.bval ? 1 : 0)), 0);
            break;
        case AST_CHAR:
            emit(c, OPC_LOAD_CONST, bf_const(c->fn, lumin_make_char(node->u.ch)), 0);
            break;
        case AST_STRING:
            emit(c, OPC_LOAD_CONST, bf_const(c->fn, lumin_make_string(node->u.sval)), 0);
            break;
        case AST_VAR:
            emit(c, OPC_LOAD_VAR, bf_sym(c->fn, node->u.varname), 0);
            break;
        case AST_ASSIGN:
            c_expr(c, node->u.assign.expr);
            emit(c, OPC_STORE_VAR, bf_sym(c->fn, node->u.assign.varname), 0);
            break;
        case AST_BINOP: {
            c_expr(c, node->u.bin.left);
            c_expr(c, node->u.bin.right);
            static const OpCode map[] = {
                [OP_ADD] = OPC_ADD, [OP_SUB] = OPC_SUB, [OP_MUL] = OPC_MUL, [OP_DIV] = OPC_DIV,
                [OP_GT] = OPC_GT, [OP_LT] = OPC_LT, [OP_GE] = OPC_GE, [OP_LE] = OPC_LE,
                [OP_EQ] = OPC_EQ, [OP_NE] = OPC_NE,
            };
            emit(c, map[node->u.bin.op], 0, 0);
            break;
        }
        case AST_UNARY: {
            AstNode* kid = node->u.uny.child;
            switch(node->u.uny.op) {
                case OP_PRE_INC:   emit(c, OPC_PRE_INC, bf_sym(c->fn, kid->u.varname), 0); break;
                case OP_POST_INC:  emit(c, OPC_POST_INC, bf_sym(c->fn, kid->u.varname), 0); break;
                case OP_PRE_DEC:   emit(c, OPC_PRE_DEC, bf_sym(c->fn, kid->u.varname), 0); break;
                case OP_POST_DEC:  emit(c, OPC_POST_DEC, bf_sym(c->fn, kid->u.varname), 0); break;
                case OP_UNARY_PLUS:  c_expr(c, kid); emit(c, OPC_POS, 0, 0); break;
                case OP_UNARY_MINUS: c_expr(c, kid); emit(c, OPC_NEG, 0, 0); break;
                default: break;
            }
            break;
        }
        case AST_CAST: {
            c_expr(c, node->u.cast.child);
            static const OpCode cmap[] = {
                [CAST_INT] = OPC_CAST_INT, [CAST_DOUBLE] = OPC_CAST_DOUBLE,
                [CAST_CHAR] = OPC_CAST_CHAR, [CAST_BOOL] = OPC_CAST_BOOL,
                [CAST_STRING] = OPC_CAST_STRING, [CAST_ASCII] = OPC_CAST_ASCII,
            };
            emit(c, cmap[node->u.cast.cast_type], 0, 0);
            break;
        }
        case AST_TERNARY: {
            c_expr(c, node->u.ternary.cond);
            int jf = emit_here(c, OPC_JMP_IF_FALSE, 0, 0);
            c_expr(c, node->u.ternary.true_expr);
            int je = emit_here(c, OPC_JMP, 0, 0);
            patch_to(c, jf);
            c_expr(c, node->u.ternary.false_expr);
            patch_to(c, je);
            break;
        }
        case AST_CALL: {
            int argc = 0;
            c_args(c, node->u.call.args, &argc);
            emit(c, OPC_CALL, bf_sym(c->fn, node->u.call.name), argc);
            break;
        }
        case AST_PRINT:
            c_expr(c, node->u.print.expr);
            emit(c, OPC_PRINT, 0, 0);
            break;
        case AST_SEQ:
            c_expr(c, node->u.seq.first);
            emit(c, OPC_POP, 0, 0);
            c_expr(c, node->u.seq.second);
            break;
        // 语句型节点出现在表达式位置（边缘）：求值后压 0（与解释器 make_int(0) 一致）
        case AST_IF:
        case AST_IF_CHAIN:
        case AST_BLOCK:
        case AST_WHILE:
        case AST_FOR:
        case AST_SWITCH:
            c_stmt(c, node);
            emit(c, OPC_LOAD_CONST, bf_const(c->fn, lumin_make_int(0)), 0);
            break;
        case AST_RETURN:
            if(node->u.ret.ret_val) c_expr(c, node->u.ret.ret_val);
            else emit(c, OPC_LOAD_CONST, bf_const(c->fn, val_none()), 0);
            break;
        default:
            emit(c, OPC_LOAD_CONST, bf_const(c->fn, val_none()), 0);
            break;
    }
}

static void c_stmt(Ctx* c, AstNode* node)
{
    if(!node) return;
    switch(node->type) {
        case AST_SEQ:
            c_stmt(c, node->u.seq.first);
            c_stmt(c, node->u.seq.second);
            break;
        case AST_BLOCK:
            c_stmt(c, node->u.block.stmts);
            break;
        // 表达式语句：求值后丢弃结果
        case AST_ASSIGN:
        case AST_BINOP:
        case AST_UNARY:
        case AST_CAST:
        case AST_TERNARY:
        case AST_CALL:
        case AST_VAR:
        case AST_INT:
        case AST_NUM:
        case AST_BOOL:
        case AST_CHAR:
        case AST_STRING:
            c_expr(c, node);
            emit(c, OPC_POP, 0, 0);
            break;
        case AST_PRINT:
            c_expr(c, node->u.print.expr);
            emit(c, OPC_PRINT, 0, 0);
            emit(c, OPC_POP, 0, 0);
            break;
        case AST_IF: {
            c_expr(c, node->u.ifnode.cond);
            int jf = emit_here(c, OPC_JMP_IF_FALSE, 0, 0);
            c_stmt(c, node->u.ifnode.then_stmt);
            if(node->u.ifnode.else_stmt || node->u.ifnode.elif_chain) {
                int je = emit_here(c, OPC_JMP, 0, 0);
                patch_to(c, jf);
                c_stmt(c, node->u.ifnode.elif_chain);
                c_stmt(c, node->u.ifnode.else_stmt);
                patch_to(c, je);
            } else {
                patch_to(c, jf);
            }
            break;
        }
        case AST_IF_CHAIN: {
            // if-elif-else 链：逐个条件判断
            int end_jumps[32]; int jump_cnt = 0;
            c_expr(c, node->u.if_chain.cond);
            int jf = emit_here(c, OPC_JMP_IF_FALSE, 0, 0);
            c_stmt(c, node->u.if_chain.if_body);
            if(jump_cnt < 32) end_jumps[jump_cnt++] = emit_here(c, OPC_JMP, 0, 0);
            patch_to(c, jf);

            AstNode* p = node->u.if_chain.elif_list;
            while(p) {
                c_expr(c, p->u.elif.cond);
                int jf2 = emit_here(c, OPC_JMP_IF_FALSE, 0, 0);
                c_stmt(c, p->u.elif.body);
                if(jump_cnt < 32) end_jumps[jump_cnt++] = emit_here(c, OPC_JMP, 0, 0);
                patch_to(c, jf2);
                p = p->u.elif.next;
            }
            if(node->u.if_chain.else_body) c_stmt(c, node->u.if_chain.else_body);
            for(int i = 0; i < jump_cnt; i++) patch_to(c, end_jumps[i]);
            break;
        }
        case AST_ELIF:
            // 仅由 IF_CHAIN 直接遍历，不独立出现
            break;
        case AST_WHILE: {
            int l_cond = here(c);
            c_expr(c, node->u.while_node.cond);
            int jf = emit_here(c, OPC_JMP_IF_FALSE, 0, 0);
            layer_push(c, 0, l_cond);          // continue → 回 cond
            c_stmt(c, node->u.while_node.body);
            Layer l = layer_pop(c);
            emit(c, OPC_JMP, l_cond, 0);
            int l_end = here(c);
            bf_patch(c->fn, jf, l_end);
            for(int i = 0; i < l.brk_cnt; i++) bf_patch(c->fn, l.brk[i], l_end);
            free(l.brk); free(l.cont);
            break;
        }
        case AST_FOR: {
            if(node->u.for_node.init) c_stmt(c, node->u.for_node.init);
            int l_cond = here(c);
            int jf = -1;
            if(node->u.for_node.cond) {
                c_expr(c, node->u.for_node.cond);
                jf = emit_here(c, OPC_JMP_IF_FALSE, 0, 0);
            }
            layer_push(c, 0, -1);              // continue 目标未知（update 后才知道）
            c_stmt(c, node->u.for_node.body);
            Layer l = layer_pop(c);
            int l_cont = here(c);              // continue 跳到这里（update 前）
            for(int i = 0; i < l.cont_cnt; i++) bf_patch(c->fn, l.cont[i], l_cont);
            if(node->u.for_node.update) c_stmt(c, node->u.for_node.update);
            emit(c, OPC_JMP, l_cond, 0);
            int l_end = here(c);
            if(jf >= 0) bf_patch(c->fn, jf, l_end);
            for(int i = 0; i < l.brk_cnt; i++) bf_patch(c->fn, l.brk[i], l_end);
            free(l.brk); free(l.cont);
            break;
        }
        case AST_SWITCH: {
            c_expr(c, node->u.sw.cond);        // 栈顶 sw_val（保留）
            layer_push(c, 1, -1);
            int end_jumps[64]; int jump_cnt = 0;
            AstNode* cp = node->u.sw.cases;
            int first_case = 1;
            int last_jf = -1;                  // 上一个非 default case 的未中跳转
            while(cp) {
                // 上一 case 未中 → 跳到当前 case 开头（patch 后重置，避免重复 patch 覆盖目标）
                if(!first_case) {
                    if(last_jf >= 0) patch_to(c, last_jf);
                    last_jf = -1;
                }
                if(cp->u.cs.is_default) {
                    emit(c, OPC_POP, 0, 0);      // 丢弃 sw_val
                    c_stmt(c, cp->u.cs.body);
                    if(jump_cnt < 64) end_jumps[jump_cnt++] = emit_here(c, OPC_JMP, 0, 0);
                } else {
                    emit(c, OPC_DUP, 0, 0);
                    c_expr(c, cp->u.cs.const_val);
                    emit(c, OPC_EQ, 0, 0);
                    last_jf = emit_here(c, OPC_JMP_IF_FALSE, 0, 0);
                    emit(c, OPC_POP, 0, 0);      // 命中：丢弃 sw_val
                    c_stmt(c, cp->u.cs.body);
                    if(jump_cnt < 64) end_jumps[jump_cnt++] = emit_here(c, OPC_JMP, 0, 0);
                }
                first_case = 0;
                cp = cp->u.cs.next;
            }
            // 最后一个 case 非 default 且未中 → 跳到循环后（丢弃 sw_val）
            if(last_jf >= 0) patch_to(c, last_jf);
            emit(c, OPC_POP, 0, 0);              // 未命中：丢弃 sw_val
            Layer l = layer_pop(c);
            int l_end = here(c);
            for(int i = 0; i < jump_cnt; i++) bf_patch(c->fn, end_jumps[i], l_end);
            for(int i = 0; i < l.brk_cnt; i++) bf_patch(c->fn, l.brk[i], l_end);
            free(l.brk); free(l.cont);
            break;
        }
        case AST_CASE:
            // 仅由 SWITCH 直接遍历，不独立出现
            break;
        case AST_BREAK: {
            if(c->layer_depth <= 0) {
                fprintf(stderr, "IR: break 不在循环/switch 内\n");
                exit(EXIT_FAILURE);
            }
            int pos = emit_here(c, OPC_JMP, 0, 0);
            layer_brk_add(c, pos);
            break;
        }
        case AST_CONTINUE: {
            int pos = emit_here(c, OPC_JMP, 0, 0);
            layer_cont_add(c, pos);
            break;
        }
        case AST_RETURN:
            if(node->u.ret.ret_val) {
                c_expr(c, node->u.ret.ret_val);
                emit(c, OPC_RETURN, 0, 0);
            } else {
                emit(c, OPC_RETURN_NIL, 0, 0);
            }
            break;
        case AST_FUNC_DEF:
            // 函数定义已由 yacc 期注册；顶层/函数体内的定义节点不产生指令
            break;
        default:
            break;
    }
}

// ---------------- 入口 ----------------

static void compile_params(BytecodeFunc* fn, AstNode* params)
{
    int idx = 0;
    AstNode* p = params;
    while(p) {
        if(p->u.param.is_ellipsis) fn->has_variadic = 1;
        fn->params = (char**)realloc(fn->params, sizeof(char*) * (idx + 1));
        fn->params[idx++] = strdup(p->u.param.name);
        p = p->u.param.next;
    }
    fn->param_cnt = idx - (fn->has_variadic ? 1 : 0);
}

BytecodeFunc* ir_compile_function(const char* name, AstNode* params, AstNode* body)
{
    BytecodeFunc* fn = bytecode_func_new(name, 0);
    compile_params(fn, params);

    Ctx c = { .fn = fn, .layer_depth = 0 };
    c_stmt(&c, body);
    emit(&c, OPC_RETURN_NIL, 0, 0);   // fallthrough 默认返回 nil

    ir_func_table_add(fn);
    return fn;
}

BytecodeFunc* ir_compile_main(AstNode* root)
{
    BytecodeFunc* fn = bytecode_func_new(NULL, 1);
    Ctx c = { .fn = fn, .layer_depth = 0 };
    c_stmt(&c, root);
    emit(&c, OPC_HALT, 0, 0);
    return fn;
}
