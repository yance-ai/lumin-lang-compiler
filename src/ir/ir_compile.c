/* 编译期模块：ir_func_table/ir_func_count 为编译期状态，单线程编译设计；
 * 未来支持并发编译时需实例化（每编译任务一份），不影响运行时多线程。 */
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
// 动态扩容，无硬上限。
static BytecodeFunc** ir_func_table = NULL;
static int ir_func_count = 0;
static int ir_func_cap = 0;

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
    if(ir_func_count >= ir_func_cap) {
        int newcap = ir_func_cap > 0 ? ir_func_cap * 2 : 64;
        BytecodeFunc** nt = (BytecodeFunc**)realloc(ir_func_table, (size_t)newcap * sizeof(BytecodeFunc*));
        if(!nt) { fprintf(stderr, "IR: 函数表扩容内存不足\n"); exit(EXIT_FAILURE); }
        ir_func_table = nt;
        ir_func_cap = newcap;
    }
    ir_func_table[ir_func_count++] = fn;
}

// ---------------- 编译上下文 ----------------

typedef struct {
    int kind;              // 0=循环 1=switch
    int* brk; int brk_cnt, brk_cap;    // 未定 break 跳转位置（JMP.a）
    int* cont; int cont_cnt, cont_cap; // 未定 continue 跳转位置（循环，JMP.a）
    int* brk_fin; int brk_fin_cnt, brk_fin_cap;   // try-finally 内 break 的 FIN_PUSH 位置（patch b）
    int* cont_fin; int cont_fin_cnt, cont_fin_cap; // try-finally 内 continue 的 FIN_PUSH 位置（patch b）
    int cont_target;       // 已知 continue 目标（while 的 cond 开头）或 -1
} Layer;

typedef struct {
    BytecodeFunc* fn;
    Layer* layers;           // 动态：循环/switch 嵌套无硬上限
    int layer_depth;
    int layers_cap;
    /* finally 上下文：fin_depth>0 表示当前编译位置在 try-finally 内；
       fin_pend[depth][*] = body/catch 中 PEND_RETURN(b=0) 的位置，fstart 确定后统一 patch b
       行/列均动态扩容，try-finally 嵌套与每层挂起数无硬上限 */
    int** fin_pend;
    int* fin_pend_n;
    int* fin_pend_cap;
    int** fin_jmp;            // try-finally 内 break/continue 的 JMP 位置（patch a=fstart）
    int* fin_jmp_n;
    int* fin_jmp_cap;
    int fin_depth;
    int fin_cap;
} Ctx;

/* 控制层/ finally 行 扩容 helper */
static void ctx_ensure_layers(Ctx* c, int need)
{
    if(need <= c->layers_cap) return;
    int nc = c->layers_cap > 0 ? c->layers_cap * 2 : 32;
    Layer* nl = (Layer*)realloc(c->layers, (size_t)nc * sizeof(Layer));
    if(!nl) { fprintf(stderr, "IR: 控制层扩容内存不足\n"); exit(EXIT_FAILURE); }
    c->layers = nl;
    c->layers_cap = nc;
}

static void ctx_ensure_fin_rows(Ctx* c, int need)
{
    if(need < c->fin_cap) return;
    int nc = c->fin_cap > 0 ? c->fin_cap * 2 : 32;
    int** np = (int**)realloc(c->fin_pend, (size_t)nc * sizeof(int*));
    if(!np) { fprintf(stderr, "IR: finally 层扩容内存不足\n"); exit(EXIT_FAILURE); }
    c->fin_pend = np;
    int** nj = (int**)realloc(c->fin_jmp, (size_t)nc * sizeof(int*));
    if(!nj) { fprintf(stderr, "IR: finally 层扩容内存不足\n"); exit(EXIT_FAILURE); }
    c->fin_jmp = nj;
    int* nn = (int*)realloc(c->fin_pend_n, (size_t)nc * sizeof(int));
    if(!nn) { fprintf(stderr, "IR: finally 层扩容内存不足\n"); exit(EXIT_FAILURE); }
    c->fin_pend_n = nn;
    int* nm = (int*)realloc(c->fin_jmp_n, (size_t)nc * sizeof(int));
    if(!nm) { fprintf(stderr, "IR: finally 层扩容内存不足\n"); exit(EXIT_FAILURE); }
    c->fin_jmp_n = nm;
    int* nc1 = (int*)realloc(c->fin_pend_cap, (size_t)nc * sizeof(int));
    if(!nc1) { fprintf(stderr, "IR: finally 层扩容内存不足\n"); exit(EXIT_FAILURE); }
    c->fin_pend_cap = nc1;
    int* nc2 = (int*)realloc(c->fin_jmp_cap, (size_t)nc * sizeof(int));
    if(!nc2) { fprintf(stderr, "IR: finally 层扩容内存不足\n"); exit(EXIT_FAILURE); }
    c->fin_jmp_cap = nc2;
    for(int i = c->fin_cap; i < nc; i++) {
        c->fin_pend[i] = NULL; c->fin_pend_n[i] = 0; c->fin_pend_cap[i] = 0;
        c->fin_jmp[i] = NULL;  c->fin_jmp_n[i] = 0;  c->fin_jmp_cap[i] = 0;
    }
    c->fin_cap = nc;
}

