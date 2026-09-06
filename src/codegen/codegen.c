#include "codegen.h"
#include "ast/lumin_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// xxd嵌入的运行时资源，在main.c里面#include embed头文件；codegen这里extern声明
extern unsigned char src_runtime_full_runtime_full_h[];
extern unsigned int src_runtime_full_runtime_full_h_len;
extern unsigned char src_runtime_full_runtime_full_c[];
extern unsigned int src_runtime_full_runtime_full_c_len;

#define COLLECT_MAX 64

// 顶层变量（解释器的顶层栈帧变量 → C 文件级全局，函数与 main 共享）
typedef struct {
    const char* name;
} VarEntry;

// lumin 函数 → C 函数
typedef struct {
    const char* name;                 // lumin 函数名
    const char* params[COLLECT_MAX];  // 参数名（普通参数在前，可变参数最后）
    int param_cnt;                    // 普通参数个数（不含变参）
    int has_variadic;                 // 是否有 ...rest
    const char* locals[COLLECT_MAX];  // 函数局部变量（不在参数、不在全局）
    int local_cnt;
    AstNode* body;                    // 函数体 AST（AST_BLOCK）
} FuncInfo;

static VarEntry var_list[COLLECT_MAX];
static int var_count;
static FuncInfo func_list[COLLECT_MAX];
static int func_count;
static FuncInfo* g_cur_func;   // 生成语句时的当前函数（NULL=main）
static FILE* out_fp;

// break 上下文栈：区分 switch（__sw_break=1）与循环（C break）
typedef enum { BRK_LOOP, BRK_SWITCH } BrkKind;
static BrkKind brk_stack[COLLECT_MAX];
static int brk_depth;

static void collect_globals(AstNode* node);
static void collect_func_locals(FuncInfo* fn);
static void collect_locals_walk(FuncInfo* fn, AstNode* node);
static void gen_expr(AstNode* node);
static void gen_expr_fp(AstNode* node, FILE* out);
static void gen_stmt(AstNode* node);
static void gen_stmt_no_semi(AstNode* node);
static void emit_var(FILE* out, const char* name);
static void emit_func_proto(const FuncInfo* fn);
static void emit_func_def(const FuncInfo* fn);

// ---------------- 小工具 ----------------

static void emit_c_string_lit(FILE* f, const char* s) {
    fputc('"', f);
    for (const char* p = s; *p; ++p) {
        if (*p == '"')
            fputs("\\\"", f);
        else
            fputc(*p, f);
    }
    fputc('"', f);
}

static int var_exists(const char* name) {
    for (int i = 0; i < var_count; ++i) {
        if (strcmp(var_list[i].name, name) == 0)
            return 1;
    }
    return 0;
}

static void add_var_if_absent(const char* name) {
    if (!name) return;
    if (var_exists(name)) return;
    if (var_count < COLLECT_MAX) {
        var_list[var_count].name = name;
        var_count++;
    }
}

static FuncInfo* find_func(const char* name) {
    for (int i = 0; i < func_count; ++i) {
        if (strcmp(func_list[i].name, name) == 0)
            return &func_list[i];
    }
    return NULL;
}

static int func_has_param(const FuncInfo* fn, const char* name) {
    if (!fn || !name) return 0;
    int total = fn->param_cnt + (fn->has_variadic ? 1 : 0);
    for (int i = 0; i < total; ++i) {
        if (strcmp(fn->params[i], name) == 0)
            return 1;
    }
    return 0;
}

static int func_has_local(const FuncInfo* fn, const char* name) {
    if (!fn || !name) return 0;
    for (int i = 0; i < fn->local_cnt; ++i) {
        if (strcmp(fn->locals[i], name) == 0)
            return 1;
    }
    return 0;
}

