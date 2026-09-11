/* 编译期模块：ir_func_table/ir_func_count 为编译期状态，单线程编译设计；
 * 未来支持并发编译时需实例化（每编译任务一份），不影响运行时多线程。 */
// AST → 字节码 IR 编译器
// 遍历结构与 ast_typecheck.c / codegen.c 对齐（用户建议复用其递归结构）。
#include "ir_compile.h"
#include "ir_opt.h"
#include "ast/lumyr_types.h"
#include "ast/ast_types.h"
#include "ast/func_compile.h"
#include "ast/ast_interp.h"
#include "lm_value.h"
#include "gc_runtime.h"
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

// ---------------- 字符串常量缓存 ----------------
// 编译期全局缓存：相同字符串字面量只创建一次 Value，避免重复分配和 bf_const 临时对象泄漏。
// 长字符串（非 SSO）调用 gc_pin 钉住，防止编译期 GC 回收；短字符串内联在 Value 中无需钉住。
static Value* str_cache = NULL;
static char** str_cache_keys = NULL;
static int str_cache_cnt = 0;
static int str_cache_cap = 0;

void string_cache_reset(void)
{
    for(int i = 0; i < str_cache_cnt; i++) {
        free(str_cache_keys[i]);
    }
    free(str_cache);
    free(str_cache_keys);
    str_cache = NULL;
    str_cache_keys = NULL;
    str_cache_cnt = 0;
    str_cache_cap = 0;
}