static void fin_pend_add(Ctx* c, int pos)
{
    if(c->fin_pend_n[c->fin_depth] >= c->fin_pend_cap[c->fin_depth]) {
        int nc = c->fin_pend_cap[c->fin_depth] > 0 ? c->fin_pend_cap[c->fin_depth] * 2 : 16;
        int* na = (int*)realloc(c->fin_pend[c->fin_depth], (size_t)nc * sizeof(int));
        if(!na) { fprintf(stderr, "IR: finally 挂起表扩容内存不足\n"); exit(EXIT_FAILURE); }
        c->fin_pend[c->fin_depth] = na;
        c->fin_pend_cap[c->fin_depth] = nc;
    }
    c->fin_pend[c->fin_depth][c->fin_pend_n[c->fin_depth]++] = pos;
}

static void fin_jmp_add(Ctx* c, int pos)
{
    if(c->fin_jmp_n[c->fin_depth] >= c->fin_jmp_cap[c->fin_depth]) {
        int nc = c->fin_jmp_cap[c->fin_depth] > 0 ? c->fin_jmp_cap[c->fin_depth] * 2 : 16;
        int* na = (int*)realloc(c->fin_jmp[c->fin_depth], (size_t)nc * sizeof(int));
        if(!na) { fprintf(stderr, "IR: finally 跳转表扩容内存不足\n"); exit(EXIT_FAILURE); }
        c->fin_jmp[c->fin_depth] = na;
        c->fin_jmp_cap[c->fin_depth] = nc;
    }
    c->fin_jmp[c->fin_depth][c->fin_jmp_n[c->fin_depth]++] = pos;
}

static int here(Ctx* c) { return c->fn->code_len; }

static void emit(Ctx* c, OpCode op, int a, int b) { bf_emit(c->fn, op, a, b); }

static int emit_here(Ctx* c, OpCode op, int a, int b) { return bf_emit_here(c->fn, op, a, b); }

static void patch_to(Ctx* c, int pos) { bf_patch(c->fn, pos, here(c)); }

// ---------------- 控制层（break/continue） ----------------

static void layer_push(Ctx* c, int kind, int cont_target)
{
    ctx_ensure_layers(c, c->layer_depth + 1);
    Layer* l = &c->layers[c->layer_depth++];
    memset(l, 0, sizeof(Layer));
    l->kind = kind;
    l->cont_target = cont_target;
}

static Layer layer_pop(Ctx* c)
{
    return c->layers[--c->layer_depth];
}