static void add_local_if_absent(FuncInfo* fn, const char* name) {
    if (!name) return;
    if (func_has_param(fn, name)) return;   // 参数优先
    if (var_exists(name)) return;           // 全局优先（沿链更新全局）
    if (func_has_local(fn, name)) return;
    if (fn->local_cnt < COLLECT_MAX) {
        fn->locals[fn->local_cnt++] = name;
    }
}

static void brk_push(BrkKind k) {
    if (brk_depth < COLLECT_MAX) brk_stack[brk_depth++] = k;
}
static void brk_pop(void) {
    if (brk_depth > 0) brk_depth--;
}

// 实参链表是左嵌套 AST_SEQ 链（ast_arg_append 复用 ast_seq）
typedef void (*ArgWalkFn)(AstNode* arg, void* ud);

static void walk_call_args(AstNode* args, ArgWalkFn fn, void* ud) {
    if (!args) return;
    if (args->type != AST_SEQ) { fn(args, ud); return; }
    walk_call_args(args->u.seq.first, fn, ud);
    walk_call_args(args->u.seq.second, fn, ud);
}

static void count_arg_cb(AstNode* arg, void* ud) {
    (void)arg;
    (*(int*)ud)++;
}

static void collect_arg_cb(AstNode* arg, void* ud) {
    (void)ud;
    collect_globals(arg);
}

static void collect_local_arg_cb(AstNode* arg, void* ud) {
    collect_locals_walk((FuncInfo*)ud, arg);
}

typedef struct {
    FILE* out;
    int idx;
} ArgEmitCtx;

static void emit_arg_assign_cb(AstNode* arg, void* ud) {
    ArgEmitCtx* ac = (ArgEmitCtx*)ud;
    fprintf(ac->out, "__a%d = ", ac->idx);
    gen_expr_fp(arg, ac->out);
    fprintf(ac->out, "; ");
    ac->idx++;
}

// ---------------- 变量收集 ----------------

// 顶层遍历：收集全局变量 + 函数表（函数体由 collect_func_locals 单独处理）
static void collect_globals(AstNode* node) {
    if (!node) return;
    switch (node->type) {
        case AST_VAR:
            add_var_if_absent(node->u.varname);
            break;
        case AST_ASSIGN:
            add_var_if_absent(node->u.assign.varname);
            collect_globals(node->u.assign.expr);
            break;
        case AST_BINOP:
            collect_globals(node->u.bin.left);
            collect_globals(node->u.bin.right);
            break;
        case AST_UNARY:
            collect_globals(node->u.uny.child);
            break;
        case AST_CAST:
            collect_globals(node->u.cast.child);
            break;
        case AST_TERNARY:
            collect_globals(node->u.ternary.cond);
            collect_globals(node->u.ternary.true_expr);
            collect_globals(node->u.ternary.false_expr);
            break;
        case AST_PRINT:
            collect_globals(node->u.print.expr);
            break;
        case AST_SEQ:
            collect_globals(node->u.seq.first);
            collect_globals(node->u.seq.second);
            break;
        case AST_BLOCK:
            collect_globals(node->u.block.stmts);
            break;
        case AST_IF:
            collect_globals(node->u.ifnode.cond);
            collect_globals(node->u.ifnode.then_stmt);
            collect_globals(node->u.ifnode.elif_chain);
            collect_globals(node->u.ifnode.else_stmt);
            break;
        case AST_IF_CHAIN:
            collect_globals(node->u.if_chain.cond);
            collect_globals(node->u.if_chain.if_body);
            {
                AstNode* p = node->u.if_chain.elif_list;
                while (p) {
                    collect_globals(p->u.elif.cond);
                    collect_globals(p->u.elif.body);
                    p = p->u.elif.next;
                }
            }
            collect_globals(node->u.if_chain.else_body);
            break;
        case AST_ELIF:
            collect_globals(node->u.elif.cond);
            collect_globals(node->u.elif.body);
            break;
        case AST_FOR:
            collect_globals(node->u.for_node.init);
            collect_globals(node->u.for_node.cond);
            collect_globals(node->u.for_node.update);
            collect_globals(node->u.for_node.body);
            break;
        case AST_WHILE:
            collect_globals(node->u.while_node.cond);
            collect_globals(node->u.while_node.body);
            break;
        case AST_SWITCH:
            collect_globals(node->u.sw.cond);
            {
                AstNode* cp = node->u.sw.cases;
                while (cp) {
                    collect_globals(cp->u.cs.const_val);
                    collect_globals(cp->u.cs.body);
                    cp = cp->u.cs.next;
                }
            }
            break;
        case AST_CASE:
            collect_globals(node->u.cs.const_val);
            collect_globals(node->u.cs.body);
            break;
        case AST_RETURN:
            collect_globals(node->u.ret.ret_val);
            break;
        case AST_CALL:
            walk_call_args(node->u.call.args, collect_arg_cb, NULL);
            break;
        case AST_FUNC_DEF: {
            // 记录函数（函数体变量不进入全局列表）
            if (func_count >= COLLECT_MAX) {
                fprintf(stderr, "codegen: 函数数量超限\n");
                exit(EXIT_FAILURE);
            }
            FuncInfo* fn = &func_list[func_count++];
            memset(fn, 0, sizeof(FuncInfo));
            fn->name = node->u.func_def.name;
            fn->body = node->u.func_def.body;
            int idx = 0;
            AstNode* p = node->u.func_def.params;
            while (p) {
                if (p->u.param.is_ellipsis) fn->has_variadic = 1;
                if (idx < COLLECT_MAX) fn->params[idx++] = p->u.param.name;
                p = p->u.param.next;
            }
            fn->param_cnt = idx - (fn->has_variadic ? 1 : 0);
            break;
        }
        case AST_BREAK:
        case AST_CONTINUE:
            break;
        default:
            break;
    }
}

