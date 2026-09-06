#include "ast_typecheck.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast_symtab.h"


// ---------------- 作用域快照 ----------------
// static_sym 是全局单表；检查函数体前保存、检查后恢复，
// 使函数参数/局部变量不泄漏到顶层，且参数能遮蔽同名全局。
static SymStaticEntry saved_table[STATIC_SYM_MAX];
static int saved_count;

static void sym_save(void) {
    saved_count = static_sym_count;
    memcpy(saved_table, static_sym_table, sizeof(saved_table));
}

static void sym_restore(void) {
    for (int i = saved_count; i < static_sym_count; ++i) {
        free(static_sym_table[i].name);
    }
    memcpy(static_sym_table, saved_table, sizeof(saved_table));
    static_sym_count = saved_count;
}

// 函数体递归深度：>0 表示正在检查某函数体，其内的嵌套 func 定义跳过
static int func_depth = 0;

// ---------------- 阶段1：顶层收集 ----------------
// 遍历顶层（不深入函数体）：登记全部函数名 + 顶层赋值变量，
// 使函数前向引用、函数体读取全局变量在阶段2都能通过。

static void collect_top_level(AstNode* node) {
    if (!node) return;
    switch (node->type) {
        case AST_ASSIGN:
            static_sym_put(node->u.assign.varname, VAL_NONE);
            collect_top_level(node->u.assign.expr);
            break;
        case AST_FUNC_DEF:
            // 登记函数名（支持前向引用）；不深入函数体
            static_sym_put(node->u.func_def.name, VAL_FUNC);
            break;
        case AST_BINOP:
            collect_top_level(node->u.bin.left);
            collect_top_level(node->u.bin.right);
            break;
        case AST_UNARY:
            collect_top_level(node->u.uny.child);
            break;
        case AST_CAST:
            collect_top_level(node->u.cast.child);
            break;
        case AST_INDEX:
            collect_top_level(node->u.index.arr);
            collect_top_level(node->u.index.idx);
            break;
        case AST_INDEX_ASSIGN:
            collect_top_level(node->u.index_assign.arr);
            collect_top_level(node->u.index_assign.idx);
            collect_top_level(node->u.index_assign.value);
            break;
        case AST_ARRAY_LIT:
            collect_top_level(node->u.array_lit.elems);
            break;
        case AST_TERNARY:
            collect_top_level(node->u.ternary.cond);
            collect_top_level(node->u.ternary.true_expr);
            collect_top_level(node->u.ternary.false_expr);
            break;
        case AST_PRINT:
            collect_top_level(node->u.print.expr);
            break;
        case AST_SEQ:
            collect_top_level(node->u.seq.first);
            collect_top_level(node->u.seq.second);
            break;
        case AST_BLOCK:
            collect_top_level(node->u.block.stmts);
            break;
        case AST_IF:
            collect_top_level(node->u.ifnode.cond);
            collect_top_level(node->u.ifnode.then_stmt);
            collect_top_level(node->u.ifnode.elif_chain);
            collect_top_level(node->u.ifnode.else_stmt);
            break;
        case AST_IF_CHAIN:
            collect_top_level(node->u.if_chain.cond);
            collect_top_level(node->u.if_chain.if_body);
            {
                AstNode* p = node->u.if_chain.elif_list;
                while (p) {
                    collect_top_level(p->u.elif.cond);
                    collect_top_level(p->u.elif.body);
                    p = p->u.elif.next;
                }
            }
            collect_top_level(node->u.if_chain.else_body);
            break;
        case AST_ELIF:
            collect_top_level(node->u.elif.cond);
            collect_top_level(node->u.elif.body);
            break;
        case AST_FOR:
            collect_top_level(node->u.for_node.init);
            collect_top_level(node->u.for_node.cond);
            collect_top_level(node->u.for_node.update);
            collect_top_level(node->u.for_node.body);
            break;
        case AST_WHILE:
            collect_top_level(node->u.while_node.cond);
            collect_top_level(node->u.while_node.body);
            break;
        case AST_SWITCH:
            collect_top_level(node->u.sw.cond);
            {
                AstNode* cp = node->u.sw.cases;
                while (cp) {
                    if (cp->u.cs.const_val) collect_top_level(cp->u.cs.const_val);
                    collect_top_level(cp->u.cs.body);
                    cp = cp->u.cs.next;
                }
            }
            break;
        case AST_CASE:
            if (node->u.cs.const_val) collect_top_level(node->u.cs.const_val);
            collect_top_level(node->u.cs.body);
            break;
        case AST_RETURN:
            collect_top_level(node->u.ret.ret_val);
            break;
        case AST_CALL: {
            AstNode* a = node->u.call.args;
            if (a) {
                if (a->type != AST_SEQ) collect_top_level(a);
                else {
                    collect_top_level(a->u.seq.first);
                    collect_top_level(a->u.seq.second);
                }
            }
            break;
        }
        default:
            break;
    }
}