static void layer_brk_fin_add(Ctx* c, int pos)
{
    Layer* l = &c->layers[c->layer_depth - 1];
    if(l->brk_fin_cnt >= l->brk_fin_cap) {
        l->brk_fin_cap = l->brk_fin_cap ? l->brk_fin_cap * 2 : 4;
        l->brk_fin = (int*)realloc(l->brk_fin, sizeof(int) * l->brk_fin_cap);
    }
    l->brk_fin[l->brk_fin_cnt++] = pos;
}
static void layer_cont_fin_add(Ctx* c, int pos)
{
    Layer* l = &c->layers[c->layer_depth - 1];
    if(l->cont_fin_cnt >= l->cont_fin_cap) {
        l->cont_fin_cap = l->cont_fin_cap ? l->cont_fin_cap * 2 : 4;
        l->cont_fin = (int*)realloc(l->cont_fin, sizeof(int) * l->cont_fin_cap);
    }
    l->cont_fin[l->cont_fin_cnt++] = pos;
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

// 字典字面量项递归展开：AST_SEQ 链 / AST_MAP_ENTRY 单节点
static void c_map_entries(Ctx* c, AstNode* e, int* n) {
    if(!e) return;
    if(e->type == AST_SEQ) {
        c_map_entries(c, e->u.seq.first, n);
        c_map_entries(c, e->u.seq.second, n);
        return;
    }
    c_expr(c, e->u.map_entry.key);
    c_expr(c, e->u.map_entry.value);
    (*n)++;
}

// ---------------- 常量折叠 ----------------
// 纯字面量表达式在编译期求值（调用运行时 lumin_*，语义与执行期一致）。
// 除零不折叠（保留运行期错误行为）。

static int fold_lit(AstNode* node, Value* out)
{
    switch(node->type) {
        case AST_INT:    *out = lumin_make_int(node->u.inum); return 1;
        case AST_NUM:    *out = lumin_make_double(node->u.num); return 1;
        case AST_BOOL:   *out = lumin_make_bool(node->u.bval ? 1 : 0); return 1;
        case AST_CHAR:   *out = lumin_make_char(node->u.ch); return 1;
        case AST_STRING: *out = lumin_make_string(node->u.sval); return 1;
        default:         return 0;
    }
}

static int fold_const(Ctx* c, AstNode* node, Value* out)
{
    (void)c;
    switch(node->type) {
        case AST_INT: case AST_NUM: case AST_BOOL: case AST_CHAR: case AST_STRING:
            return fold_lit(node, out);
        case AST_BINOP: {
            Value l, r;
            if(!fold_const(c, node->u.bin.left, &l)) return 0;
            if(!fold_const(c, node->u.bin.right, &r)) return 0;
            switch(node->u.bin.op) {
                case OP_ADD: *out = lumin_add(l, r); return 1;
                case OP_SUB: *out = lumin_sub(l, r); return 1;
                case OP_MUL: *out = lumin_mul(l, r); return 1;
                case OP_DIV:
                    if((r.type == VAL_INT && r.v.i != 0) || (r.type == VAL_DOUBLE && r.v.d != 0.0)) {
                        *out = lumin_div(l, r);
                        return 1;
                    }
                    return 0;
                case OP_GT: *out = lumin_gt(l, r); return 1;
                case OP_LT: *out = lumin_lt(l, r); return 1;
                case OP_GE: *out = lumin_ge(l, r); return 1;
                case OP_LE: *out = lumin_le(l, r); return 1;
                case OP_EQ: *out = lumin_eq(l, r); return 1;
                case OP_NE: *out = lumin_ne(l, r); return 1;
                case OP_MOD:
                    if((r.type == VAL_INT && r.v.i != 0) || (r.type == VAL_DOUBLE && r.v.d != 0.0)) {
                        *out = lumin_mod(l, r);
                        return 1;
                    }
                    return 0;
                case OP_LOGIC_AND:
                    *out = lumin_make_bool(lumin_to_bool(l) && lumin_to_bool(r));
                    return 1;
                case OP_LOGIC_OR:
                    *out = lumin_make_bool(lumin_to_bool(l) || lumin_to_bool(r));
                    return 1;
                default: return 0;
            }
        }
        case AST_UNARY: {
            Value v;
            if(!fold_const(c, node->u.uny.child, &v)) return 0;
            switch(node->u.uny.op) {
                case OP_UNARY_PLUS:  *out = lumin_unary_plus(v); return 1;
                case OP_UNARY_MINUS: *out = lumin_unary_minus(v); return 1;
                case OP_LOGIC_NOT:   *out = lumin_logic_not(v); return 1;
                default: return 0;
            }
        }
        case AST_CAST: {
            Value v;
            if(!fold_const(c, node->u.cast.child, &v)) return 0;
            switch(node->u.cast.cast_type) {
                case CAST_INT:    *out = lumin_cast_int(v); return 1;
                case CAST_DOUBLE: *out = lumin_cast_double(v); return 1;
                case CAST_CHAR:   *out = lumin_cast_char(v); return 1;
                case CAST_BOOL:   *out = lumin_cast_bool(v); return 1;
                case CAST_STRING: *out = lumin_cast_string(v); return 1;
                case CAST_ASCII:  *out = lumin_cast_ascii(v); return 1;
                case CAST_BYTE:   *out = lumin_cast_byte(v); return 1;
                default: return 0;
            }
        }
        default:
            return 0;
    }
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
        case AST_NONE:
            emit(c, OPC_LOAD_CONST, bf_const(c->fn, val_none()), 0);
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
        case AST_FUNCREF:
            // 真函数（全局函数表）→ 取函数值；否则为存函数值的变量 → 读变量
            if(ir_func_table_lookup(node->u.varname))
                emit(c, OPC_GETFUNC, bf_sym(c->fn, node->u.varname), 0);
            else
                emit(c, OPC_LOAD_VAR, bf_sym(c->fn, node->u.varname), 0);
            break;
        case AST_FUNC_DEF:
            // 匿名函数表达式：函数已由 yacc 期注册（compile_func_from_ast → IR 函数表），
            // 表达式求值 = 压入函数值（内部名 _lambda_N）
            if(strncmp(node->u.func_def.name, "_lambda_", 8) == 0)
                emit(c, OPC_GETFUNC, bf_sym(c->fn, node->u.func_def.name), 0);
            else
                emit(c, OPC_LOAD_CONST, bf_const(c->fn, val_none()), 0);
            break;
        case AST_ASSIGN:
            c_expr(c, node->u.assign.expr);
            emit(c, OPC_STORE_VAR, bf_sym(c->fn, node->u.assign.varname), 0);
            break;
        case AST_BINOP: {
            Value fv;
            if(fold_const(c, node, &fv)) {
                emit(c, OPC_LOAD_CONST, bf_const(c->fn, fv), 0);
                break;
            }
            BinOp bop = node->u.bin.op;
            if(bop == OP_LOGIC_AND || bop == OP_LOGIC_OR) {
                // 短路求值：&& 假跳过右；|| 真跳过右
                c_expr(c, node->u.bin.left);
                OpCode jop = (bop == OP_LOGIC_AND) ? OPC_JMP_IF_FALSE : OPC_JMP_IF_TRUE;
                int jskip = bf_emit_here(c->fn, jop, -1, 0);   // 弹左值
                c_expr(c, node->u.bin.right);
                emit(c, OPC_TO_BOOL, 0, 0);                    // 右值 → bool
                int jend = bf_emit_here(c->fn, OPC_JMP, -1, 0);
                bf_patch(c->fn, jskip, c->fn->code_len);       // 短路路径：
                emit(c, OPC_LOAD_CONST,
                     bf_const(c->fn, lumin_make_bool(bop == OP_LOGIC_OR)), 0);
                bf_patch(c->fn, jend, c->fn->code_len);
                break;
            }
            c_expr(c, node->u.bin.left);
            c_expr(c, node->u.bin.right);
            static const OpCode map[] = {
                [OP_ADD] = OPC_ADD, [OP_SUB] = OPC_SUB, [OP_MUL] = OPC_MUL, [OP_DIV] = OPC_DIV,
                [OP_MOD] = OPC_MOD,
                [OP_GT] = OPC_GT, [OP_LT] = OPC_LT, [OP_GE] = OPC_GE, [OP_LE] = OPC_LE,
                [OP_EQ] = OPC_EQ, [OP_NE] = OPC_NE,
            };
            emit(c, map[bop], 0, 0);
            break;
        }
        case AST_UNARY: {
            AstNode* kid = node->u.uny.child;
            switch(node->u.uny.op) {
                case OP_PRE_INC:   emit(c, OPC_PRE_INC, bf_sym(c->fn, kid->u.varname), 0); break;
                case OP_POST_INC:  emit(c, OPC_POST_INC, bf_sym(c->fn, kid->u.varname), 0); break;
                case OP_PRE_DEC:   emit(c, OPC_PRE_DEC, bf_sym(c->fn, kid->u.varname), 0); break;
                case OP_POST_DEC:  emit(c, OPC_POST_DEC, bf_sym(c->fn, kid->u.varname), 0); break;
                case OP_UNARY_PLUS: {
                    Value fv;
                    if(fold_const(c, node, &fv)) { emit(c, OPC_LOAD_CONST, bf_const(c->fn, fv), 0); break; }
                    c_expr(c, kid); emit(c, OPC_POS, 0, 0); break;
                }
                case OP_UNARY_MINUS: {
                    Value fv;
                    if(fold_const(c, node, &fv)) { emit(c, OPC_LOAD_CONST, bf_const(c->fn, fv), 0); break; }
                    c_expr(c, kid); emit(c, OPC_NEG, 0, 0); break;
                }
                case OP_LOGIC_NOT: {
                    Value fv;
                    if(fold_const(c, node, &fv)) { emit(c, OPC_LOAD_CONST, bf_const(c->fn, fv), 0); break; }
                    c_expr(c, kid); emit(c, OPC_LOGIC_NOT, 0, 0); break;
                }
                default: break;
            }
            break;
        }
        case AST_CAST: {
            Value fv;
            if(fold_const(c, node, &fv)) {
                emit(c, OPC_LOAD_CONST, bf_const(c->fn, fv), 0);
                break;
            }
            c_expr(c, node->u.cast.child);
            static const OpCode cmap[] = {
                [CAST_INT] = OPC_CAST_INT, [CAST_DOUBLE] = OPC_CAST_DOUBLE,
                [CAST_CHAR] = OPC_CAST_CHAR, [CAST_BOOL] = OPC_CAST_BOOL,
                [CAST_STRING] = OPC_CAST_STRING, [CAST_ASCII] = OPC_CAST_ASCII,
                [CAST_BYTE] = OPC_CAST_BYTE,
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
            // 用户函数优先；否则内置函数（len/type/input/range/substr）
            static const char* bnames[BUILTIN_COUNT] = {"len", "type", "input", "range", "substr", "toupper", "tolower", "split", "del", "insert", "floor", "ceil", "abs", "sqrt", "max", "min", "join", "contains", "repeat", "replace", "sum", "avg", "format", "sort", "reverse", "map", "filter", "reduce", "strip", "startswith", "endswith", "read_file", "write_file", "file_exists", "keys", "values", "thread", "thread_join", "mutex", "rmutex", "rwlock", "spinlock", "lock", "unlock", "trylock", "rdlock", "wrlock", "tryrdlock", "trywrlock", "condvar", "cond_wait", "cond_wait_timeout", "cond_signal", "cond_broadcast", "threadlocal_get", "threadlocal_set", "get", "post", "put", "delete", "head", "patch", "json", "stringify", "add", "remove", "clear", "indexOf", "arr_get", "set", "first", "last", "has"};
            int bid = -1;
            if(!ir_func_table_lookup(node->u.call.name)) {
                for(int k = 0; k < BUILTIN_COUNT; k++) {
                    if(strcmp(node->u.call.name, bnames[k]) == 0) { bid = k; break; }
                }
            }
            if(bid >= 0) emit(c, OPC_BUILTIN, bid, argc);
            else emit(c, OPC_CALL, bf_sym(c->fn, node->u.call.name), argc);
            break;
        }
        case AST_DYN_CALL: {
            int argc = 0;
            c_expr(c, node->u.dyn_call.callee);   // 压函数值
            c_args(c, node->u.dyn_call.args, &argc);
            emit(c, OPC_CALLV, 0, argc);
            break;
        }
        case AST_INDEX:
            c_expr(c, node->u.index.arr);
            c_expr(c, node->u.index.idx);
            emit(c, OPC_INDEX_GET, 0, 0);
            break;
        case AST_INDEX_ASSIGN:
            c_expr(c, node->u.index_assign.arr);
            c_expr(c, node->u.index_assign.idx);
            c_expr(c, node->u.index_assign.value);
            emit(c, OPC_INDEX_SET, 0, 0);
            break;
        case AST_ARRAY_LIT: {
            int n = 0;
            c_args(c, node->u.array_lit.elems, &n);
            emit(c, OPC_ARRAY_LIT, 0, n);
            break;
        }
        case AST_MAP_LIT: {
            int n = 0;
            c_map_entries(c, node->u.map_lit.entries, &n);
            emit(c, OPC_MAP_LIT, 0, n);
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
        case AST_DYN_CALL:
        case AST_VAR:
        case AST_INT:
        case AST_NUM:
        case AST_BOOL:
        case AST_CHAR:
        case AST_STRING:
        case AST_NONE:
            c_expr(c, node);
            emit(c, OPC_POP, 0, 0);
            break;
        case AST_INDEX:
        case AST_INDEX_ASSIGN:
        case AST_ARRAY_LIT:
        case AST_MAP_LIT:
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
            // if-elif-else 链：逐个条件判断（分支数动态，无硬上限）
            int* end_jumps = NULL; int jump_cnt = 0, jump_cap = 0;
            c_expr(c, node->u.if_chain.cond);
            int jf = emit_here(c, OPC_JMP_IF_FALSE, 0, 0);
            c_stmt(c, node->u.if_chain.if_body);
            if(jump_cnt >= jump_cap) {
                int nc = jump_cap > 0 ? jump_cap * 2 : 8;
                int* nj = (int*)realloc(end_jumps, (size_t)nc * sizeof(int));
                if(!nj) { fprintf(stderr, "IR: if-chain 分支表扩容内存不足\n"); exit(EXIT_FAILURE); }
                end_jumps = nj; jump_cap = nc;
            }
            end_jumps[jump_cnt++] = emit_here(c, OPC_JMP, 0, 0);
            patch_to(c, jf);

            AstNode* p = node->u.if_chain.elif_list;
            while(p) {
                c_expr(c, p->u.elif.cond);
                int jf2 = emit_here(c, OPC_JMP_IF_FALSE, 0, 0);
                c_stmt(c, p->u.elif.body);
                if(jump_cnt >= jump_cap) {
                    int nc = jump_cap > 0 ? jump_cap * 2 : 8;
                    int* nj = (int*)realloc(end_jumps, (size_t)nc * sizeof(int));
                    if(!nj) { fprintf(stderr, "IR: if-chain 分支表扩容内存不足\n"); exit(EXIT_FAILURE); }
                    end_jumps = nj; jump_cap = nc;
                }
                end_jumps[jump_cnt++] = emit_here(c, OPC_JMP, 0, 0);
                patch_to(c, jf2);
                p = p->u.elif.next;
            }
            if(node->u.if_chain.else_body) c_stmt(c, node->u.if_chain.else_body);
            for(int i = 0; i < jump_cnt; i++) patch_to(c, end_jumps[i]);
            free(end_jumps);
            break;
        }
        case AST_TRY: {
            /* try { body } [catch (e) { handler }] [finally { fbody }]
               布局（有 finally）：
                 TRY cstart fstart → body → FIN_PUSH(1, end) → JMP fstart
                 cstart: [GET_ERR STORE e handler] → FIN_PUSH(catch?1:2) → JMP fstart
                 fstart: fbody → FINISH → end
               （无 finally）旧布局：TRY → body → ENDTRY → JMP skip → GET_ERR... */
            int has_fin = (node->u.trynode.finally_body != NULL);
            int has_catch = (node->u.trynode.catch_var != NULL);
            int jtry = here(c);
            int jf2_patch = -1;
            emit(c, OPC_TRY, 0, 0);
            if(has_fin) { c->fin_depth++; ctx_ensure_fin_rows(c, c->fin_depth + 1); c->fin_pend_n[c->fin_depth] = 0; }
            c_stmt(c, node->u.trynode.body);
            if(has_fin) {
                int jnp = here(c);
                emit(c, OPC_FIN_PUSH, 1, 0);       // 正常完成 → JMP end
                int jf1 = here(c);
                emit(c, OPC_JMP, 0, 0);
                int cstart = here(c);
                bf_patch(c->fn, jtry, cstart);     // 错误恢复 → catch
                if(has_catch) {
                    emit(c, OPC_GET_ERR, 0, 0);
                    emit(c, OPC_STORE_VAR, bf_sym(c->fn, node->u.trynode.catch_var), 0);
                    emit(c, OPC_POP, 0, 0);        // 丢弃表达式值
                    c_stmt(c, node->u.trynode.catch_body);
                    int jnp2 = here(c);
                    emit(c, OPC_FIN_PUSH, 1, 0);   // catch 处理完 → JMP end
                    jf2_patch = jnp2;
                } else {
                    emit(c, OPC_FIN_PUSH, 2, 0);   // 无 catch → RETHROW
                }
                int jf2 = here(c);
                emit(c, OPC_JMP, 0, 0);
                int fstart = here(c);
                bf_patch(c->fn, jf1, fstart);
                bf_patch(c->fn, jf2, fstart);
                bf_patch_b(c->fn, jtry, fstart);   // TRY.b = finally 起始
                /* body/catch 内 return 挂起的 PEND_RETURN.b = fstart；
                   break/continue 的 JMP.a = fstart */
                for(int pi = 0; pi < c->fin_pend_n[c->fin_depth]; pi++)
                    bf_patch_b(c->fn, c->fin_pend[c->fin_depth][pi], fstart);
                for(int ji = 0; ji < c->fin_jmp_n[c->fin_depth]; ji++)
                    bf_patch(c->fn, c->fin_jmp[c->fin_depth][ji], fstart);
                c->fin_pend_n[c->fin_depth] = 0;
                c->fin_jmp_n[c->fin_depth] = 0;
                /* 已消费，fbody 内 return/break 不再挂起（finally 体直接用完成动作） */
                /* finally 体：内部 return 直接返回（不挂起，避免自跳） */
                c->fin_depth--;
                c_stmt(c, node->u.trynode.finally_body);
                c->fin_depth++;
                int end = here(c);
                emit(c, OPC_FINISH, 0, 0);
                end = here(c);                      // end = FINISH 之后（FINISH 弹出动作后跳此处）
                bf_patch_b(c->fn, jnp, end);        // FIN_PUSH(1).b = end（a=1 保持）
                if(jf2_patch >= 0) bf_patch_b(c->fn, jf2_patch, end);  // catch 的 FIN_PUSH(1).b = end
                if(c->fin_depth > 0) c->fin_depth--;
            } else {
                int jend = here(c);
                emit(c, OPC_ENDTRY, 0, 0);
                int jskip = here(c);
                emit(c, OPC_JMP, 0, 0);
                int cstart = here(c);
                bf_patch(c->fn, jtry, cstart);     // 错误恢复 → catch
                bf_patch(c->fn, jend, cstart);     // ENDTRY 的 a（C 端 else 分支 goto）
                emit(c, OPC_GET_ERR, 0, 0);
                emit(c, OPC_STORE_VAR, bf_sym(c->fn, node->u.trynode.catch_var), 0);
                emit(c, OPC_POP, 0, 0);            // 丢弃 STORE 压回的表达式值，catch 尾 sp 平衡
                c_stmt(c, node->u.trynode.catch_body);
                patch_to(c, jskip);
            }
            break;
        }
        case AST_THROW:
            /* throw expr：求值 → 弹栈顶包装成错误对象抛出 */
            c_expr(c, node->u.thrownode.expr);
            emit(c, OPC_THROW, 0, 0);
            break;
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
            for(int i = 0; i < l.brk_fin_cnt; i++) bf_patch_b(c->fn, l.brk_fin[i], l_end);
            /* try-finally 内 continue 的 FIN_PUSH.b 回填到 while 条件（此前遗漏导致跳 pc=0 死循环） */
            for(int i = 0; i < l.cont_fin_cnt; i++) bf_patch_b(c->fn, l.cont_fin[i], l_cond);
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
            for(int i = 0; i < l.cont_fin_cnt; i++) bf_patch_b(c->fn, l.cont_fin[i], l_cont);
            if(node->u.for_node.update) c_stmt(c, node->u.for_node.update);
            emit(c, OPC_JMP, l_cond, 0);
            int l_end = here(c);
            if(jf >= 0) bf_patch(c->fn, jf, l_end);
            for(int i = 0; i < l.brk_cnt; i++) bf_patch(c->fn, l.brk[i], l_end);
            for(int i = 0; i < l.brk_fin_cnt; i++) bf_patch_b(c->fn, l.brk_fin[i], l_end);
            for(int i = 0; i < l.brk_fin_cnt; i++) bf_patch_b(c->fn, l.brk_fin[i], l_end);
            free(l.brk); free(l.cont);
            break;
        }
        case AST_SWITCH: {
            c_expr(c, node->u.sw.cond);        // 栈顶 sw_val（保留）
            layer_push(c, 1, -1);
            int* end_jumps = NULL; int jump_cnt = 0, jump_cap = 0;
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
                    if(jump_cnt >= jump_cap) {
                        int nc = jump_cap > 0 ? jump_cap * 2 : 8;
                        int* nj = (int*)realloc(end_jumps, (size_t)nc * sizeof(int));
                        if(!nj) { fprintf(stderr, "IR: switch 分支表扩容内存不足\n"); exit(EXIT_FAILURE); }
                        end_jumps = nj; jump_cap = nc;
                    }
                    end_jumps[jump_cnt++] = emit_here(c, OPC_JMP, 0, 0);
                } else {
                    emit(c, OPC_DUP, 0, 0);
                    c_expr(c, cp->u.cs.const_val);
                    emit(c, OPC_EQ, 0, 0);
                    last_jf = emit_here(c, OPC_JMP_IF_FALSE, 0, 0);
                    emit(c, OPC_POP, 0, 0);      // 命中：丢弃 sw_val
                    c_stmt(c, cp->u.cs.body);
                    if(jump_cnt >= jump_cap) {
                        int nc = jump_cap > 0 ? jump_cap * 2 : 8;
                        int* nj = (int*)realloc(end_jumps, (size_t)nc * sizeof(int));
                        if(!nj) { fprintf(stderr, "IR: switch 分支表扩容内存不足\n"); exit(EXIT_FAILURE); }
                        end_jumps = nj; jump_cap = nc;
                    }
                    end_jumps[jump_cnt++] = emit_here(c, OPC_JMP, 0, 0);
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
            free(end_jumps);
            for(int i = 0; i < l.brk_cnt; i++) bf_patch(c->fn, l.brk[i], l_end);
            for(int i = 0; i < l.brk_fin_cnt; i++) bf_patch_b(c->fn, l.brk_fin[i], l_end);
            for(int i = 0; i < l.brk_fin_cnt; i++) bf_patch_b(c->fn, l.brk_fin[i], l_end);
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
            if(c->fin_depth > 0) {
                /* try-finally 内 break：压 BREAK 完成动作（b=循环出口，循环层 patch），
                   再直接 JMP 到 finally（fstart，fin 层 patch），避免正常路径的 FIN_PUSH(1) 覆盖 */
                int pos = emit_here(c, OPC_FIN_PUSH, 3, 0);
                layer_brk_fin_add(c, pos);
                int jp = emit_here(c, OPC_JMP, 0, 0);
                fin_jmp_add(c, jp);
                break;
            }
            int pos = emit_here(c, OPC_JMP, 0, 0);
            layer_brk_add(c, pos);
            break;
        }
        case AST_CONTINUE: {
            if(c->fin_depth > 0) {
                /* try-finally 内 continue：压 CONT 完成动作（b=continue 目标，循环层 patch） */
                int pos = emit_here(c, OPC_FIN_PUSH, 4, 0);
                layer_cont_fin_add(c, pos);
                int jp = emit_here(c, OPC_JMP, 0, 0);
                fin_jmp_add(c, jp);
                break;
            }
            int pos = emit_here(c, OPC_JMP, 0, 0);
            layer_cont_add(c, pos);
            break;
        }
        case AST_RETURN:
            if(c->fin_depth > 0) {
                /* try-finally 内 return：挂起返回值，先跑 finally */
                if(node->u.ret.ret_val) c_expr(c, node->u.ret.ret_val);
                else emit(c, OPC_LOAD_CONST, bf_const(c->fn, val_none()), 0);
                int ppos = here(c);
                emit(c, OPC_PEND_RETURN, 0, 0);   // b 由 AST_TRY 结束处 patch 为 fstart
                fin_pend_add(c, ppos);
                break;
            }
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

// 重编译已注册函数：函数体字节码在 parse 期生成，早于 typecheck 的
// AST_VAR→AST_FUNCREF 转换；typecheck 后对受影响函数重编译（原位替换，
// 保持函数表顺序与 CALL 指令的 index 绑定不变）。
BytecodeFunc* ir_func_table_recompile(const char* name, AstNode* params, AstNode* body)
{
    BytecodeFunc* nb = ir_compile_function(name, params, body); // 内部 add 到表尾
    int old = -1;
    for(int i = 0; i < ir_func_count - 1; i++) {
        if(ir_func_table[i]->name && strcmp(ir_func_table[i]->name, name) == 0) { old = i; break; }
    }
    if(old >= 0) {
        bytecode_func_free(ir_func_table[old]);
        ir_func_table[old] = nb;
        ir_func_count--;   // 去掉尾部重复条目（nb 已原位引用）
    }
    return nb;
}

BytecodeFunc* ir_compile_main(AstNode* root)
{
    BytecodeFunc* fn = bytecode_func_new(NULL, 1);
    Ctx c = { .fn = fn, .layer_depth = 0 };
    c_stmt(&c, root);
    emit(&c, OPC_HALT, 0, 0);
    return fn;
}