// 函数体内遍历：收集局部变量（不在参数、不在全局的名字）
static void collect_locals_walk(FuncInfo* fn, AstNode* node) {
    if (!node) return;
    switch (node->type) {
        case AST_VAR:
            add_local_if_absent(fn, node->u.varname);
            break;
        case AST_ASSIGN:
            add_local_if_absent(fn, node->u.assign.varname);
            collect_locals_walk(fn, node->u.assign.expr);
            break;
        case AST_BINOP:
            collect_locals_walk(fn, node->u.bin.left);
            collect_locals_walk(fn, node->u.bin.right);
            break;
        case AST_UNARY:
            collect_locals_walk(fn, node->u.uny.child);
            break;
        case AST_CAST:
            collect_locals_walk(fn, node->u.cast.child);
            break;
        case AST_TERNARY:
            collect_locals_walk(fn, node->u.ternary.cond);
            collect_locals_walk(fn, node->u.ternary.true_expr);
            collect_locals_walk(fn, node->u.ternary.false_expr);
            break;
        case AST_PRINT:
            collect_locals_walk(fn, node->u.print.expr);
            break;
        case AST_SEQ:
            collect_locals_walk(fn, node->u.seq.first);
            collect_locals_walk(fn, node->u.seq.second);
            break;
        case AST_BLOCK:
            collect_locals_walk(fn, node->u.block.stmts);
            break;
        case AST_IF:
            collect_locals_walk(fn, node->u.ifnode.cond);
            collect_locals_walk(fn, node->u.ifnode.then_stmt);
            collect_locals_walk(fn, node->u.ifnode.elif_chain);
            collect_locals_walk(fn, node->u.ifnode.else_stmt);
            break;
        case AST_IF_CHAIN:
            collect_locals_walk(fn, node->u.if_chain.cond);
            collect_locals_walk(fn, node->u.if_chain.if_body);
            {
                AstNode* p = node->u.if_chain.elif_list;
                while (p) {
                    collect_locals_walk(fn, p->u.elif.cond);
                    collect_locals_walk(fn, p->u.elif.body);
                    p = p->u.elif.next;
                }
            }
            collect_locals_walk(fn, node->u.if_chain.else_body);
            break;
        case AST_ELIF:
            collect_locals_walk(fn, node->u.elif.cond);
            collect_locals_walk(fn, node->u.elif.body);
            break;
        case AST_FOR:
            collect_locals_walk(fn, node->u.for_node.init);
            collect_locals_walk(fn, node->u.for_node.cond);
            collect_locals_walk(fn, node->u.for_node.update);
            collect_locals_walk(fn, node->u.for_node.body);
            break;
        case AST_WHILE:
            collect_locals_walk(fn, node->u.while_node.cond);
            collect_locals_walk(fn, node->u.while_node.body);
            break;
        case AST_SWITCH:
            collect_locals_walk(fn, node->u.sw.cond);
            {
                AstNode* cp = node->u.sw.cases;
                while (cp) {
                    collect_locals_walk(fn, cp->u.cs.const_val);
                    collect_locals_walk(fn, cp->u.cs.body);
                    cp = cp->u.cs.next;
                }
            }
            break;
        case AST_CASE:
            collect_locals_walk(fn, node->u.cs.const_val);
            collect_locals_walk(fn, node->u.cs.body);
            break;
        case AST_RETURN:
            collect_locals_walk(fn, node->u.ret.ret_val);
            break;
        case AST_CALL:
            walk_call_args(node->u.call.args, collect_local_arg_cb, fn);
            break;
        case AST_FUNC_DEF:
            // 嵌套函数定义暂不支持（不收集、不生成）
            break;
        case AST_BREAK:
        case AST_CONTINUE:
            break;
        default:
            break;
    }
}