// 实参链表是左嵌套 AST_SEQ 链，递归展开逐个检查
static int typecheck_call_args(AstNode* args) {
    if (!args) return 0;
    if (args->type != AST_SEQ) return typecheck_expr(args);
    return typecheck_call_args(args->u.seq.first) | typecheck_call_args(args->u.seq.second);
}

int ast_typecheck(AstNode* node)
{
    static_sym_reset();
    func_depth = 0;
    if(!node) return 0;
    // 阶段1：顶层收集（函数名 + 全局变量），支持前向引用/函数体读全局
    collect_top_level(node);
    // 阶段2：全面检查（含函数体递归）
    return typecheck_expr(node);
}

// 实参个数：AST_SEQ 二叉链递归计数
static int count_args(AstNode* args)
{
    if(!args) return 0;
    if(args->type != AST_SEQ) return 1;
    return count_args(args->u.seq.first) + count_args(args->u.seq.second);
}

int typecheck_expr(AstNode* node)
{
    if(!node) return 0;
    int err = 0;
    switch(node->type) {
        case AST_INT:
            node->val_type = VAL_INT;
            break;
        case AST_NUM:
            node->val_type = VAL_DOUBLE;
            break;
        case AST_BOOL:
            node->val_type = VAL_BOOL;
            break;
        case AST_NONE:
            node->val_type = VAL_NONE;
            break;
        case AST_STRING:
            node->val_type = VAL_STRING;
            break;
        case AST_CHAR:
            node->val_type = VAL_CHAR;
            break;
        case AST_VAR:{
            ValueType t;
            if(static_sym_get(node->u.varname, &t)) {
                if(t == VAL_FUNC) {
                    // 函数名引用：就地转 AST_FUNCREF（函数作为值）
                    node->type = AST_FUNCREF;
                    node->val_type = VAL_FUNC;
                } else {
                    node->val_type = t;
                }
            } else {
                fprintf(stderr,"语义错误(第%d行)：使用未定义变量 %s\n", node->line, node->u.varname);
                node->val_type = VAL_NONE;
                err = 1;
            }
            break;
        }
        case AST_FUNCREF:
            node->val_type = VAL_FUNC;
            break;
        case AST_UNARY: {
            AstNode* kid = node->u.uny.child;
            BinOp op = node->u.uny.op;
            err |= typecheck_expr(kid);
            if(op == OP_PRE_INC || op == OP_POST_INC || op == OP_PRE_DEC || op == OP_POST_DEC) {
                if(kid->type != AST_VAR) {
                    fprintf(stderr,"语义错误：++/-- 的操作数必须是变量\n");
                    return -1;
                }
            }
            node->val_type = kid->val_type;
            break;
        }
        case AST_BINOP:{
            err |= typecheck_expr(node->u.bin.left);
            err |= typecheck_expr(node->u.bin.right);
            ValueType tl = node->u.bin.left->val_type;
            ValueType tr = node->u.bin.right->val_type;
            int left_unknown = (tl == VAL_NONE);
            int right_unknown = (tr == VAL_NONE);
            int left_is_str = (tl == VAL_STRING);
            int right_is_str = (tr == VAL_STRING);
            int left_is_num = (tl == VAL_INT || tl == VAL_DOUBLE || tl == VAL_CHAR);
            int right_is_num = (tr == VAL_INT || tr == VAL_DOUBLE || tr == VAL_CHAR);
            int left_is_bool = (tl == VAL_BOOL);
            int right_is_bool= (tr == VAL_BOOL);
            BinOp op = node->u.bin.op;
            if(op == OP_ADD) {
                if(left_unknown || right_unknown) {
                    node->val_type = VAL_NONE;
                } else if(left_is_str || right_is_str || left_is_bool || right_is_bool) {
                    node->val_type = VAL_STRING;
                } else if(left_is_num && right_is_num) {
                    if(tl == VAL_DOUBLE || tr == VAL_DOUBLE)
                        node->val_type = VAL_DOUBLE;
                    else
                        node->val_type = VAL_INT;
                } else {
                    fprintf(stderr,"语义错误：不支持 %s + %s\n", valtype_to_cstr(tl), valtype_to_cstr(tr));
                    err = 1;
                }
            } else if(op == OP_EQ || op == OP_NE) {
                if(left_unknown || right_unknown) {
                    node->val_type = VAL_BOOL;
                } else {
                    int ok = 0;
                    if(tl == tr) { ok = 1; }
                    if( (tl == VAL_CHAR && tr == VAL_INT) || (tl == VAL_INT && tr == VAL_CHAR) ) { ok = 1; }
                    if(!ok) {
                        fprintf(stderr,"语义错误：==/!= 两侧类型不一致 %s vs %s\n", valtype_to_cstr(tl), valtype_to_cstr(tr));
                        err = 1;
                    }
                }
                node->val_type = VAL_BOOL;
            } else if(op == OP_LOGIC_AND || op == OP_LOGIC_OR) {
                // 逻辑运算：任意类型按 truthy 判定，结果 bool
                node->val_type = VAL_BOOL;
            } else if(op == OP_MOD) {
                if(!(left_unknown || right_unknown)) {
                    if(!(left_is_num && right_is_num)) {
                        fprintf(stderr,"语义错误：%% 只支持数值类型\n");
                        err = 1;
                    }
                }
                if(tl == VAL_DOUBLE || tr == VAL_DOUBLE)
                    node->val_type = VAL_DOUBLE;
                else
                    node->val_type = VAL_INT;
            } else {
                if(!(left_unknown || right_unknown)) {
                    if(!(left_is_num && right_is_num)) {
                        fprintf(stderr,"语义错误：运算符只支持数值类型\n");
                        err = 1;
                    }
                }
                if(tl == VAL_DOUBLE || tr == VAL_DOUBLE)
                    node->val_type = VAL_DOUBLE;
                else
                    node->val_type = VAL_INT;
            }
            break;
        }
        case AST_ASSIGN:
            err |= typecheck_expr(node->u.assign.expr);
            node->val_type = node->u.assign.expr->val_type;
            static_sym_put(node->u.assign.varname, node->val_type);
            break;
        case AST_INDEX: {
            err |= typecheck_expr(node->u.index.arr);
            err |= typecheck_expr(node->u.index.idx);
            if(node->u.index.arr->val_type != VAL_ARRAY &&
               node->u.index.arr->val_type != VAL_STRING &&
               node->u.index.arr->val_type != VAL_NONE) {
                fprintf(stderr,"语义错误(第%d行)：下标访问的对象不是数组或字符串\n", node->line);
                err = 1;
            }
            node->val_type = VAL_NONE;   // 元素类型不可静态追踪
            break;
        }
        case AST_INDEX_ASSIGN: {
            err |= typecheck_expr(node->u.index_assign.arr);
            err |= typecheck_expr(node->u.index_assign.idx);
            err |= typecheck_expr(node->u.index_assign.value);
            if(node->u.index_assign.arr->val_type != VAL_ARRAY &&
               node->u.index_assign.arr->val_type != VAL_NONE) {
                fprintf(stderr,"语义错误(第%d行)：下标访问的对象不是数组\n", node->line);
                err = 1;
            }
            node->val_type = node->u.index_assign.value->val_type;
            break;
        }
        case AST_ARRAY_LIT:
            err |= typecheck_expr(node->u.array_lit.elems);
            node->val_type = VAL_ARRAY;
            break;
        case AST_PRINT:
            err |= typecheck_expr(node->u.print.expr);
            node->val_type = node->u.print.expr->val_type;
            break;
        case AST_SEQ:
            err |= typecheck_expr(node->u.seq.first);
            err |= typecheck_expr(node->u.seq.second);
            node->val_type = node->u.seq.second->val_type;
            break;
        case AST_IF:{
            err |= typecheck_expr(node->u.ifnode.cond);
            err |= typecheck_expr(node->u.ifnode.then_stmt);
            if(node->u.ifnode.elif_chain) {
                err |= typecheck_expr(node->u.ifnode.elif_chain);
            }
            if(node->u.ifnode.else_stmt) {
                err |= typecheck_expr(node->u.ifnode.else_stmt);
            }
            node->val_type = VAL_DOUBLE;
            break;
        }
        case AST_BLOCK:
            if(node->u.block.stmts) err |= typecheck_expr(node->u.block.stmts);
            node->val_type = VAL_DOUBLE;
            break;
        case AST_IF_CHAIN:
            err |= typecheck_expr(node->u.if_chain.cond);
            err |= typecheck_expr(node->u.if_chain.if_body);
            {
                AstNode* p = node->u.if_chain.elif_list;
                while(p) {
                    err |= typecheck_expr(p->u.elif.cond);
                    err |= typecheck_expr(p->u.elif.body);
                    p = p->u.elif.next;
                }
            }
            if(node->u.if_chain.else_body) err |= typecheck_expr(node->u.if_chain.else_body);
            node->val_type = VAL_DOUBLE;
            break;
        case AST_ELIF:
            err |= typecheck_expr(node->u.elif.cond);
            err |= typecheck_expr(node->u.elif.body);
            node->val_type = VAL_DOUBLE;
            break;
        case AST_WHILE:
            err |= typecheck_expr(node->u.while_node.cond);
            err |= typecheck_expr(node->u.while_node.body);
            node->val_type = VAL_DOUBLE;
            break;
        case AST_FOR:
            if(node->u.for_node.init) err |= typecheck_expr(node->u.for_node.init);
            if(node->u.for_node.cond) err |= typecheck_expr(node->u.for_node.cond);
            if(node->u.for_node.update) err |= typecheck_expr(node->u.for_node.update);
            err |= typecheck_expr(node->u.for_node.body);
            node->val_type = VAL_DOUBLE;
            break;
        case AST_CAST: {
            err |= typecheck_expr(node->u.cast.child);
            switch(node->u.cast.cast_type) {
                case CAST_INT:      node->val_type = VAL_INT; break;
                case CAST_DOUBLE:   node->val_type = VAL_DOUBLE; break;
                case CAST_CHAR:     node->val_type = VAL_CHAR; break;
                case CAST_BOOL:     node->val_type = VAL_BOOL; break;
                case CAST_STRING:   node->val_type = VAL_STRING; break;
                case CAST_ASCII:    node->val_type = VAL_INT; break;
                default: node->val_type = VAL_INT;
            }
            break;
        }
        case AST_TERNARY: {
            err |= typecheck_expr(node->u.ternary.cond);
            err |= typecheck_expr(node->u.ternary.true_expr);
            err |= typecheck_expr(node->u.ternary.false_expr);
            ValueType t1 = node->u.ternary.true_expr->val_type;
            ValueType t2 = node->u.ternary.false_expr->val_type;
            if(t1 == VAL_STRING || t2 == VAL_STRING) {
                node->val_type = VAL_STRING;
            } else {
                node->val_type = t1;
            }
            break;
        }
        case AST_CALL: {
            // 实参逐个检查（含嵌套调用）
            err |= typecheck_call_args(node->u.call.args);
            // 函数名：已定义函数 或 赋过函数值的变量 均可（与解释器一致）；
            // 内置函数白名单：len/type/input/range/substr（用户函数同名时用户优先）
            ValueType t;
            if(!static_sym_get(node->u.call.name, &t)) {
                static const struct { const char* name; int min; int max; } builtins[] = {
                    {"len", 1, 1}, {"type", 1, 1}, {"input", 0, 0}, {"range", 1, 3}, {"substr", 3, 3},
                    {"toupper", 1, 1}, {"tolower", 1, 1}, {"split", 2, 2}, {"del", 2, 2}, {"insert", 3, 3},
                    {"floor", 1, 1}, {"ceil", 1, 1}, {"abs", 1, 1}, {"sqrt", 1, 1},
                    {"max", 1, -1}, {"min", 1, -1}, {"join", 2, 2}, {"contains", 2, 2},
                    {"repeat", 2, 2}, {"replace", 3, 3}, {"sum", 1, 1}, {"avg", 1, 1},
                    {"format", 1, -1}, {"sort", 1, 1}, {"reverse", 1, 1},
                    {"map", 2, 2}, {"filter", 2, 2}, {"reduce", 3, 3},
                    {"strip", 1, 1}, {"startswith", 2, 2}, {"endswith", 2, 2},
                };
                int found = 0;
                for(int k = 0; k < 31; k++) {
                    if(strcmp(node->u.call.name, builtins[k].name) == 0) {
                        found = 1;
                        int nargs = count_args(node->u.call.args);
                        int bad = (nargs < builtins[k].min) ||
                                  (builtins[k].max >= 0 && nargs > builtins[k].max);
                        if(bad) {
                            if(builtins[k].max >= 0 && builtins[k].min == builtins[k].max)
                                fprintf(stderr,"语义错误(第%d行)：%s() 需要 %d 个实参（给了 %d 个）\n", node->line,
                                        builtins[k].name, builtins[k].min, nargs);
                            else if(builtins[k].max < 0)
                                fprintf(stderr,"语义错误(第%d行)：%s() 需要至少 %d 个实参（给了 %d 个）\n", node->line,
                                        builtins[k].name, builtins[k].min, nargs);
                            else
                                fprintf(stderr,"语义错误(第%d行)：%s() 需要 %d 到 %d 个实参（给了 %d 个）\n", node->line,
                                        builtins[k].name, builtins[k].min, builtins[k].max, nargs);
                            err = 1;
                        }
                        break;
                    }
                }
                if(!found) {
                    fprintf(stderr,"语义错误(第%d行)：调用未定义函数 %s\n", node->line, node->u.call.name);
                    err = 1;
                }
            } else if(t != VAL_NONE && t != VAL_FUNC) {
                fprintf(stderr,"语义错误(第%d行)：%s 不是函数\n", node->line, node->u.call.name);
                err = 1;
            }
            node->val_type = VAL_NONE;
            break;
        }
        case AST_RETURN: {
            if(node->u.ret.ret_val) {
                err |= typecheck_expr(node->u.ret.ret_val);
                node->val_type = node->u.ret.ret_val->val_type;
            } else {
                node->val_type = VAL_NONE;
            }
            break;
        }
        case AST_SWITCH: {
            err |= typecheck_expr(node->u.sw.cond);
            AstNode* cp = node->u.sw.cases;
            while(cp) {
                if(cp->u.cs.const_val) err |= typecheck_expr(cp->u.cs.const_val);
                err |= typecheck_expr(cp->u.cs.body);
                cp = cp->u.cs.next;
            }
            node->val_type = VAL_NONE;
            break;
        }
        case AST_CASE: {
            if(node->u.cs.const_val) err |= typecheck_expr(node->u.cs.const_val);
            err |= typecheck_expr(node->u.cs.body);
            node->val_type = VAL_NONE;
            break;
        }
        case AST_BREAK:
        case AST_CONTINUE:
            node->val_type = VAL_NONE;
            break;
        case AST_FUNC_DEF: {
            // ...rest 位置检查
            AstNode* p = node->u.func_def.params;
            while(p) {
                if(p->u.param.is_ellipsis && p->u.param.next != NULL) {
                    fprintf(stderr,"语义错误：可变参数...必须放在参数列表最后\n");
                    err = 1;
                }
                p = p->u.param.next;
            }
            if(func_depth > 0) {
                // 嵌套函数定义：codegen 不支持，语义检查同样跳过（保持一致）
                node->val_type = VAL_FUNC;
                break;
            }
            func_depth++;
            sym_save();
            // 登记参数（覆盖同名全局实现遮蔽；类型动态 → VAL_NONE 占位）
            for(p = node->u.func_def.params; p; p = p->u.param.next) {
                static_sym_put(p->u.param.name, VAL_NONE);
            }
            err |= typecheck_expr(node->u.func_def.body);
            sym_restore();
            func_depth--;
            node->val_type = VAL_FUNC;
            break;
        }

        default: break;
    }
    return err;
}