static Value intern_string(const char* s)
{
    if(!s) s = "";
    for(int i = 0; i < str_cache_cnt; i++) {
        if(strcmp(str_cache_keys[i], s) == 0)
            return str_cache[i];
    }
    Value v = lumyr_make_string(s);
    if(str_cache_cnt >= str_cache_cap) {
        int newcap = str_cache_cap > 0 ? str_cache_cap * 2 : 64;
        Value* nv = (Value*)realloc(str_cache, (size_t)newcap * sizeof(Value));
        char** nk = (char**)realloc(str_cache_keys, (size_t)newcap * sizeof(char*));
        if(!nv || !nk) { fprintf(stderr, "IR: string cache oom\n"); exit(EXIT_FAILURE); }
        str_cache = nv;
        str_cache_keys = nk;
        str_cache_cap = newcap;
    }
    str_cache_keys[str_cache_cnt] = strdup(s);
    str_cache[str_cache_cnt] = v;
    if(v.type == VAL_STRING && !v.str_inline && v.v.s) gc_pin(v.v.s);
    str_cache_cnt++;
    return v;
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

/* ===== type 构造（type Person { name, age } → Person("张三", 18) 编译为 map 字面量） ===== */
static int type_arg_count(AstNode* chain)
{
    if(!chain) return 0;
    if(chain->type == AST_SEQ) return type_arg_count(chain->u.seq.first) + type_arg_count(chain->u.seq.second);
    return 1;
}
static AstNode* type_arg_nth(AstNode* chain, int n, int* cur)
{
    if(!chain) return NULL;
    if(chain->type == AST_SEQ) {
        AstNode* r = type_arg_nth(chain->u.seq.first, n, cur);
        if(r) return r;
        return type_arg_nth(chain->u.seq.second, n, cur);
    }
    if((*cur)++ == n) return chain;
    return NULL;
}
static AstNode* type_wrap_cast(AstNode* e, ValueType vt)
{
    int kind;
    switch(vt) {
        case VAL_INT:    kind = CAST_INT;    break;
        case VAL_DOUBLE: kind = CAST_DOUBLE; break;
        case VAL_STRING: kind = CAST_STRING; break;
        case VAL_BOOL:   kind = CAST_BOOL;   break;
        case VAL_CHAR:   kind = CAST_CHAR;   break;
        case VAL_BYTE:   kind = CAST_BYTE;   break;
        default:         return e;  /* 未标注/自定义：不强转 */
    }
    return new_cast_node(kind, e);
}
/* 单参是否字面量（字符串/数字等）：字面量走位置构造（Tag("a") → {"name":"a"}），
   非字面量（map 字面量/变量/调用结果）走原样返回（Person(m) 标注场景） */
static int type_arg_is_literal(AstNode* m)
{
    if(!m) return 1;
    return m->type == AST_INT || m->type == AST_NUM || m->type == AST_STRING ||
           m->type == AST_BOOL || m->type == AST_CHAR || m->type == AST_ARRAY_LIT;
}
/* Person(a, b) → {"name": cast(a), "age": cast(b)}；
   Person(m) → m（单 map 参数原样，动态语言宽松语义） */
static AstNode* build_type_ctor(AstNode* call, TypeDef* t)
{
    AstNode* args = call->u.call.args;
    int argc = type_arg_count(args);
    AstNode* items = NULL;
    if(argc == 1 && t->nprops >= 1 && !type_arg_is_literal(type_arg_nth(args, 0, &(int){0}))) {
        /* 单参数非字面量：原样返回（map 泛型元素标注场景） */
        AstNode* m = type_arg_nth(args, 0, &(int){0});
        call->u.call.args = NULL;
        return m;
    }
    /* 首项注入只读类名属性：__classname__ = 类型名 */
    items = ast_seq(items, ast_map_entry(ast_string(strdup("__classname__")),
                                         ast_string(strdup(t->name))));
    int n = argc < t->nprops ? argc : t->nprops;
    for(int k = 0; k < n; k++) {
        int cur = 0;
        AstNode* a = type_arg_nth(args, k, &cur);
        AstNode* key = ast_string(strdup(t->props[k]));
        items = ast_seq(items, ast_map_entry(key, type_wrap_cast(a, t->ptypes[k])));
    }
    call->u.call.args = NULL;  /* 参数节点已移入 items 树，摘空原链防双 free */
    return ast_map_lit(items);
}

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

// 递归检测 AST_SEQ 树中是否含 AST_SPREAD
static int has_spread_node(AstNode* e) {
    if(!e) return 0;
    if(e->type == AST_SPREAD) return 1;
    if(e->type == AST_SEQ) return has_spread_node(e->u.seq.first) || has_spread_node(e->u.seq.second);
    return 0;
}
// 递归编译数组字面量元素（含 spread）：栈顶保持为当前数组
static void compile_array_elems(Ctx* c, AstNode* e) {
    if(!e) return;
    if(e->type == AST_SEQ) {
        compile_array_elems(c, e->u.seq.first);
        compile_array_elems(c, e->u.seq.second);
        return;
    }
    if(e->type == AST_SPREAD) {
        c_expr(c, e->u.spread.expr);
        emit(c, OPC_BUILTIN, BUILTIN_ARRAY_ADDALL, 2);
    } else {
        c_expr(c, e);
        emit(c, OPC_BUILTIN, BUILTIN_ARRAY_ADD, 2);
    }
}
// 递归编译 map 字面量条目（含 spread）
// 约定：入口时栈顶为正在构建的 map；出口时栈顶仍为该 map
static void compile_map_entries_spread(Ctx* c, AstNode* e) {
    if(!e) return;
    if(e->type == AST_SEQ) {
        compile_map_entries_spread(c, e->u.seq.first);
        compile_map_entries_spread(c, e->u.seq.second);
        return;
    }
    if(e->type == AST_SPREAD) {
        c_expr(c, e->u.spread.expr);
        emit(c, OPC_BUILTIN, BUILTIN_ARRAY_ADDALL, 2);
    } else {
        // OPC_INDEX_SET 返回被设置的值而非 map，因此先 DUP map，
        // 设置后 POP 掉返回值，保留原 map 在栈顶
        emit(c, OPC_DUP, 0, 0);
        c_expr(c, e->u.map_entry.key);
        c_expr(c, e->u.map_entry.value);
        emit(c, OPC_INDEX_SET, 0, 0);
        emit(c, OPC_POP, 0, 0);
    }
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
// 纯字面量表达式在编译期求值（调用运行时 lumyr_*，语义与执行期一致）。
// 除零不折叠（保留运行期错误行为）。

static int fold_lit(AstNode* node, Value* out)
{
    switch(node->type) {
        case AST_INT:    *out = lumyr_make_int(node->u.inum); return 1;
        case AST_NUM:    *out = lumyr_make_double(node->u.num); return 1;
        case AST_BOOL:   *out = lumyr_make_bool(node->u.bval ? 1 : 0); return 1;
        case AST_CHAR:   *out = lumyr_make_char(node->u.ch); return 1;
        case AST_STRING: *out = lumyr_make_string(node->u.sval); return 1;
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
                case OP_ADD: *out = lumyr_add(l, r); return 1;
                case OP_SUB: *out = lumyr_sub(l, r); return 1;
                case OP_MUL: *out = lumyr_mul(l, r); return 1;
                case OP_DIV:
                    if((r.type == VAL_INT && r.v.i != 0) || (r.type == VAL_DOUBLE && r.v.d != 0.0)) {
                        *out = lumyr_div(l, r);
                        return 1;
                    }
                    return 0;
                case OP_GT: *out = lumyr_gt(l, r); return 1;
                case OP_LT: *out = lumyr_lt(l, r); return 1;
                case OP_GE: *out = lumyr_ge(l, r); return 1;
                case OP_LE: *out = lumyr_le(l, r); return 1;
                case OP_EQ: *out = lumyr_eq(l, r); return 1;
                case OP_NE: *out = lumyr_ne(l, r); return 1;
                case OP_MOD:
                    if((r.type == VAL_INT && r.v.i != 0) || (r.type == VAL_DOUBLE && r.v.d != 0.0)) {
                        *out = lumyr_mod(l, r);
                        return 1;
                    }
                    return 0;
                case OP_LOGIC_AND:
                    *out = lumyr_make_bool(lumyr_to_bool(l) && lumyr_to_bool(r));
                    return 1;
                case OP_LOGIC_OR:
                    *out = lumyr_make_bool(lumyr_to_bool(l) || lumyr_to_bool(r));
                    return 1;
                default: return 0;
            }
        }
        case AST_UNARY: {
            Value v;
            if(!fold_const(c, node->u.uny.child, &v)) return 0;
            switch(node->u.uny.op) {
                case OP_UNARY_PLUS:  *out = lumyr_unary_plus(v); return 1;
                case OP_UNARY_MINUS: *out = lumyr_unary_minus(v); return 1;
                case OP_LOGIC_NOT:   *out = lumyr_logic_not(v); return 1;
                default: return 0;
            }
        }
        case AST_CAST: {
            Value v;
            if(!fold_const(c, node->u.cast.child, &v)) return 0;
            switch(node->u.cast.cast_type) {
                case CAST_INT:    *out = lumyr_cast_int(v); return 1;
                case CAST_DOUBLE: *out = lumyr_cast_double(v); return 1;
                case CAST_CHAR:   *out = lumyr_cast_char(v); return 1;
                case CAST_BOOL:   *out = lumyr_cast_bool(v); return 1;
                case CAST_STRING: *out = lumyr_cast_string(v); return 1;
                case CAST_ASCII:  *out = lumyr_cast_ascii(v); return 1;
                case CAST_BYTE:   *out = lumyr_cast_byte(v); return 1;
                case CAST_INT8:   *out = lumyr_cast_int8(v); return 1;
                case CAST_INT16:  *out = lumyr_cast_int16(v); return 1;
                case CAST_INT32:  *out = lumyr_cast_int32(v); return 1;
                case CAST_INT64:  *out = lumyr_cast_int64(v); return 1;
                case CAST_UINT8:  *out = lumyr_cast_uint8(v); return 1;
                case CAST_UINT16: *out = lumyr_cast_uint16(v); return 1;
                case CAST_UINT32: *out = lumyr_cast_uint32(v); return 1;
                case CAST_UINT64: *out = lumyr_cast_uint64(v); return 1;
                case CAST_LONG: *out = lumyr_cast_long(v); return 1;
                case CAST_LONGLONG: *out = lumyr_cast_longlong(v); return 1;
                case CAST_FLOAT: *out = lumyr_cast_float(v); return 1;
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
            emit(c, OPC_LOAD_CONST, bf_const(c->fn, lumyr_make_int(node->u.inum)), 0);
            break;
        case AST_NUM:
            emit(c, OPC_LOAD_CONST, bf_const(c->fn, lumyr_make_double(node->u.num)), 0);
            break;
        case AST_BOOL:
            emit(c, OPC_LOAD_CONST, bf_const(c->fn, lumyr_make_bool(node->u.bval ? 1 : 0)), 0);
            break;
        case AST_NONE:
            emit(c, OPC_LOAD_CONST, bf_const(c->fn, val_none()), 0);
            break;
        case AST_CHAR:
            emit(c, OPC_LOAD_CONST, bf_const(c->fn, lumyr_make_char(node->u.ch)), 0);
            break;
        case AST_STRING:
            emit(c, OPC_LOAD_CONST, bf_const(c->fn, intern_string(node->u.sval)), 0);
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
            if(strncmp(node->u.func_def.name, "_lambda_", 8) == 0) {
                // 有捕获变量：运行时装箱生成闭包实例；无捕获：直接取全局共享函数值
                if(lambda_capture_count(node->u.func_def.name) > 0)
                    emit(c, OPC_MKCLOSURE, bf_sym(c->fn, node->u.func_def.name), 0);
                else
                    emit(c, OPC_GETFUNC, bf_sym(c->fn, node->u.func_def.name), 0);
            } else {
                emit(c, OPC_LOAD_CONST, bf_const(c->fn, val_none()), 0);
            }
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
                     bf_const(c->fn, lumyr_make_bool(bop == OP_LOGIC_OR)), 0);
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
                [CAST_INT8] = OPC_CAST_INT8, [CAST_INT16] = OPC_CAST_INT16,
                [CAST_INT32] = OPC_CAST_INT32, [CAST_INT64] = OPC_CAST_INT64,
                [CAST_UINT8] = OPC_CAST_UINT8, [CAST_UINT16] = OPC_CAST_UINT16,
                [CAST_UINT32] = OPC_CAST_UINT32, [CAST_UINT64] = OPC_CAST_UINT64,
                [CAST_LONG] = OPC_CAST_LONG, [CAST_LONGLONG] = OPC_CAST_LONGLONG,
                [CAST_FLOAT] = OPC_CAST_FLOAT,
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
            // ---- const fn 编译期求值 ----
            AstNode* func_ast = func_ast_lookup(node->u.call.name);
            if(func_ast && func_ast->u.func_def.is_const) {
                // 检查所有参数是否都是编译期常量
                _Bool all_const = 1;
                AstNode* arg = node->u.call.args;
                while(arg) {
                    if(arg->type != AST_INT && arg->type != AST_NUM &&
                       arg->type != AST_BOOL && arg->type != AST_STRING &&
                       arg->type != AST_CHAR && arg->type != AST_NONE) {
                        all_const = 0;
                        break;
                    }
                    arg = arg->u.seq.second;
                }
                if(all_const) {
                    // 编译期求值：用解释执行引擎求值函数调用
                    Value cv = ast_eval(node);
                    emit(c, OPC_LOAD_CONST, bf_const(c->fn, cv), 0);
                    break;
                }
            }
            int ti = type_lookup(node->u.call.name);
            if(ti >= 0) {
                /* 类型构造调用：Person(a, b) → map 字面量（属性按序强转） */
                AstNode* ml = build_type_ctor(node, type_get(ti));
                c_expr(c, ml);
                ast_free(ml);
                break;
            }
            /* 引用语义：修改型内置直接原地修改，无需自动 DUP+STORE 回变量 */
            int argc = 0;
            c_args(c, node->u.call.args, &argc);
            // 用户函数优先；否则内置函数（len/type/input/range/substr）
            static const char* bnames[BUILTIN_COUNT] = {"len", "type", "input", "range", "substr", "toupper", "tolower", "split", "del", "insert", "floor", "ceil", "abs", "sqrt", "max", "min", "join", "contains", "repeat", "replace", "sum", "avg", "format", "sort", "reverse", "map", "filter", "reduce", "strip", "startswith", "endswith", "read_file", "write_file", "file_exists", "keys", "values", "thread", "thread_join", "mutex", "rmutex", "rwlock", "spinlock", "lock", "unlock", "trylock", "rdlock", "wrlock", "tryrdlock", "trywrlock", "condvar", "cond_wait", "cond_wait_timeout", "cond_signal", "cond_broadcast", "threadlocal_get", "threadlocal_set", "get", "post", "put", "delete", "head", "patch", "json", "stringify", "add", "remove", "clear", "indexOf", "arr_get", "set", "first", "last", "has", "flat", "qs", "addAll", "bytes", "str", "encode", "decode", "encodeURL", "decodeURL", "md5", "encodeBase64", "decodeBase64", "regex_match", "regex_search", "regex_replace", "now", "timestamp", "timestamp_ms", "sleep", "date", "time", "datetime", "format_time", "debug", "info", "warn", "error", "fatal", "gc_count", "gc_bytes", "gc_collect", "gc_stw_ns"};
            int bid = -1;
            if(!ir_func_table_lookup(node->u.call.name)) {
                for(int k = 0; k < BUILTIN_COUNT; k++) {
                    if(strcmp(node->u.call.name, bnames[k]) == 0) { bid = k; break; }
                }
            }
            if(bid >= 0) {
                emit(c, OPC_BUILTIN, bid, argc);
            }
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
        case AST_SAFE_CALL: {
            // 安全调用：用 OPC_JMP_IF_NULL 判断是否为 null
            // 1. 压入 obj
            c_expr(c, node->u.safe_call.obj);
            // 2. 复制 obj（用于判断和返回）
            emit(c, OPC_DUP, 0, 0);
            // 3. 如果为 null，跳转到 null 分支
            int jnull = emit_here(c, OPC_JMP_IF_NULL, 0, 0);
            // 4. obj 非 null：弹出副本，执行方法调用或属性访问
            emit(c, OPC_POP, 0, 0);
            if(node->u.safe_call.args != NULL) {
                // 安全方法调用：obj?.method(args) → method(obj, args)
                c_expr(c, node->u.safe_call.obj);
                int argc = 0;
                c_args(c, node->u.safe_call.args, &argc);
                // 检查是否为内置函数
                static const char* bnames[BUILTIN_COUNT] = {"len", "type", "input", "range", "substr", "toupper", "tolower", "split", "del", "insert", "floor", "ceil", "abs", "sqrt", "max", "min", "join", "contains", "repeat", "replace", "sum", "avg", "format", "sort", "reverse", "map", "filter", "reduce", "strip", "startswith", "endswith", "read_file", "write_file", "file_exists", "keys", "values", "thread", "thread_join", "mutex", "rmutex", "rwlock", "spinlock", "lock", "unlock", "trylock", "rdlock", "wrlock", "tryrdlock", "trywrlock", "condvar", "cond_wait", "cond_wait_timeout", "cond_signal", "cond_broadcast", "threadlocal_get", "threadlocal_set", "get", "post", "put", "delete", "head", "patch", "json", "stringify", "add", "remove", "clear", "indexOf", "arr_get", "set", "first", "last", "has", "flat", "qs", "addAll", "bytes", "str", "encode", "decode", "encodeURL", "decodeURL", "md5", "encodeBase64", "decodeBase64", "regex_match", "regex_search", "regex_replace", "now", "timestamp", "timestamp_ms", "sleep", "date", "time", "datetime", "format_time", "debug", "info", "warn", "error", "fatal", "gc_count", "gc_bytes", "gc_collect", "gc_stw_ns"};
                int bid = -1;
                if(!ir_func_table_lookup(node->u.safe_call.method)) {
                    for(int k = 0; k < BUILTIN_COUNT; k++) {
                        if(strcmp(node->u.safe_call.method, bnames[k]) == 0) { bid = k; break; }
                    }
                }
                if(bid >= 0) {
                    emit(c, OPC_BUILTIN, bid, argc + 1);
                } else {
                    emit(c, OPC_CALL, bf_sym(c->fn, node->u.safe_call.method), argc + 1);
                }
            } else {
                // 安全属性访问：obj?.property → obj["property"]
                c_expr(c, node->u.safe_call.obj);
                emit(c, OPC_LOAD_CONST, bf_const(c->fn, lumyr_make_string(node->u.safe_call.method)), 0);
                emit(c, OPC_INDEX_GET, 0, 0);
            }
            // 5. 跳转到结束
            int jend = emit_here(c, OPC_JMP, 0, 0);
            // 6. null 分支：栈顶是 obj（null），直接返回
            patch_to(c, jnull);
            patch_to(c, jend);
            break;
        }
        case AST_NULL_COALESCE: {
            // 空值合并：left ?? right → 如果 left 为 null，返回 right；否则返回 left
            // 简化模式：非 null 分支直接返回栈顶的 left 副本
            c_expr(c, node->u.null_coalesce.left);
            emit(c, OPC_DUP, 0, 0);  // 复制 left，栈：[left, left]
            int jnull = emit_here(c, OPC_JMP_IF_NULL, 0, 0);  // 弹出栈顶，如果为 null 跳转，栈：[left]
            // 非 null 分支：栈顶是 left 副本，直接跳转到结束
            int jend = emit_here(c, OPC_JMP, 0, 0);
            // null 分支：栈顶是 left，弹出后返回 right
            patch_to(c, jnull);
            emit(c, OPC_POP, 0, 0);  // 弹出 left
            c_expr(c, node->u.null_coalesce.right);  // 求值 right
            patch_to(c, jend);
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
            if(!has_spread_node(node->u.array_lit.elems)) {
                int n = 0;
                c_args(c, node->u.array_lit.elems, &n);
                emit(c, OPC_ARRAY_LIT, 0, n);
            } else {
                emit(c, OPC_ARRAY_LIT, 0, 0);
                compile_array_elems(c, node->u.array_lit.elems);
            }
            break;
        }
        case AST_MAP_LIT: {
            if(!has_spread_node(node->u.map_lit.entries)) {
                int n = 0;
                c_map_entries(c, node->u.map_lit.entries, &n);
                emit(c, OPC_MAP_LIT, 0, n);
            } else {
                emit(c, OPC_MAP_LIT, 0, 0);
                compile_map_entries_spread(c, node->u.map_lit.entries);
            }
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
        case AST_DO_WHILE:
        case AST_FOR:
        case AST_SWITCH:
            c_stmt(c, node);
            emit(c, OPC_LOAD_CONST, bf_const(c->fn, lumyr_make_int(0)), 0);
            break;
        case AST_RETURN:
            if(node->u.ret.ret_val) c_expr(c, node->u.ret.ret_val);
            else emit(c, OPC_LOAD_CONST, bf_const(c->fn, val_none()), 0);
            break;
        case AST_DESTRUCT: {
            c_expr(c, node->u.destruct.rhs);
            for(int i = 0; i < node->u.destruct.count; i++) {
                emit(c, OPC_DUP, 0, 0);
                emit(c, OPC_LOAD_CONST, bf_const(c->fn, lumyr_make_int(i)), 0);
                emit(c, OPC_INDEX_GET, 0, 0);
                emit(c, OPC_STORE_VAR, bf_sym(c->fn, node->u.destruct.names[i]), 0);
                emit(c, OPC_POP, 0, 0);
            }
            emit(c, OPC_POP, 0, 0);
            emit(c, OPC_LOAD_CONST, bf_const(c->fn, val_none()), 0);
            break;
        }
        case AST_SPREAD:
            c_expr(c, node->u.spread.expr);
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
        case AST_DESTRUCT:
        case AST_SPREAD:
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
            /* try { body } [catch (Type e) { handler }]... [finally { fbody }]
               支持多个 catch 块和按类型捕获。 */
            int has_fin = (node->u.trynode.finally_body != NULL);
            int has_catch = (node->u.trynode.catch_var != NULL);
            /* 仅在多 catch 或有类型 catch 时走新分支；无类型单个 catch 保持旧逻辑 */
            int use_multi = 0;
            if(node->u.trynode.catch_count > 1) use_multi = 1;
            else if(node->u.trynode.catch_count == 1 && node->u.trynode.catches != NULL
                    && node->u.trynode.catches[0].type != NULL) use_multi = 1;

            if(use_multi && !has_fin) {
                /* 多 catch 块，无 finally - 简化版：先 STORE_VAR，再类型检查 */
                int ccnt = node->u.trynode.catch_count;
                int* jmismatch = (int*)calloc((size_t)ccnt, sizeof(int));
                int* jdone = (int*)calloc((size_t)ccnt, sizeof(int));
                int mismatch_cnt = 0, done_cnt = 0;

                int jtry = here(c);
                emit(c, OPC_TRY, 0, 0);
                c_stmt(c, node->u.trynode.body);
                int jendtry = here(c);
                emit(c, OPC_ENDTRY, 0, 0);
                int jskip = here(c);
                emit(c, OPC_JMP, 0, 0);

                int cstart = here(c);
                bf_patch(c->fn, jtry, cstart);
                bf_patch(c->fn, jendtry, cstart);

                for(int ci = 0; ci < ccnt; ci++) {
                    CatchClause* cc = &node->u.trynode.catches[ci];
                    /* 修补上一个 catch 的不匹配跳转到当前位置 */
                    for(int k = 0; k < mismatch_cnt; k++)
                        bf_patch(c->fn, jmismatch[k], here(c));
                    mismatch_cnt = 0;

                    /* 获取异常对象并存入变量 */
                    emit(c, OPC_GET_ERR, 0, 0);
                    emit(c, OPC_STORE_VAR, bf_sym(c->fn, cc->var), 0);
                    emit(c, OPC_POP, 0, 0);

                    /* 类型检查：加载变量，获取 type 字段，比较 */
                    if(cc->type != NULL) {
                        emit(c, OPC_LOAD_VAR, bf_sym(c->fn, cc->var), 0);
                        emit(c, OPC_LOAD_CONST, bf_const(c->fn, lumyr_make_string("type")), 0);
                        emit(c, OPC_INDEX_GET, 0, 0);
                        emit(c, OPC_LOAD_CONST, bf_const(c->fn, lumyr_make_string(cc->type)), 0);
                        emit(c, OPC_EQ, 0, 0);
                        jmismatch[mismatch_cnt++] = here(c);
                        emit(c, OPC_JMP_IF_FALSE, 0, 0);
                    }

                    /* catch body */
                    c_stmt(c, cc->body);
                    jdone[done_cnt++] = here(c);
                    emit(c, OPC_JMP, 0, 0);
                }

                /* 所有 catch 都不匹配：重新抛出 */
                for(int k = 0; k < mismatch_cnt; k++)
                    bf_patch(c->fn, jmismatch[k], here(c));
                emit(c, OPC_GET_ERR, 0, 0);
                emit(c, OPC_THROW, 0, 0);

                /* 结束位置：修补 jskip 和所有 jdone */
                int end = here(c);
                patch_to(c, jskip);
                for(int k = 0; k < done_cnt; k++)
                    bf_patch(c->fn, jdone[k], end);

                free(jmismatch);
                free(jdone);
                break;
            }
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
                /* 异常路径不需要 ENDTRY：OPC_TRY else 已将 vm_depth 设为 d（TRY 前深度），
                   GET_ERR 不修改 vm_depth，catch_body 结束后 vm_depth 已正确为 d */
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
        case AST_DO_WHILE: {
            int l_body = here(c);
            layer_push(c, 0, -1);              /* continue 目标未知（cond 前才知道） */
            c_stmt(c, node->u.while_node.body);
            Layer l = layer_pop(c);
            int l_cond = here(c);              /* continue 跳到这里（cond 检查前） */
            for(int i = 0; i < l.cont_cnt; i++) bf_patch(c->fn, l.cont[i], l_cond);
            for(int i = 0; i < l.cont_fin_cnt; i++) bf_patch_b(c->fn, l.cont_fin[i], l_cond);
            c_expr(c, node->u.while_node.cond);
            emit(c, OPC_JMP_IF_TRUE, l_body, 0);  /* cond 为真则跳回 body */
            int l_end = here(c);
            for(int i = 0; i < l.brk_cnt; i++) bf_patch(c->fn, l.brk[i], l_end);
            for(int i = 0; i < l.brk_fin_cnt; i++) bf_patch_b(c->fn, l.brk_fin[i], l_end);
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
                } else if(cp->u.cs.is_type_match) {
                    // 类型匹配：type(sw_val) == "typename"
                    emit(c, OPC_DUP, 0, 0);
                    emit(c, OPC_BUILTIN, BUILTIN_TYPE, 1);  // type(sw_val)
                    const char* type_names[] = {"none","int","double","bool","char","string","func","array","map","error","byte"};
                    int tidx = cp->u.cs.match_type;
                    if(tidx < 0 || tidx > 11) tidx = 0;
                    int cidx = bf_const(c->fn, lumyr_make_string(type_names[tidx]));
                    emit(c, OPC_LOAD_CONST, cidx, 0);
                    emit(c, OPC_EQ, 0, 0);
                    last_jf = emit_here(c, OPC_JMP_IF_FALSE, 0, 0);
                    emit(c, OPC_POP, 0, 0);      // 命中：丢弃 sw_val
                    c_stmt(c, cp->u.cs.body);
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

    /* 优化 pass：常量折叠。在所有 BytecodeFunc 生成完毕后、返回前，
       对 main 与函数表中每个函数统一做一遍 IR peephole 优化。
       VM 执行 / -S 反汇编 / -c 代码生成三条通道共用此 IR，故双通道一致。 */
    for(int k = 0; k < ir_func_count; k++) {
        if(ir_func_table[k]) ir_optimize(ir_func_table[k]);
    }
    ir_optimize(fn);

    /* 编译完成：清理字符串常量缓存（长字符串已 gc_pin，由 GC 回收） */
    string_cache_reset();

    return fn;
}