static void collect_func_locals(FuncInfo* fn) {
    collect_locals_walk(fn, fn->body);
}

// ---------------- 表达式生成 ----------------

// 变量名 → C 标识符：参数/局部 → lmloc_ 前缀；全局 → lmvar_ 前缀
static void emit_var(FILE* out, const char* name) {
    if (g_cur_func && (func_has_param(g_cur_func, name) || func_has_local(g_cur_func, name))) {
        fprintf(out, "lmloc_%s", name);
    } else {
        fprintf(out, "lmvar_%s", name);
    }
}

static void gen_expr(AstNode* node) {
    gen_expr_fp(node, out_fp);
}

static void gen_expr_fp(AstNode* node, FILE* out) {
    if (!node) return;
    switch (node->type) {
        case AST_INT:
            fprintf(out, "lumin_make_int(%lldLL)", node->u.inum);
            break;
        case AST_NUM:
            fprintf(out, "lumin_make_double(%g)", node->u.num);
            break;
        case AST_BOOL:
            fprintf(out, "lumin_make_bool(%d)", node->u.bval ? 1 : 0);
            break;
        case AST_CHAR:
            fprintf(out, "lumin_make_char('%c')", node->u.ch);
            break;
        case AST_STRING:
            fprintf(out, "lumin_make_string(");
            emit_c_string_lit(out, node->u.sval);
            fprintf(out, ")");
            break;
        case AST_VAR:
            emit_var(out, node->u.varname);
            break;
        case AST_UNARY: {
            AstNode* child = node->u.uny.child;
            BinOp op = node->u.uny.op;
            switch (op) {
                case OP_POST_INC:
                    fprintf(out, "lumin_post_inc(&");
                    emit_var(out, child->u.varname);
                    fprintf(out, ")");
                    break;
                case OP_PRE_INC:
                    fprintf(out, "lumin_pre_inc(&");
                    emit_var(out, child->u.varname);
                    fprintf(out, ")");
                    break;
                case OP_POST_DEC:
                    fprintf(out, "lumin_post_dec(&");
                    emit_var(out, child->u.varname);
                    fprintf(out, ")");
                    break;
                case OP_PRE_DEC:
                    fprintf(out, "lumin_pre_dec(&");
                    emit_var(out, child->u.varname);
                    fprintf(out, ")");
                    break;
                case OP_UNARY_PLUS:
                    fprintf(out, "lumin_unary_plus(");
                    gen_expr_fp(child, out);
                    fprintf(out, ")");
                    break;
                case OP_UNARY_MINUS:
                    fprintf(out, "lumin_unary_minus(");
                    gen_expr_fp(child, out);
                    fprintf(out, ")");
                    break;
                default:
                    fprintf(stderr, "codegen:未知一元op %d\n", op);
                    break;
            }
            break;
        }
        case AST_BINOP: {
            const char* fn = NULL;
            switch (node->u.bin.op) {
                case OP_ADD: fn = "lumin_add"; break;
                case OP_SUB: fn = "lumin_sub"; break;
                case OP_MUL: fn = "lumin_mul"; break;
                case OP_DIV: fn = "lumin_div"; break;
                case OP_GT:  fn = "lumin_gt"; break;
                case OP_LT:  fn = "lumin_lt"; break;
                case OP_GE:  fn = "lumin_ge"; break;
                case OP_LE:  fn = "lumin_le"; break;
                case OP_EQ:  fn = "lumin_eq"; break;
                case OP_NE:  fn = "lumin_ne"; break;
                default: break;
            }
            fprintf(out, "%s(", fn);
            gen_expr_fp(node->u.bin.left, out);
            fprintf(out, ",");
            gen_expr_fp(node->u.bin.right, out);
            fprintf(out, ")");
            break;
        }
        case AST_CAST: {
            switch (node->u.cast.cast_type) {
                case CAST_INT:      fprintf(out, "lumin_cast_int("); break;
                case CAST_DOUBLE:   fprintf(out, "lumin_cast_double("); break;
                case CAST_BOOL:     fprintf(out, "lumin_cast_bool("); break;
                case CAST_STRING:   fprintf(out, "lumin_cast_string("); break;
                case CAST_ASCII:    fprintf(out, "lumin_cast_ascii("); break;
                case CAST_CHAR:     fprintf(out, "lumin_cast_char("); break;
                default:            fprintf(out, "lumin_cast_int("); break;
            }
            gen_expr_fp(node->u.cast.child, out);
            fprintf(out, ")");
            break;
        }
        case AST_TERNARY: {
            fprintf(out, "(lumin_to_bool(");
            gen_expr_fp(node->u.ternary.cond, out);
            fprintf(out, ")) ? (");
            gen_expr_fp(node->u.ternary.true_expr, out);
            fprintf(out, ") : (");
            gen_expr_fp(node->u.ternary.false_expr, out);
            fprintf(out, ")");
            break;
        }
        // 函数调用：GNU语句表达式，实参从左到右求值后调用（保证与解释器一致）
        case AST_CALL: {
            FuncInfo* fn = find_func(node->u.call.name);
            if (!fn) {
                if (var_exists(node->u.call.name)) {
                    // 函数值变量调用：解释器支持，编译后端暂未实现（高阶函数范畴）
                    fprintf(stderr, "codegen: 暂不支持通过变量调用函数: %s（函数值/高阶函数特性未实现）\n", node->u.call.name);
                } else {
                    fprintf(stderr, "codegen: 未定义函数: %s\n", node->u.call.name);
                }
                exit(EXIT_FAILURE);
            }
            int argc = 0;
            walk_call_args(node->u.call.args, count_arg_cb, &argc);
            int fixed = fn->param_cnt;
            int nbind = (argc < fixed) ? argc : fixed;
            int rest_n = argc - fixed;
            if (rest_n < 0) rest_n = 0;

            fprintf(out, "({ ");
            // 1) 声明实参临时变量
            for (int i = 0; i < argc; i++) {
                fprintf(out, "Value __a%d; ", i);
            }
            // 2) 从左到右求值
            ArgEmitCtx ac = { out, 0 };
            walk_call_args(node->u.call.args, emit_arg_assign_cb, &ac);
            // 3) 可变参数打包成数组（与解释器一致）
            if (fn->has_variadic) {
                fprintf(out, "Value __rest = val_array(%d); ", rest_n);
                for (int i = 0; i < rest_n; i++) {
                    fprintf(out, "__rest.v.array.items[%d] = __a%d; ", i, fixed + i);
                }
            }
            // 4) 调用（缺省实参补 nil）
            fprintf(out, "lumin_func_%s(", node->u.call.name);
            for (int i = 0; i < fixed; i++) {
                if (i) fprintf(out, ", ");
                if (i < nbind) fprintf(out, "__a%d", i);
                else fprintf(out, "val_none()");
            }
            if (fn->has_variadic) {
                if (fixed > 0) fprintf(out, ", ");
                fprintf(out, "__rest");
            }
            fprintf(out, "); })");
            break;
        }
        default:
            break;
    }
}

// ---------------- 语句生成 ----------------

static void gen_stmt_no_semi(AstNode* node) {
    if (!node) return;
    switch (node->type) {
        case AST_ASSIGN:
            emit_var(out_fp, node->u.assign.varname);
            fprintf(out_fp, " = ");
            gen_expr(node->u.assign.expr);
            break;
        case AST_SEQ:
            gen_stmt_no_semi(node->u.seq.first);
            fprintf(out_fp, ",");
            gen_stmt_no_semi(node->u.seq.second);
            break;
        default:
            gen_expr(node);
            break;
    }
}

static void gen_stmt(AstNode* node) {
    if (!node) return;
    switch (node->type) {
        case AST_UNARY: {
            AstNode* child = node->u.uny.child;
            emit_var(out_fp, child->u.varname);
            fprintf(out_fp, " = ");
            gen_expr(node);
            fprintf(out_fp, ";\n");
            break;
        }
        case AST_SEQ:
            gen_stmt(node->u.seq.first);
            gen_stmt(node->u.seq.second);
            break;
        case AST_ASSIGN:
            emit_var(out_fp, node->u.assign.varname);
            fprintf(out_fp, " = ");
            gen_expr(node->u.assign.expr);
            fprintf(out_fp, ";\n");
            break;
        case AST_PRINT: {
            AstNode* e = node->u.print.expr;
            fprintf(out_fp, "lumin_print(");
            gen_expr(e);
            fprintf(out_fp, ");\n");
            break;
        }
        case AST_BLOCK:
            fprintf(out_fp, "{\n");
            if (node->u.block.stmts != NULL) {
                gen_stmt(node->u.block.stmts);
            }
            fprintf(out_fp, "}\n");
            break;
        case AST_IF_CHAIN: {
            fprintf(out_fp, "if(lumin_to_bool(");
            gen_expr(node->u.if_chain.cond);
            fprintf(out_fp, "))\n");
            gen_stmt(node->u.if_chain.if_body);

            AstNode* p = node->u.if_chain.elif_list;
            while (p != NULL) {
                fprintf(out_fp, "else if(lumin_to_bool(");
                gen_expr(p->u.elif.cond);
                fprintf(out_fp, "))\n");
                gen_stmt(p->u.elif.body);
                p = p->u.elif.next;
            }
            if (node->u.if_chain.else_body != NULL) {
                fprintf(out_fp, "else\n");
                gen_stmt(node->u.if_chain.else_body);
            }
            break;
        }
        case AST_IF: {
            fprintf(out_fp, "if(lumin_to_bool(");
            gen_expr(node->u.ifnode.cond);
            fprintf(out_fp, ")){\n");
            gen_stmt(node->u.ifnode.then_stmt);
            fprintf(out_fp, "}\n");

            AstNode* elif = node->u.ifnode.elif_chain;
            while (elif != NULL) {
                fprintf(out_fp, "else if(lumin_to_bool(");
                gen_expr(elif->u.ifnode.cond);
                fprintf(out_fp, ")){\n");
                gen_stmt(elif->u.ifnode.then_stmt);
                fprintf(out_fp, "}\n");
                elif = elif->u.ifnode.elif_chain;
            }
            if (node->u.ifnode.else_stmt != NULL) {
                fprintf(out_fp, "else{\n");
                gen_stmt(node->u.ifnode.else_stmt);
                fprintf(out_fp, "}\n");
            }
            break;
        }
        case AST_WHILE: {
            brk_push(BRK_LOOP);
            fprintf(out_fp, "while(lumin_to_bool(");
            gen_expr(node->u.while_node.cond);
            fprintf(out_fp, ")){\n");
            gen_stmt(node->u.while_node.body);
            fprintf(out_fp, "}\n");
            brk_pop();
            break;
        }
        case AST_FOR: {
            brk_push(BRK_LOOP);
            fprintf(out_fp, "for(");
            if (node->u.for_node.init) {
                gen_stmt_no_semi(node->u.for_node.init);
            }
            fprintf(out_fp, ";");
            if (node->u.for_node.cond) {
                fprintf(out_fp, "lumin_to_bool(");
                gen_expr(node->u.for_node.cond);
                fprintf(out_fp, ")");
            }
            fprintf(out_fp, ";");
            if (node->u.for_node.update) {
                gen_stmt_no_semi(node->u.for_node.update);
            }
            fprintf(out_fp, "){\n");
            gen_stmt(node->u.for_node.body);
            fprintf(out_fp, "}\n");
            brk_pop();
            break;
        }
        case AST_SWITCH: {
            brk_push(BRK_SWITCH);
            fprintf(out_fp, "{\n");
            fprintf(out_fp, "Value __sw_val = ");
            gen_expr(node->u.sw.cond);
            fprintf(out_fp, ";\n");
            fprintf(out_fp, "int __sw_break = 0;\n");

            AstNode* cp = node->u.sw.cases;
            for (AstNode* p = cp; p != NULL; p = p->u.cs.next) {
                if (p->u.cs.is_default) {
                    fprintf(out_fp, "if (!__sw_break) {\n");
                } else {
                    fprintf(out_fp, "if (!__sw_break && lumin_to_bool(lumin_eq(__sw_val, ");
                    gen_expr(p->u.cs.const_val);
                    fprintf(out_fp, "))) {\n");
                }
                gen_stmt(p->u.cs.body);
                // 执行完case体，置break标记，模拟switch break语义
                fprintf(out_fp, "__sw_break = 1;\n");
                fprintf(out_fp, "}\n");
            }
            fprintf(out_fp, "}\n");
            brk_pop();
            break;
        }

        case AST_RETURN: {
            if (g_cur_func != NULL) {
                // 函数内：返回表达式值；无表达式 → nil
                fprintf(out_fp, "return ");
                if (node->u.ret.ret_val) {
                    gen_expr(node->u.ret.ret_val);
                } else {
                    fprintf(out_fp, "val_none()");
                }
                fprintf(out_fp, ";\n");
            } else {
                // 顶层：值丢弃，终止 main（与解释器"return 结束整个程序"一致）
                if (node->u.ret.ret_val != NULL) {
                    fputs("(void)(", out_fp);
                    gen_expr(node->u.ret.ret_val);
                    fputs(");\n", out_fp);
                }
                fputs("return 0;\n", out_fp);
            }
            break;
        }
        case AST_BREAK:
            if (brk_depth > 0 && brk_stack[brk_depth - 1] == BRK_SWITCH) {
                fprintf(out_fp, "__sw_break = 1;\n");
            } else {
                fprintf(out_fp, "break;\n");
            }
            break;
        case AST_CONTINUE:
            fprintf(out_fp, "continue;\n");
            break;
        case AST_CALL:
            gen_expr(node);
            fprintf(out_fp, ";\n");
            break;
        case AST_FUNC_DEF:
            // 函数定义已输出到 main 之前，main 内无需执行
            fprintf(out_fp, "/* func %s: 定义见上方 */\n", node->u.func_def.name);
            break;
        default:
            break;
    }
}

// ---------------- 函数输出 ----------------

static void emit_func_proto(const FuncInfo* fn) {
    fprintf(out_fp, "static Value lumin_func_%s(", fn->name);
    int total = fn->param_cnt + (fn->has_variadic ? 1 : 0);
    for (int i = 0; i < total; i++) {
        if (i) fprintf(out_fp, ", ");
        fprintf(out_fp, "Value lmloc_%s", fn->params[i]);
    }
    fprintf(out_fp, ");\n");
}

static void emit_func_def(const FuncInfo* fn) {
    fprintf(out_fp, "static Value lumin_func_%s(", fn->name);
    int total = fn->param_cnt + (fn->has_variadic ? 1 : 0);
    for (int i = 0; i < total; i++) {
        if (i) fprintf(out_fp, ", ");
        fprintf(out_fp, "Value lmloc_%s", fn->params[i]);
    }
    fprintf(out_fp, ")\n{\n");
    // 局部变量声明（解释器新建栈帧局部变量的对应物）
    // 初始化为 val_none()：条件分支内才赋值的变量，未执行分支时读取为 nil（可预测，而非未初始化垃圾值）
    for (int i = 0; i < fn->local_cnt; i++) {
        fprintf(out_fp, "    Value lmloc_%s = val_none();\n", fn->locals[i]);
    }
    g_cur_func = (FuncInfo*)fn;
    gen_stmt(fn->body);
    g_cur_func = NULL;
    // 无显式 return → 默认返回 nil（与解释器一致）
    fprintf(out_fp, "    return val_none();\n");
    fprintf(out_fp, "}\n\n");
}

// ---------------- 入口 ----------------

void codegen_generate(AstNode* root, const char* out_c_path) {
    var_count = 0;
    func_count = 0;
    g_cur_func = NULL;
    brk_depth = 0;
    memset(var_list, 0, sizeof(var_list));
    memset(func_list, 0, sizeof(func_list));

    // 1. 收集：全局变量 + 函数表（先全局，后函数局部）
    collect_globals(root);
    for (int i = 0; i < func_count; i++) {
        collect_func_locals(&func_list[i]);
    }

    out_fp = fopen(out_c_path, "w");
    if (!out_fp) {
        perror("open output c file failed");
        return;
    }

    // 写入runtime.h（包含Value结构体、运行时函数声明）
    fwrite(src_runtime_full_runtime_full_h, 1, src_runtime_full_runtime_full_h_len, out_fp);
    fputs("\n\n", out_fp);

    fprintf(out_fp, "#include <stdio.h>\n");
    fprintf(out_fp, "#include <stdlib.h>\n");
    fprintf(out_fp, "#include <string.h>\n\n");

    // 追加runtime.c实现
    fwrite(src_runtime_full_runtime_full_c, 1, src_runtime_full_runtime_full_c_len, out_fp);

    // 2. 全局变量声明（文件级：函数与 main 共享，对应解释器顶层栈帧）
    for (int i = 0; i < var_count; ++i) {
        fprintf(out_fp, "static Value lmvar_%s = {0};\n", var_list[i].name);
    }
    fprintf(out_fp, "\n");

    // 3. 函数前向声明（支持前向引用/递归）
    for (int i = 0; i < func_count; i++) {
        emit_func_proto(&func_list[i]);
    }
    fprintf(out_fp, "\n");

    // 4. 函数定义
    for (int i = 0; i < func_count; i++) {
        emit_func_def(&func_list[i]);
    }

    // 5. main
    fprintf(out_fp, "int main(void){\n");
    gen_stmt(root);
    fprintf(out_fp, "return 0;\n");
    fprintf(out_fp, "}\n\n");

    fclose(out_fp);
}
