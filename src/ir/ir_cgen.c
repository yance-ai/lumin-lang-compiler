/* 编译期模块：out/g_globals/fn_locals/g_cur_fn/fin_lab_* 均为单次编译状态，
 * 未来并发编译需实例化；运行时多线程由 _Thread_local 执行器状态保证。 */
// IR → C 生成器：把字节码指令流翻译为 C（栈模拟 + goto 标号），语义与 VM 一致。
// 变量两级收集（全局/函数局部+参数）与旧 codegen 对齐：全局 = main 中引用的名字；
// 函数局部 = 参数 + 函数内 STORE/INC/DEC 的名字（排除参数与全局）。
#include "ir_cgen.h"
#include "ir_compile.h"
#include "ast/lumin_types.h"
#include "runtime/lm_qs.h"
#include "runtime/lm_array.h"
#include "runtime/lm_charset.h"
#include "runtime/lm_crypto.h"
#include "runtime/lm_regex.h"
#include "runtime/lm_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// xxd 嵌入的运行时资源（codegen 同款）
extern unsigned char src_runtime_full_runtime_full_h[];
extern unsigned int src_runtime_full_runtime_full_h_len;
extern unsigned char src_runtime_full_runtime_full_c[];
extern unsigned int src_runtime_full_runtime_full_c_len;

typedef struct {
    char** names;   // 动态扩容，无硬上限
    int count;
    int cap;
} NameSet;

static FILE* out;
static NameSet g_globals;      // 全局变量（main 指令流引用）
static NameSet fn_locals;      // 当前函数局部变量（非参数、非全局）
static BytecodeFunc* g_cur_fn; // 当前生成所在函数（NULL=main）

/* 逃逸分析结果：
 * g_stack_alloc[i]=1：指令 i 处的 OPC_ARRAY_LIT 栈分配（ValueArray 结构体）
 * g_items_stack_alloc[i]=1：items 缓冲区也栈分配（完全免堆）
 * g_map_stack_alloc[i]=1：指令 i 处的 OPC_MAP_LIT 栈分配（ValueMap 结构体）
 * 每次 analyze_escape* 后有效，emit_insns 消费，函数结束后释放。 */
static uint8_t* g_stack_alloc = NULL;
static int g_stack_alloc_len = 0;
static uint8_t* g_items_stack_alloc = NULL;
static int g_items_stack_alloc_len = 0;
static uint8_t* g_map_stack_alloc = NULL;
static int g_map_stack_alloc_len = 0;

/* 标量替换（Scalar Replacement）：
 * g_scalar_var[v]=1：局部变量 v 持有一个被标量替换的数组/map，
 *   其创建被拆解为一组标量局部变量，INDEX_GET/INDEX_SET/len 被替换为标量读写。
 * g_scalar_kind[v]：0=数组，1=map
 * g_scalar_count[v]：元素个数（数组）或键值对个数（map）
 * g_scalar_keys[v][k]：map 第 k 个键的常量表索引（仅 map）
 * 标量变量命名：__sr_v{v}_e{k}（变量 v 的第 k 个元素） */
static uint8_t* g_scalar_var = NULL;
static uint8_t* g_scalar_kind = NULL;
static int* g_scalar_count = NULL;
static int** g_scalar_keys = NULL;
static int g_scalar_sym_cnt = 0;

/* items 栈分配最大元素数（每个 Value 32 字节，256 个 = 8KB，防止栈溢出） */
#define ITEMS_STACK_MAX 256
/* 标量替换最大元素数（超过则不替换，避免生成过多标量变量） */
#define SCALAR_REPL_MAX 16

/* 判断指令 i 处的 ARRAY_LIT/MAP_LIT 是否被标量替换。
 * 标量替换模式：LIT + STORE_VAR(v) + POP，且 g_scalar_var[v]==1。
 * 被标量替换的字面量不再生成 __arr_stk_N / __items_stk_N / __map_stk_N 栈声明。 */
static int is_scalar_replaced_lit(const BytecodeFunc* fn, int i)
{
    if(!g_scalar_var) return 0;
    if(i + 2 >= fn->code_len) return 0;
    if(fn->code[i].op != OPC_ARRAY_LIT && fn->code[i].op != OPC_MAP_LIT) return 0;
    if(fn->code[i+1].op != OPC_STORE_VAR) return 0;
    if(fn->code[i+2].op != OPC_POP) return 0;
    int v = fn->code[i+1].a;
    if(v < 0 || v >= fn->sym_cnt) return 0;
    return g_scalar_var[v];
}

// ---------------- NameSet ----------------

static int ns_has(const NameSet* s, const char* name)
{
    for(int i = 0; i < s->count; i++)
        if(strcmp(s->names[i], name) == 0) return 1;
    return 0;
}

static void ns_add(NameSet* s, const char* name)
{
    if(!name || ns_has(s, name)) return;
    if(s->count >= s->cap) {
        int newcap = s->cap > 0 ? s->cap * 2 : 64;
        char** nn = (char**)realloc(s->names, (size_t)newcap * sizeof(char*));
        if(!nn) { fprintf(stderr, "codegen: 符号表扩容内存不足\n"); exit(EXIT_FAILURE); }
        s->names = nn;
        s->cap = newcap;
    }
    s->names[s->count++] = (char*)name;
}

static int fn_has_param(const BytecodeFunc* fn, const char* name)
{
    int total = fn->param_cnt + (fn->has_variadic ? 1 : 0);
    for(int i = 0; i < total; i++)
        if(fn->params[i] && strcmp(fn->params[i], name) == 0) return 1;
    return 0;
}

// 变量名解析：函数内参数/局部 → lmloc_<name>，否则（全局）→ lmvar_<name>
static const char* cvar(const char* name)
{
    static char buf[512];
    if(g_cur_fn && (fn_has_param(g_cur_fn, name) || ns_has(&fn_locals, name))) {
        snprintf(buf, sizeof(buf), "lmloc_%s", name);
    } else {
        snprintf(buf, sizeof(buf), "lmvar_%s", name);
    }
    return buf;
}

// ---------------- 文本工具 ----------------

static void emit_c_string_lit(FILE* f, const char* s)
{
    fputc('"', f);
    for(const char* p = s; *p; ++p) {
        if(*p == '"') fputs("\\\"", f);
        else if(*p == '\\') fputs("\\\\", f);
        else if(*p == '\n') fputs("\\n", f);
        else if(*p == '\t') fputs("\\t", f);
        else fputc(*p, f);
    }
    fputc('"', f);
}

static void emit_c_char_lit(FILE* f, char ch)
{
    fputc('\'', f);
    if(ch == '\'') fputs("\\'", f);
    else if(ch == '\\') fputs("\\\\", f);
    else if(ch == '\n') fputs("\\n", f);
    else if(ch == '\t') fputs("\\t", f);
    else fputc(ch, f);
    fputc('\'', f);
}

static void emit_const(FILE* f, const Value* v)
{
    switch(v->type) {
        case VAL_INT:    fprintf(f, "lumin_make_int(%lld)", v->v.i); break;
        case VAL_DOUBLE: fprintf(f, "lumin_make_double(%.17g)", v->v.d); break;
        case VAL_BOOL:   fprintf(f, "lumin_make_bool(%d)", v->v.b ? 1 : 0); break;
        case VAL_CHAR:   fprintf(f, "lumin_make_char("); emit_c_char_lit(f, v->v.c); fprintf(f, ")"); break;
        case VAL_BYTE:   fprintf(f, "lumin_make_byte(%d)", (int)(v->v.i & 0xFF)); break;
        case VAL_STRING: fprintf(f, "lumin_make_string("); emit_c_string_lit(f, lumin_str_cstr(v)); fprintf(f, ")"); break;
        default:         fprintf(f, "val_none()"); break;
    }
}

// ---------------- 收集 ----------------

// 收集指令流里的变量引用名（LOAD/STORE/INC/DEC）
static void scan_var_refs(BytecodeFunc* fn, NameSet* set, int include_load)
{
    for(int i = 0; i < fn->code_len; i++) {
        Instruction in = fn->code[i];
        switch(in.op) {
            case OPC_STORE_VAR:
            case OPC_PRE_INC:
            case OPC_POST_INC:
            case OPC_PRE_DEC:
            case OPC_POST_DEC:
                if(in.a >= 0 && in.a < fn->sym_cnt) ns_add(set, fn->syms[in.a]);
                break;
            case OPC_LOAD_VAR:
                if(include_load && in.a >= 0 && in.a < fn->sym_cnt) ns_add(set, fn->syms[in.a]);
                break;
            default:
                break;
        }
    }
}

static void collect_func_locals(BytecodeFunc* fn)
{
    NameSet raw; memset(&raw, 0, sizeof(raw));
    scan_var_refs(fn, &raw, 0);   // STORE/INC/DEC 的名字
    memset(&fn_locals, 0, sizeof(fn_locals));
    for(int i = 0; i < raw.count; i++) {
        const char* n = raw.names[i];
        /* 词法遮蔽（对齐 VM）：函数内写过的名字（非参数）一律为局部变量，
           即使与全局同名也遮蔽 —— 与 C 语言"函数内局部变量遮蔽全局"一致。
           读全局（只读未写）仍走 lmvar_（cvar 回退），见 cvar() 的解析。 */
        if(!fn_has_param(fn, n)) ns_add(&fn_locals, n);
    }
}

// ---------------- 指令翻译 ----------------

/* finally 完成跳转表：FIN_PUSH 的目标 pc → label 编号（生成函数头 static void* 数组） */
static int fin_lab_cnt = 0;
static int* fin_lab_pcs = NULL;
static int fin_lab_cap = 0;

static int fin_lab_idx_of(int pc)
{
    for(int i = 0; i < fin_lab_cnt; i++)
        if(fin_lab_pcs[i] == pc) return i;
    if(fin_lab_cnt >= fin_lab_cap) {
        int newcap = fin_lab_cap > 0 ? fin_lab_cap * 2 : 64;
        int* np = (int*)realloc(fin_lab_pcs, (size_t)newcap * sizeof(int));
        if(!np) { fprintf(stderr, "codegen: fin_lab 表扩容内存不足\n"); exit(EXIT_FAILURE); }
        fin_lab_pcs = np;
        fin_lab_cap = newcap;
    }
    fin_lab_pcs[fin_lab_cnt] = pc; return fin_lab_cnt++;
    exit(EXIT_FAILURE);
}

// 判断指令 idx 是否为跳转目标
static int is_jump_target(BytecodeFunc* fn, int idx)
{
    for(int i = 0; i < fn->code_len; i++) {
        Instruction in = fn->code[i];
        if((in.op == OPC_JMP || in.op == OPC_JMP_IF_FALSE || in.op == OPC_JMP_IF_TRUE
            || in.op == OPC_TRY || in.op == OPC_ENDTRY) && in.a == idx)
            return 1;
        if((in.op == OPC_TRY || in.op == OPC_FIN_PUSH || in.op == OPC_PEND_RETURN) && in.b == idx)
            return 1;
    }
    return 0;
}

static void emit_insns(BytecodeFunc* fn)
{
    int i = 0;
    while(i < fn->code_len) {
        if(is_jump_target(fn, i)) fprintf(out, "L%d:;\n", i);
        Instruction in = fn->code[i];
        const char* nm = (in.a >= 0 && in.a < fn->sym_cnt) ? fn->syms[in.a] : NULL;

        /* ===== 标量替换：创建模式 ARRAY_LIT/MAP_LIT + STORE_VAR + POP ===== */
        if((in.op == OPC_ARRAY_LIT || in.op == OPC_MAP_LIT) &&
           i + 2 < fn->code_len &&
           fn->code[i+1].op == OPC_STORE_VAR &&
           fn->code[i+2].op == OPC_POP) {
            int v = fn->code[i+1].a;
            if(g_scalar_var && g_scalar_var[v]) {
                int cnt = g_scalar_count[v];
                if(g_scalar_kind[v] == 0) {
                    /* 数组标量初始化：元素在栈上 __stk[__sp-cnt..__sp-1] */
                    fprintf(out, "    { /* scalar-repl array init v=%d cnt=%d */\n", v, cnt);
                    for(int k = 0; k < cnt; k++)
                        fprintf(out, "        __sr_v%d_e%d = __stk[__sp - %d + %d];\n", v, k, cnt, k);
                    fprintf(out, "        __sp -= %d;\n", cnt);
                    fprintf(out, "    }\n");
                } else {
                    /* map 标量初始化：键值对在栈上，值在奇数位置 */
                    fprintf(out, "    { /* scalar-repl map init v=%d cnt=%d */\n", v, cnt);
                    for(int k = 0; k < cnt; k++)
                        fprintf(out, "        __sr_v%d_e%d = __stk[__sp - %d + %d];\n", v, k, 2*cnt, 2*k + 1);
                    fprintf(out, "        __sp -= %d;\n", 2 * cnt);
                    fprintf(out, "    }\n");
                }
                i += 3;  /* 跳过 LIT + STORE_VAR + POP */
                continue;
            }
        }

        /* ===== 标量替换：使用模式 LOAD_VAR(scalar var) + ... ===== */
        if(in.op == OPC_LOAD_VAR && g_scalar_var && g_scalar_var[in.a]) {
            int v = in.a;
            int cnt = g_scalar_count[v];
            /* 模式1：LOAD_VAR + BUILTIN_LEN → 常量 len */
            if(i + 1 < fn->code_len && fn->code[i+1].op == OPC_BUILTIN &&
               fn->code[i+1].a == BUILTIN_LEN) {
                fprintf(out, "    { Value __c; __c.type = VAL_INT; __c.v.i = %d; __stk[__sp++] = __c; }\n", cnt);
                i += 2;
                continue;
            }
            /* 模式2/3：LOAD_VAR + LOAD_CONST(idx) + INDEX_GET/INDEX_SET */
            if(i + 1 < fn->code_len && fn->code[i+1].op == OPC_LOAD_CONST) {
                int ci = fn->code[i+1].a;
                int elem_idx = -1;
                if(g_scalar_kind[v] == 0) {
                    /* 数组：下标是整数常量 */
                    if(ci >= 0 && ci < fn->const_cnt && fn->consts[ci].type == VAL_INT) {
                        long long idx = fn->consts[ci].v.i;
                        if(idx >= 0 && idx < cnt) elem_idx = (int)idx;
                    }
                } else {
                    /* map：键是字符串常量，匹配 g_scalar_keys[v] */
                    if(ci >= 0 && ci < fn->const_cnt && fn->consts[ci].type == VAL_STRING && g_scalar_keys[v]) {
                        const char* key_str = lumin_str_cstr(&fn->consts[ci]);
                        for(int k = 0; k < cnt; k++) {
                            int kci = g_scalar_keys[v][k];
                            if(kci >= 0 && kci < fn->const_cnt && fn->consts[kci].type == VAL_STRING) {
                                if(strcmp(lumin_str_cstr(&fn->consts[kci]), key_str) == 0) {
                                    elem_idx = k;
                                    break;
                                }
                            }
                        }
                    }
                }
                if(elem_idx >= 0) {
                    /* INDEX_GET：LOAD_VAR + LOAD_CONST + INDEX_GET */
                    if(i + 2 < fn->code_len && fn->code[i+2].op == OPC_INDEX_GET) {
                        fprintf(out, "    __stk[__sp++] = __sr_v%d_e%d;\n", v, elem_idx);
                        i += 3;
                        continue;
                    }
                    /* INDEX_SET：LOAD_VAR + LOAD_CONST + <value> + INDEX_SET（value 单条指令） */
                    if(i + 3 < fn->code_len && fn->code[i+3].op == OPC_INDEX_SET) {
                        Instruction val_in = fn->code[i+2];
                        /* 生成值指令（内联支持 LOAD_CONST / LOAD_VAR / GETFUNC） */
                        if(val_in.op == OPC_LOAD_CONST) {
                            fprintf(out, "    __stk[__sp++] = ");
                            emit_const(out, &fn->consts[val_in.a]);
                            fprintf(out, ";\n");
                        } else if(val_in.op == OPC_LOAD_VAR) {
                            const char* vnm = (val_in.a >= 0 && val_in.a < fn->sym_cnt) ? fn->syms[val_in.a] : NULL;
                            fprintf(out, "    __stk[__sp++] = %s;\n", cvar(vnm));
                        } else if(val_in.op == OPC_GETFUNC) {
                            const char* vnm = (val_in.a >= 0 && val_in.a < fn->sym_cnt) ? fn->syms[val_in.a] : NULL;
                            int fidx = -1;
                            for(int fi = 0; fi < ir_func_table_count(); fi++)
                                if(strcmp(ir_func_table_get(fi)->name, vnm) == 0) { fidx = fi; break; }
                            fprintf(out, "    { Value __f; __f.type = VAL_FUNC; __f.v.func.func_obj = (void*)lum_wrap_%d; __stk[__sp++] = __f; }\n", fidx);
                        } else {
                            /* 不支持的值表达式类型：回退到正常处理（不应发生，分析阶段已过滤） */
                            fprintf(out, "    __stk[__sp++] = val_none(); /* scalar-repl fallback */\n");
                        }
                        /* 标量存储：弹出值，存入标量，再压回（INDEX_SET 返回值语义） */
                        fprintf(out, "    { Value __v = __stk[--__sp]; __sr_v%d_e%d = __v; __stk[__sp++] = __v; }\n", v, elem_idx);
                        i += 4;
                        continue;
                    }
                }
                /* elem_idx < 0 或模式不匹配：回退正常处理 */
            }
        }

        switch(in.op) {
            case OPC_NOP:
                break;
            case OPC_LOAD_CONST:
                fprintf(out, "    __stk[__sp++] = ");
                emit_const(out, &fn->consts[in.a]);
                fprintf(out, ";\n");
                break;
            case OPC_GETFUNC: {
                int fidx = -1;
                for(int fi = 0; fi < ir_func_table_count(); fi++)
                    if(strcmp(ir_func_table_get(fi)->name, nm) == 0) { fidx = fi; break; }
                if(fidx < 0) { fprintf(stderr, "codegen: 未定义函数: %s\n", nm); exit(EXIT_FAILURE); }
                fprintf(out, "    { Value __f; __f.type = VAL_FUNC; __f.v.func.func_obj = (void*)lum_wrap_%d; __stk[__sp++] = __f; }\n", fidx);
                break;
            }
            case OPC_LOAD_VAR:
                fprintf(out, "    __stk[__sp++] = %s;\n", cvar(nm));
                break;
            case OPC_STORE_VAR:
                fprintf(out, "    { Value __v = __stk[--__sp]; %s = __v; __stk[__sp++] = __v; }\n", cvar(nm));
                break;
            case OPC_ADD: fprintf(out, "    { Value __r = __stk[--__sp], __l = __stk[--__sp]; __stk[__sp++] = lumin_add(__l, __r); }\n"); break;
            case OPC_SUB: fprintf(out, "    { Value __r = __stk[--__sp], __l = __stk[--__sp]; __stk[__sp++] = lumin_sub(__l, __r); }\n"); break;
            case OPC_MUL: fprintf(out, "    { Value __r = __stk[--__sp], __l = __stk[--__sp]; __stk[__sp++] = lumin_mul(__l, __r); }\n"); break;
            case OPC_DIV: fprintf(out, "    { Value __r = __stk[--__sp], __l = __stk[--__sp]; __stk[__sp++] = lumin_div(__l, __r); }\n"); break;
            case OPC_MOD: fprintf(out, "    { Value __r = __stk[--__sp], __l = __stk[--__sp]; __stk[__sp++] = lumin_mod(__l, __r); }\n"); break;
            case OPC_GT:  fprintf(out, "    { Value __r = __stk[--__sp], __l = __stk[--__sp]; __stk[__sp++] = lumin_gt(__l, __r); }\n"); break;
            case OPC_LT:  fprintf(out, "    { Value __r = __stk[--__sp], __l = __stk[--__sp]; __stk[__sp++] = lumin_lt(__l, __r); }\n"); break;
            case OPC_GE:  fprintf(out, "    { Value __r = __stk[--__sp], __l = __stk[--__sp]; __stk[__sp++] = lumin_ge(__l, __r); }\n"); break;
            case OPC_LE:  fprintf(out, "    { Value __r = __stk[--__sp], __l = __stk[--__sp]; __stk[__sp++] = lumin_le(__l, __r); }\n"); break;
            case OPC_EQ:  fprintf(out, "    { Value __r = __stk[--__sp], __l = __stk[--__sp]; __stk[__sp++] = lumin_eq(__l, __r); }\n"); break;
            case OPC_NE:  fprintf(out, "    { Value __r = __stk[--__sp], __l = __stk[--__sp]; __stk[__sp++] = lumin_ne(__l, __r); }\n"); break;
            case OPC_NEG: fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_unary_minus(__v); }\n"); break;
            case OPC_POS: fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_unary_plus(__v); }\n"); break;
            case OPC_PRE_INC:  fprintf(out, "    { Value* __vp = &%s; __stk[__sp++] = lumin_pre_inc(__vp); }\n", cvar(nm)); break;
            case OPC_POST_INC: fprintf(out, "    { Value* __vp = &%s; __stk[__sp++] = lumin_post_inc(__vp); }\n", cvar(nm)); break;
            case OPC_PRE_DEC:  fprintf(out, "    { Value* __vp = &%s; __stk[__sp++] = lumin_pre_dec(__vp); }\n", cvar(nm)); break;
            case OPC_POST_DEC: fprintf(out, "    { Value* __vp = &%s; __stk[__sp++] = lumin_post_dec(__vp); }\n", cvar(nm)); break;
            case OPC_CAST_INT:    fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_cast_int(__v); }\n"); break;
            case OPC_CAST_DOUBLE: fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_cast_double(__v); }\n"); break;
            case OPC_CAST_CHAR:   fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_cast_char(__v); }\n"); break;
            case OPC_CAST_BOOL:   fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_cast_bool(__v); }\n"); break;
            case OPC_CAST_STRING: fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_cast_string(__v); }\n"); break;
            case OPC_CAST_ASCII:  fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_cast_ascii(__v); }\n"); break;
            case OPC_CAST_BYTE:   fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_cast_byte(__v); }\n"); break;
            case OPC_CAST_INT8:   fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_cast_int8(__v); }\n"); break;
            case OPC_CAST_INT16:  fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_cast_int16(__v); }\n"); break;
            case OPC_CAST_INT32:  fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_cast_int32(__v); }\n"); break;
            case OPC_CAST_INT64:  fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_cast_int64(__v); }\n"); break;
            case OPC_CAST_UINT8:  fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_cast_uint8(__v); }\n"); break;
            case OPC_CAST_UINT16: fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_cast_uint16(__v); }\n"); break;
            case OPC_CAST_UINT32: fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_cast_uint32(__v); }\n"); break;
            case OPC_CAST_UINT64: fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_cast_uint64(__v); }\n"); break;
            case OPC_CAST_LONG: fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_cast_long(__v); }\n"); break;
            case OPC_CAST_LONGLONG: fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_cast_longlong(__v); }\n"); break;
            case OPC_CAST_FLOAT: fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_cast_float(__v); }\n"); break;
            case OPC_LOGIC_NOT:   fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_logic_not(__v); }\n"); break;
            case OPC_ARRAY_LIT: {
                int n = in.b;
                if(g_stack_alloc && g_stack_alloc[i]) {
                    if(g_items_stack_alloc && g_items_stack_alloc[i]) {
                        /* 完全栈分配：ValueArray 结构体 + items 缓冲区均在 C 栈上，免 GC */
                        fprintf(out, "    {\n");
                        fprintf(out, "        Value __arr = val_array_from_stack_items(&__arr_stk_%d, __items_stk_%d, %d);\n", i, i, n);
                        for(int k = 0; k < n; k++)
                            fprintf(out, "        __arr.v.array->items[%d] = __stk[__sp - %d + %d];\n", k, n, k);
                        fprintf(out, "        __sp = __sp - %d + 1;\n", n);
                        fprintf(out, "        __stk[__sp - 1] = __arr;\n");
                        fprintf(out, "    }\n");
                    } else {
                        /* 半栈分配：ValueArray 结构体在 C 栈上，items 仍走 gc_alloc */
                        fprintf(out, "    {\n");
                        fprintf(out, "        Value __arr = val_array_from_stack(&__arr_stk_%d, %d);\n", i, n);
                        for(int k = 0; k < n; k++)
                            fprintf(out, "        __arr.v.array->items[%d] = __stk[__sp - %d + %d];\n", k, n, k);
                        fprintf(out, "        __sp = __sp - %d + 1;\n", n);
                        fprintf(out, "        __stk[__sp - 1] = __arr;\n");
                        fprintf(out, "    }\n");
                    }
                } else {
                    fprintf(out, "    {\n");
                    fprintf(out, "        Value __arr = val_array(%d);\n", n);
                    for(int k = 0; k < n; k++)
                        fprintf(out, "        __arr.v.array->items[%d] = __stk[__sp - %d + %d];\n", k, n, k);
                    fprintf(out, "        __sp = __sp - %d + 1;\n", n);
                    fprintf(out, "        __stk[__sp - 1] = __arr;\n");
                    fprintf(out, "    }\n");
                }
                break;
            }
            case OPC_MAP_LIT: {
                int n = in.b;
                if(g_map_stack_alloc && g_map_stack_alloc[i]) {
                    /* 栈分配：ValueMap 结构体在 C 栈上，buckets/entries 仍堆分配 */
                    fprintf(out, "    {\n");
                    fprintf(out, "        Value __m = val_map_from_stack(&__map_stk_%d);\n", i);
                    fprintf(out, "        for(int __k = 0; __k < %d; __k++) lumin_map_set(&__m, __stk[__sp - %d + __k * 2], __stk[__sp - %d + __k * 2 + 1]);\n", n, 2 * n, 2 * n);
                    fprintf(out, "        __sp = __sp - %d + 1;\n", 2 * n);
                    fprintf(out, "        __stk[__sp - 1] = __m;\n");
                    fprintf(out, "    }\n");
                } else {
                    fprintf(out, "    {\n");
                    fprintf(out, "        Value __m = lumin_map_lit(&__stk[__sp - %d], %d);\n", 2 * n, n);
                    fprintf(out, "        __sp = __sp - %d + 1;\n", 2 * n);
                    fprintf(out, "        __stk[__sp - 1] = __m;\n");
                    fprintf(out, "    }\n");
                }
                break;
            }
            case OPC_INDEX_GET:
                fprintf(out, "    { Value __idx = __stk[--__sp], __c = __stk[--__sp]; __stk[__sp++] = lumin_index_get(__c, __idx); }\n");
                break;
            case OPC_INDEX_SET:
                fprintf(out, "    { Value __val = __stk[--__sp], __idx = __stk[--__sp], __arr = __stk[--__sp]; __stk[__sp++] = lumin_array_set(__arr, __idx, __val); }\n");
                break;
            case OPC_BUILTIN:
                fprintf(out, "    gc_stw_check();\n");
                switch(in.a) {
                    case BUILTIN_LEN:
                        fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_len(__v); }\n");
                        break;
                    case BUILTIN_TYPE:
                        fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_type(__v); }\n");
                        break;
                    case BUILTIN_INPUT:
                        fprintf(out, "    { __stk[__sp++] = lumin_input(); }\n");
                        break;
                                        case BUILTIN_RANGE:
                        fprintf(out, "    { Value __r = lumin_range_n(&__stk[__sp - %d], %d); __stk[__sp - %d] = __r; __sp = __sp - %d + 1; }\n",
                                in.b, in.b, in.b, in.b);
                        break;
                    case BUILTIN_SUBSTR:
                        fprintf(out, "    { Value __n = __stk[--__sp], __st = __stk[--__sp], __s = __stk[--__sp]; __stk[__sp++] = lumin_substr(__s, __st, __n); }\n");
                        break;
                    case BUILTIN_TOUPPER:
                        fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_toupper(__v); }\n");
                        break;
                    case BUILTIN_TOLOWER:
                        fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_tolower(__v); }\n");
                        break;
                    case BUILTIN_SPLIT:
                        fprintf(out, "    { Value __sep = __stk[--__sp], __s = __stk[--__sp]; __stk[__sp++] = lumin_split(__s, __sep); }\n");
                        break;
                    case BUILTIN_DEL:
                        fprintf(out, "    { Value __idx = __stk[--__sp]; lumin_del(&__stk[__sp-1], __idx); }\n");
                        break;
                    case BUILTIN_INSERT:
                        fprintf(out, "    { Value __val = __stk[--__sp], __idx = __stk[--__sp]; lumin_insert(&__stk[__sp-1], __idx, __val); }\n");
                        break;
                    case BUILTIN_FLOOR:
                        fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_floor(__v); }\n");
                        break;
                    case BUILTIN_CEIL:
                        fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_ceil(__v); }\n");
                        break;
                    case BUILTIN_ABS:
                        fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_abs(__v); }\n");
                        break;
                    case BUILTIN_SQRT:
                        fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_sqrt(__v); }\n");
                        break;
                    case BUILTIN_MAX:
                        fprintf(out, "    { Value __r = lumin_max(&__stk[__sp - %d], %d); __stk[__sp - %d] = __r; __sp = __sp - %d + 1; }\n",
                                in.b, in.b, in.b, in.b);
                        break;
                    case BUILTIN_MIN:
                        fprintf(out, "    { Value __r = lumin_min(&__stk[__sp - %d], %d); __stk[__sp - %d] = __r; __sp = __sp - %d + 1; }\n",
                                in.b, in.b, in.b, in.b);
                        break;
                    case BUILTIN_JOIN:
                        fprintf(out, "    { Value __sep = __stk[--__sp], __arr = __stk[--__sp]; __stk[__sp++] = lumin_join(__arr, __sep); }\n");
                        break;
                    case BUILTIN_CONTAINS:
                        fprintf(out, "    { Value __needle = __stk[--__sp], __hay = __stk[--__sp]; __stk[__sp++] = lumin_contains(__hay, __needle); }\n");
                        break;
                    case BUILTIN_REPEAT:
                        fprintf(out, "    { Value __n = __stk[--__sp], __s = __stk[--__sp]; __stk[__sp++] = lumin_repeat(__s, __n); }\n");
                        break;
                    case BUILTIN_REPLACE:
                        fprintf(out, "    { Value __to = __stk[--__sp], __from = __stk[--__sp], __s = __stk[--__sp]; __stk[__sp++] = lumin_replace(__s, __from, __to); }\n");
                        break;
                    case BUILTIN_SUM:
                        fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_sum(__v); }\n");
                        break;
                    case BUILTIN_FORMAT:
                        fprintf(out, "    { Value __r = lumin_format(&__stk[__sp - %d], %d); __stk[__sp - %d] = __r; __sp = __sp - %d + 1; }\n",
                                in.b, in.b, in.b, in.b);
                        break;
                    case BUILTIN_SORT:
                        fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_sort(__v); }\n");
                        break;
                    case BUILTIN_REVERSE:
                        fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_reverse(__v); }\n");
                        break;
                    case BUILTIN_STRIP:
                        fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_strip(__v); }\n");
                        break;
                    case BUILTIN_STARTSWITH:
                        fprintf(out, "    { Value __r = __stk[--__sp], __l = __stk[--__sp]; __stk[__sp++] = lumin_startswith(__l, __r); }\n");
                        break;
                    case BUILTIN_ENDSWITH:
                        fprintf(out, "    { Value __r = __stk[--__sp], __l = __stk[--__sp]; __stk[__sp++] = lumin_endswith(__l, __r); }\n");
                        break;
                    case BUILTIN_READ_FILE:
                        fprintf(out, "    { Value __r = lumin_read_file(&__stk[__sp - %d], %d); __stk[__sp - %d] = __r; __sp = __sp - %d + 1; }\n", in.b, in.b, in.b, in.b);
                        break;
                    case BUILTIN_WRITE_FILE:
                        fprintf(out, "    { Value __r = lumin_write_file(&__stk[__sp - %d], %d); __stk[__sp - %d] = __r; __sp = __sp - %d + 1; }\n", in.b, in.b, in.b, in.b);
                        break;
                    case BUILTIN_FILE_EXISTS:
                        fprintf(out, "    { Value __r = lumin_file_exists(&__stk[__sp - %d], %d); __stk[__sp - %d] = __r; __sp = __sp - %d + 1; }\n", in.b, in.b, in.b, in.b);
                        break;
                    case BUILTIN_KEYS:
                        fprintf(out, "    { Value __r = lumin_map_keys(__stk[__sp - %d]); __stk[__sp - %d] = __r; __sp = __sp - %d + 1; }\n", in.b, in.b, in.b);
                        break;
                    case BUILTIN_THREAD: {
                        int argc = in.b;
                        fprintf(out, "    {\n");
                        fprintf(out, "        int __argc = %d;\n", argc);
                        fprintf(out, "        Value __fn = __stk[__sp - __argc];\n");
                        fprintf(out, "        if(__fn.type != VAL_FUNC) runtime_error(\"thread() 第一个参数必须是函数\");\n");
                        fprintf(out, "        Value (*__cf)(Value*, int) = (Value(*)(Value*, int))__fn.v.func.func_obj;\n");
                        if(argc > 1)
                            fprintf(out, "        int __tid = lumin_thread_start_c(__cf, &__stk[__sp - __argc + 1], %d);\n", argc - 1);
                        else
                            fprintf(out, "        int __tid = lumin_thread_start_c(__cf, NULL, 0);\n");
                        fprintf(out, "        __stk[__sp - __argc] = lumin_make_int(__tid);\n");
                        fprintf(out, "        __sp = __sp - __argc + 1;\n");
                        fprintf(out, "    }\n");
                        break;
                    }
                    case BUILTIN_THREAD_JOIN: {
                        fprintf(out, "    { Value __idv = __stk[--__sp]; if(__idv.type != VAL_INT) runtime_error(\"thread_join() 参数必须是线程id（整数）\"); __stk[__sp++] = lumin_thread_join((int)__idv.v.i); }\n");
                        break;
                    }
                    case BUILTIN_MUTEX:    fprintf(out, "    __stk[__sp++] = lumin_make_int(lumin_mutex_create());\n"); break;
                    case BUILTIN_RMUTEX:   fprintf(out, "    __stk[__sp++] = lumin_make_int(lumin_rmutex_create());\n"); break;
                    case BUILTIN_RWLOCK:   fprintf(out, "    __stk[__sp++] = lumin_make_int(lumin_rwlock_create());\n"); break;
                    case BUILTIN_SPINLOCK: fprintf(out, "    __stk[__sp++] = lumin_make_int(lumin_spinlock_create());\n"); break;
                    case BUILTIN_LOCK:
                        fprintf(out, "    { Value __v = __stk[--__sp]; if(__v.type != VAL_INT) runtime_error(\"lock() 参数必须是锁id（整数）\"); lumin_lock((int)__v.v.i); __stk[__sp++] = __v; }\n");
                        break;
                    case BUILTIN_UNLOCK:
                        fprintf(out, "    { Value __v = __stk[--__sp]; if(__v.type != VAL_INT) runtime_error(\"unlock() 参数必须是锁id（整数）\"); lumin_unlock((int)__v.v.i); __stk[__sp++] = __v; }\n");
                        break;
                    case BUILTIN_TRYLOCK:
                        fprintf(out, "    { Value __v = __stk[--__sp]; if(__v.type != VAL_INT) runtime_error(\"trylock() 参数必须是锁id（整数）\"); __stk[__sp++] = lumin_make_bool(lumin_trylock((int)__v.v.i)); }\n");
                        break;
                    case BUILTIN_RDLOCK:
                        fprintf(out, "    { Value __v = __stk[--__sp]; if(__v.type != VAL_INT) runtime_error(\"rdlock() 参数必须是锁id（整数）\"); lumin_rdlock((int)__v.v.i); __stk[__sp++] = __v; }\n");
                        break;
                    case BUILTIN_WRLOCK:
                        fprintf(out, "    { Value __v = __stk[--__sp]; if(__v.type != VAL_INT) runtime_error(\"wrlock() 参数必须是锁id（整数）\"); lumin_wrlock((int)__v.v.i); __stk[__sp++] = __v; }\n");
                        break;
                    case BUILTIN_TRYRDLOCK:
                        fprintf(out, "    { Value __v = __stk[--__sp]; if(__v.type != VAL_INT) runtime_error(\"tryrdlock() 参数必须是锁id（整数）\"); __stk[__sp++] = lumin_make_bool(lumin_tryrdlock((int)__v.v.i)); }\n");
                        break;
                    case BUILTIN_TRYWRLOCK:
                        fprintf(out, "    { Value __v = __stk[--__sp]; if(__v.type != VAL_INT) runtime_error(\"trywrlock() 参数必须是锁id（整数）\"); __stk[__sp++] = lumin_make_bool(lumin_trywrlock((int)__v.v.i)); }\n");
                        break;
                    case BUILTIN_CONDVAR:
                        fprintf(out, "    __stk[__sp++] = lumin_make_int(lumin_condvar_create());\n");
                        break;
                    case BUILTIN_COND_WAIT:
                        fprintf(out, "    { Value __lk = __stk[--__sp]; Value __cd = __stk[--__sp]; if(__lk.type != VAL_INT) runtime_error(\"cond_wait() 锁参数必须是锁id（整数）\"); if(__cd.type != VAL_INT) runtime_error(\"cond_wait() 条件参数必须是条件id（整数）\"); lumin_cond_wait((int)__cd.v.i, (int)__lk.v.i); __stk[__sp++] = __lk; }\n");
                        break;
                    case BUILTIN_COND_SIGNAL:
                        fprintf(out, "    { Value __v = __stk[--__sp]; if(__v.type != VAL_INT) runtime_error(\"cond_signal() 参数必须是条件id（整数）\"); lumin_cond_signal((int)__v.v.i); __stk[__sp++] = __v; }\n");
                        break;
                    case BUILTIN_COND_BROADCAST:
                        fprintf(out, "    { Value __v = __stk[--__sp]; if(__v.type != VAL_INT) runtime_error(\"cond_broadcast() 参数必须是条件id（整数）\"); lumin_cond_broadcast((int)__v.v.i); __stk[__sp++] = __v; }\n");
                        break;
                    case BUILTIN_COND_TIMEDWAIT:
                        fprintf(out, "    { Value __ms = __stk[--__sp]; Value __lk = __stk[--__sp]; Value __cd = __stk[--__sp]; if(__ms.type != VAL_INT) runtime_error(\"cond_wait_timeout() 超时参数必须是整数毫秒\"); if(__lk.type != VAL_INT) runtime_error(\"cond_wait_timeout() 锁参数必须是锁id（整数）\"); if(__cd.type != VAL_INT) runtime_error(\"cond_wait_timeout() 条件参数必须是条件id（整数）\"); __stk[__sp++] = lumin_make_bool(lumin_cond_timedwait((int)__cd.v.i, (int)__lk.v.i, __ms.v.i)); }\n");
                        break;
                    case BUILTIN_THREADLOCAL_GET:
                        fprintf(out, "    { Value __n = __stk[--__sp]; if(__n.type != VAL_STRING) runtime_error(\"threadlocal_get() 名字参数必须是字符串\"); __stk[__sp++] = lumin_tls_get(lumin_str_cstr(&__n)); }\n");
                        break;
                    case BUILTIN_THREADLOCAL_SET:
                        fprintf(out, "    { Value __v = __stk[--__sp]; Value __n = __stk[--__sp]; if(__n.type != VAL_STRING) runtime_error(\"threadlocal_set() 名字参数必须是字符串\"); lumin_tls_set(lumin_str_cstr(&__n), __v); __stk[__sp++] = __v; }\n");
                        break;
                    case BUILTIN_HTTP_GET:
                    case BUILTIN_HTTP_POST:
                    case BUILTIN_HTTP_PUT:
                    case BUILTIN_ARRAY_ADD:
                        if(in.b == 3)
                            fprintf(out, "    { Value __v = __stk[--__sp]; Value __k = __stk[--__sp]; Value __m = __stk[--__sp]; __stk[__sp++] = lumin_map_add(__m, __k, __v); }\n");
                        else
                            fprintf(out, "    { Value __v = __stk[--__sp]; lumin_array_add(&__stk[__sp-1], __v); }\n");
                        break;
                    case BUILTIN_ARRAY_REMOVE:
                        fprintf(out, "    { Value __idx = __stk[--__sp]; lumin_del(&__stk[__sp-1], __idx); }\n");
                        break;
                    case BUILTIN_ARRAY_INDEXOF:
                        fprintf(out, "    { Value __x = __stk[--__sp]; Value __arr = __stk[--__sp]; __stk[__sp++] = lumin_index_of(__arr, __x); }\n");
                        break;
                    case BUILTIN_ARRAY_GET:
                        fprintf(out, "    { Value __i = __stk[--__sp]; Value __arr = __stk[--__sp]; __stk[__sp++] = lumin_array_get_safe(__arr, __i); }\n");
                        break;
                    case BUILTIN_ARRAY_SET:
                        fprintf(out, "    { Value __v = __stk[--__sp]; Value __i = __stk[--__sp]; Value __arr = __stk[--__sp]; __stk[__sp++] = lumin_array_set_method(__arr, __i, __v); }\n");
                        break;
                    case BUILTIN_ARRAY_FIRST:
                        fprintf(out, "    { Value __arr = __stk[--__sp]; __stk[__sp++] = lumin_array_first(__arr); }\n");
                        break;
                    case BUILTIN_ARRAY_LAST:
                        fprintf(out, "    { Value __arr = __stk[--__sp]; __stk[__sp++] = lumin_array_last(__arr); }\n");
                        break;
                    case BUILTIN_ARRAY_CLEAR:
                        fprintf(out, "    { lumin_array_clear(&__stk[__sp-1]); }\n");
                        break;
                    case BUILTIN_MAP_HAS:
                        fprintf(out, "    { Value __k = __stk[--__sp]; Value __m = __stk[--__sp]; __stk[__sp++] = lumin_make_bool(lumin_map_has(__m, __k)); }\n");
                        break;
                    case BUILTIN_JSON:
                        if(in.b >= 2)
                            fprintf(out, "    { Value __e = __stk[--__sp]; Value __v = __stk[--__sp]; __stk[__sp++] = lumin_json_parse_enc(lumin_str_cstr(&__v), __e); }\n");
                        else
                            fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_json_parse_enc(lumin_str_cstr(&__v), val_none()); }\n");
                        break;
                    case BUILTIN_STRINGIFY:
                        if(in.b >= 2)
                            fprintf(out, "    { Value __e = __stk[--__sp]; Value __v = __stk[--__sp]; char* __js = lumin_json_stringify_enc(__v, __e); Value __r = lumin_make_string(__js); free(__js); __stk[__sp++] = __r; }\n");
                        else
                            fprintf(out, "    { Value __v = __stk[--__sp]; char* __js = lumin_json_stringify_enc(__v, val_none()); Value __r = lumin_make_string(__js); free(__js); __stk[__sp++] = __r; }\n");
                        break;
                    case BUILTIN_ARRAY_FLAT:
                        if(in.b >= 2)
                            fprintf(out, "    { Value __d = __stk[--__sp]; Value __v = __stk[--__sp]; __stk[__sp++] = lumin_array_flat(__v, lumin_extract_int(__d)); }\n");
                        else
                            fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_array_flat(__v, 1); }\n");
                        break;
                    case BUILTIN_QS:
                        if(in.b >= 2)
                            fprintf(out, "    { Value __e = __stk[--__sp]; Value __v = __stk[--__sp]; if(__v.type == VAL_MAP || __v.type == VAL_ARRAY) { char* __q = lumin_qs_stringify_enc(__v, __e); __stk[__sp++] = lumin_make_string(__q); free(__q); } else if(__v.type == VAL_STRING) { __stk[__sp++] = lumin_qs_parse_enc(lumin_str_cstr(&__v), __e); } else runtime_error(\"qs() 参数必须是字典/数组（序列化）或字符串（解析）\"); }\n");
                        else
                            fprintf(out, "    { Value __v = __stk[--__sp]; if(__v.type == VAL_MAP || __v.type == VAL_ARRAY) { char* __q = lumin_qs_stringify_enc(__v, val_none()); __stk[__sp++] = lumin_make_string(__q); free(__q); } else if(__v.type == VAL_STRING) { __stk[__sp++] = lumin_qs_parse_enc(lumin_str_cstr(&__v), val_none()); } else runtime_error(\"qs() 参数必须是字典/数组（序列化）或字符串（解析）\"); }\n");
                        break;
                    case BUILTIN_ARRAY_ADDALL:
                        fprintf(out, "    { Value __b = __stk[--__sp]; lumin_array_addall(&__stk[__sp-1], __b); }\n");
                        break;
                    case BUILTIN_BYTES:
                        if(in.b >= 2)
                            fprintf(out, "    { Value __e = __stk[--__sp]; Value __v = __stk[--__sp]; __stk[__sp++] = lumin_to_bytes(__v, __e); }\n");
                        else
                            fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_to_bytes(__v, val_none()); }\n");
                        break;
                    case BUILTIN_STR:
                        if(in.b >= 2)
                            fprintf(out, "    { Value __e = __stk[--__sp]; Value __v = __stk[--__sp]; __stk[__sp++] = lumin_from_bytes(__v, __e); }\n");
                        else
                            fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_from_bytes(__v, val_none()); }\n");
                        break;
                    case BUILTIN_ENCODE:
                        if(in.b >= 2)
                            fprintf(out, "    { Value __e = __stk[--__sp]; Value __v = __stk[--__sp]; __stk[__sp++] = lumin_to_bytes(__v, __e); }\n");
                        else
                            fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_to_bytes(__v, val_none()); }\n");
                        break;
                    case BUILTIN_DECODE:
                        if(in.b >= 2)
                            fprintf(out, "    { Value __e = __stk[--__sp]; Value __v = __stk[--__sp]; __stk[__sp++] = lumin_from_bytes(__v, __e); }\n");
                        else
                            fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_from_bytes(__v, val_none()); }\n");
                        break;
                    case BUILTIN_ENCODE_URL:
                        fprintf(out, "    { Value __v = __stk[--__sp]; char* __r = lumin_url_encode(__v.type==VAL_STRING?(lumin_str_cstr(&__v)?lumin_str_cstr(&__v):\"\"):\"\"); __stk[__sp++] = lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_DECODE_URL:
                        fprintf(out, "    { Value __v = __stk[--__sp]; char* __r = lumin_url_decode(__v.type==VAL_STRING?(lumin_str_cstr(&__v)?lumin_str_cstr(&__v):\"\"):\"\"); __stk[__sp++] = lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_MD5:
                        fprintf(out, "    { Value __v = __stk[--__sp]; const char* __i = __v.type==VAL_STRING?(lumin_str_cstr(&__v)?lumin_str_cstr(&__v):\"\"):\"\"; char* __r = lumin_md5_hex(__i, (int)strlen(__i)); __stk[__sp++] = lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_ENCODE_BASE64:
                        fprintf(out, "    { Value __v = __stk[--__sp]; const char* __i = __v.type==VAL_STRING?(lumin_str_cstr(&__v)?lumin_str_cstr(&__v):\"\"):\"\"; char* __r = lumin_base64_encode(__i, (int)strlen(__i)); __stk[__sp++] = lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_DECODE_BASE64:
                        fprintf(out, "    { Value __v = __stk[--__sp]; int __ol=0; char* __r = lumin_base64_decode(__v.type==VAL_STRING?(lumin_str_cstr(&__v)?lumin_str_cstr(&__v):\"\"):\"\", &__ol); __stk[__sp++] = lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_REGEX_MATCH:
                        fprintf(out, "    { Value __p=__stk[--__sp]; Value __s=__stk[--__sp]; __stk[__sp++]=lumin_make_bool(lumin_regex_match(__s.type==VAL_STRING?(lumin_str_cstr(&__s)?lumin_str_cstr(&__s):\"\"):\"\", __p.type==VAL_STRING?(lumin_str_cstr(&__p)?lumin_str_cstr(&__p):\"\"):\"\")); }\n");
                        break;
                    case BUILTIN_REGEX_SEARCH:
                        fprintf(out, "    { Value __p=__stk[--__sp]; Value __s=__stk[--__sp]; __stk[__sp++]=lumin_regex_search(__s.type==VAL_STRING?(lumin_str_cstr(&__s)?lumin_str_cstr(&__s):\"\"):\"\", __p.type==VAL_STRING?(lumin_str_cstr(&__p)?lumin_str_cstr(&__p):\"\"):\"\"); }\n");
                        break;
                    case BUILTIN_REGEX_REPLACE:
                        fprintf(out, "    { Value __r=__stk[--__sp]; Value __p=__stk[--__sp]; Value __s=__stk[--__sp]; char* __o=lumin_regex_replace(__s.type==VAL_STRING?(lumin_str_cstr(&__s)?lumin_str_cstr(&__s):\"\"):\"\", __p.type==VAL_STRING?(lumin_str_cstr(&__p)?lumin_str_cstr(&__p):\"\"):\"\", __r.type==VAL_STRING?(lumin_str_cstr(&__r)?lumin_str_cstr(&__r):\"\"):\"\"); __stk[__sp++]=lumin_make_string(__o); free(__o); }\n");
                        break;
                    case BUILTIN_NOW:
                        fprintf(out, "    __stk[__sp++] = lumin_now();\n");
                        break;
                    case BUILTIN_TIMESTAMP:
                        fprintf(out, "    __stk[__sp++] = lumin_make_double(lumin_timestamp());\n");
                        break;
                    case BUILTIN_TIMESTAMP_MS:
                        fprintf(out, "    __stk[__sp++] = lumin_make_int(lumin_timestamp_ms());\n");
                        break;
                    case BUILTIN_SLEEP:
                        fprintf(out, "    { Value __v=__stk[--__sp]; lumin_sleep_ms((long long)lumin_extract_int(__v)); __stk[__sp++]=val_none(); }\n");
                        break;
                    case BUILTIN_DATE:
                        fprintf(out, "    { char* __r=lumin_date_str(); __stk[__sp++]=lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_TIME:
                        fprintf(out, "    { char* __r=lumin_time_str(); __stk[__sp++]=lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_DATETIME:
                        fprintf(out, "    { char* __r=lumin_datetime_str(); __stk[__sp++]=lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_FORMAT_TIME:
                        if(in.b >= 2)
                            fprintf(out, "    { Value __t=__stk[--__sp]; Value __f=__stk[--__sp]; double __ts=__t.type==VAL_DOUBLE?__t.v.d:(double)lumin_extract_int(__t); char* __r=lumin_format_time(__f.type==VAL_STRING?(lumin_str_cstr(&__f)?lumin_str_cstr(&__f):\"\"):\"\", __ts); __stk[__sp++]=lumin_make_string(__r); free(__r); }\n");
                        else
                            fprintf(out, "    { Value __f=__stk[--__sp]; char* __r=lumin_format_time(__f.type==VAL_STRING?(lumin_str_cstr(&__f)?lumin_str_cstr(&__f):\"\"):\"\", -1.0); __stk[__sp++]=lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_LOG_DEBUG:
                    case BUILTIN_LOG_INFO:
                    case BUILTIN_LOG_WARN:
                    case BUILTIN_LOG_ERROR:
                    case BUILTIN_LOG_FATAL:
                        if(in.b >= 2)
                            fprintf(out, "    { Value __m=__stk[--__sp]; __stk[--__sp]; char* __s=value_to_str(__m); lumin_log(%d, __s); free(__s); __stk[__sp++]=val_none(); }\n", in.a - BUILTIN_LOG_DEBUG);
                        else
                            fprintf(out, "    { Value __m=__stk[--__sp]; char* __s=value_to_str(__m); lumin_log(%d, __s); free(__s); __stk[__sp++]=val_none(); }\n", in.a - BUILTIN_LOG_DEBUG);
                        break;
                    case BUILTIN_GC_COUNT:
                        fprintf(out, "    __stk[__sp++] = lumin_make_int((long long)gc_count());\n");
                        break;
                    case BUILTIN_GC_BYTES:
                        fprintf(out, "    __stk[__sp++] = lumin_make_int((long long)gc_bytes());\n");
                        break;
                    case BUILTIN_GC_COLLECT:
                        fprintf(out, "    { gc_collect_now(); __stk[__sp++] = val_none(); }\n");
                        break;
                    case BUILTIN_HTTP_DELETE:
                    case BUILTIN_HTTP_HEAD:
                    case BUILTIN_HTTP_PATCH: {
                        const char* m = "GET";
                        switch(in.a) {
                            case BUILTIN_HTTP_POST:   m = "POST"; break;
                            case BUILTIN_HTTP_PUT:    m = "PUT"; break;
                            case BUILTIN_ARRAY_ADD:
                        if(in.b == 3)
                            fprintf(out, "    { Value __v = __stk[--__sp]; Value __k = __stk[--__sp]; Value __m = __stk[--__sp]; __stk[__sp++] = lumin_map_add(__m, __k, __v); }\n");
                        else
                            fprintf(out, "    { Value __v = __stk[--__sp]; lumin_array_add(&__stk[__sp-1], __v); }\n");
                        break;
                    case BUILTIN_ARRAY_REMOVE:
                        fprintf(out, "    { Value __idx = __stk[--__sp]; lumin_del(&__stk[__sp-1], __idx); }\n");
                        break;
                    case BUILTIN_ARRAY_INDEXOF:
                        fprintf(out, "    { Value __x = __stk[--__sp]; Value __arr = __stk[--__sp]; __stk[__sp++] = lumin_index_of(__arr, __x); }\n");
                        break;
                    case BUILTIN_ARRAY_GET:
                        fprintf(out, "    { Value __i = __stk[--__sp]; Value __arr = __stk[--__sp]; __stk[__sp++] = lumin_array_get_safe(__arr, __i); }\n");
                        break;
                    case BUILTIN_ARRAY_SET:
                        fprintf(out, "    { Value __v = __stk[--__sp]; Value __i = __stk[--__sp]; Value __arr = __stk[--__sp]; __stk[__sp++] = lumin_array_set_method(__arr, __i, __v); }\n");
                        break;
                    case BUILTIN_ARRAY_FIRST:
                        fprintf(out, "    { Value __arr = __stk[--__sp]; __stk[__sp++] = lumin_array_first(__arr); }\n");
                        break;
                    case BUILTIN_ARRAY_LAST:
                        fprintf(out, "    { Value __arr = __stk[--__sp]; __stk[__sp++] = lumin_array_last(__arr); }\n");
                        break;
                    case BUILTIN_ARRAY_CLEAR:
                        fprintf(out, "    { lumin_array_clear(&__stk[__sp-1]); }\n");
                        break;
                    case BUILTIN_MAP_HAS:
                        fprintf(out, "    { Value __k = __stk[--__sp]; Value __m = __stk[--__sp]; __stk[__sp++] = lumin_make_bool(lumin_map_has(__m, __k)); }\n");
                        break;
                    case BUILTIN_JSON:
                        if(in.b >= 2)
                            fprintf(out, "    { Value __e = __stk[--__sp]; Value __v = __stk[--__sp]; __stk[__sp++] = lumin_json_parse_enc(lumin_str_cstr(&__v), __e); }\n");
                        else
                            fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_json_parse_enc(lumin_str_cstr(&__v), val_none()); }\n");
                        break;
                    case BUILTIN_STRINGIFY:
                        if(in.b >= 2)
                            fprintf(out, "    { Value __e = __stk[--__sp]; Value __v = __stk[--__sp]; char* __js = lumin_json_stringify_enc(__v, __e); Value __r = lumin_make_string(__js); free(__js); __stk[__sp++] = __r; }\n");
                        else
                            fprintf(out, "    { Value __v = __stk[--__sp]; char* __js = lumin_json_stringify_enc(__v, val_none()); Value __r = lumin_make_string(__js); free(__js); __stk[__sp++] = __r; }\n");
                        break;
                    case BUILTIN_ARRAY_FLAT:
                        if(in.b >= 2)
                            fprintf(out, "    { Value __d = __stk[--__sp]; Value __v = __stk[--__sp]; __stk[__sp++] = lumin_array_flat(__v, lumin_extract_int(__d)); }\n");
                        else
                            fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_array_flat(__v, 1); }\n");
                        break;
                    case BUILTIN_QS:
                        if(in.b >= 2)
                            fprintf(out, "    { Value __e = __stk[--__sp]; Value __v = __stk[--__sp]; if(__v.type == VAL_MAP || __v.type == VAL_ARRAY) { char* __q = lumin_qs_stringify_enc(__v, __e); __stk[__sp++] = lumin_make_string(__q); free(__q); } else if(__v.type == VAL_STRING) { __stk[__sp++] = lumin_qs_parse_enc(lumin_str_cstr(&__v), __e); } else runtime_error(\"qs() 参数必须是字典/数组（序列化）或字符串（解析）\"); }\n");
                        else
                            fprintf(out, "    { Value __v = __stk[--__sp]; if(__v.type == VAL_MAP || __v.type == VAL_ARRAY) { char* __q = lumin_qs_stringify_enc(__v, val_none()); __stk[__sp++] = lumin_make_string(__q); free(__q); } else if(__v.type == VAL_STRING) { __stk[__sp++] = lumin_qs_parse_enc(lumin_str_cstr(&__v), val_none()); } else runtime_error(\"qs() 参数必须是字典/数组（序列化）或字符串（解析）\"); }\n");
                        break;
                    case BUILTIN_ARRAY_ADDALL:
                        fprintf(out, "    { Value __b = __stk[--__sp]; lumin_array_addall(&__stk[__sp-1], __b); }\n");
                        break;
                    case BUILTIN_BYTES:
                        if(in.b >= 2)
                            fprintf(out, "    { Value __e = __stk[--__sp]; Value __v = __stk[--__sp]; __stk[__sp++] = lumin_to_bytes(__v, __e); }\n");
                        else
                            fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_to_bytes(__v, val_none()); }\n");
                        break;
                    case BUILTIN_STR:
                        if(in.b >= 2)
                            fprintf(out, "    { Value __e = __stk[--__sp]; Value __v = __stk[--__sp]; __stk[__sp++] = lumin_from_bytes(__v, __e); }\n");
                        else
                            fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_from_bytes(__v, val_none()); }\n");
                        break;
                    case BUILTIN_ENCODE:
                        if(in.b >= 2)
                            fprintf(out, "    { Value __e = __stk[--__sp]; Value __v = __stk[--__sp]; __stk[__sp++] = lumin_to_bytes(__v, __e); }\n");
                        else
                            fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_to_bytes(__v, val_none()); }\n");
                        break;
                    case BUILTIN_DECODE:
                        if(in.b >= 2)
                            fprintf(out, "    { Value __e = __stk[--__sp]; Value __v = __stk[--__sp]; __stk[__sp++] = lumin_from_bytes(__v, __e); }\n");
                        else
                            fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_from_bytes(__v, val_none()); }\n");
                        break;
                    case BUILTIN_ENCODE_URL:
                        fprintf(out, "    { Value __v = __stk[--__sp]; char* __r = lumin_url_encode(__v.type==VAL_STRING?(lumin_str_cstr(&__v)?lumin_str_cstr(&__v):\"\"):\"\"); __stk[__sp++] = lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_DECODE_URL:
                        fprintf(out, "    { Value __v = __stk[--__sp]; char* __r = lumin_url_decode(__v.type==VAL_STRING?(lumin_str_cstr(&__v)?lumin_str_cstr(&__v):\"\"):\"\"); __stk[__sp++] = lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_MD5:
                        fprintf(out, "    { Value __v = __stk[--__sp]; const char* __i = __v.type==VAL_STRING?(lumin_str_cstr(&__v)?lumin_str_cstr(&__v):\"\"):\"\"; char* __r = lumin_md5_hex(__i, (int)strlen(__i)); __stk[__sp++] = lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_ENCODE_BASE64:
                        fprintf(out, "    { Value __v = __stk[--__sp]; const char* __i = __v.type==VAL_STRING?(lumin_str_cstr(&__v)?lumin_str_cstr(&__v):\"\"):\"\"; char* __r = lumin_base64_encode(__i, (int)strlen(__i)); __stk[__sp++] = lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_DECODE_BASE64:
                        fprintf(out, "    { Value __v = __stk[--__sp]; int __ol=0; char* __r = lumin_base64_decode(__v.type==VAL_STRING?(lumin_str_cstr(&__v)?lumin_str_cstr(&__v):\"\"):\"\", &__ol); __stk[__sp++] = lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_REGEX_MATCH:
                        fprintf(out, "    { Value __p=__stk[--__sp]; Value __s=__stk[--__sp]; __stk[__sp++]=lumin_make_bool(lumin_regex_match(__s.type==VAL_STRING?(lumin_str_cstr(&__s)?lumin_str_cstr(&__s):\"\"):\"\", __p.type==VAL_STRING?(lumin_str_cstr(&__p)?lumin_str_cstr(&__p):\"\"):\"\")); }\n");
                        break;
                    case BUILTIN_REGEX_SEARCH:
                        fprintf(out, "    { Value __p=__stk[--__sp]; Value __s=__stk[--__sp]; __stk[__sp++]=lumin_regex_search(__s.type==VAL_STRING?(lumin_str_cstr(&__s)?lumin_str_cstr(&__s):\"\"):\"\", __p.type==VAL_STRING?(lumin_str_cstr(&__p)?lumin_str_cstr(&__p):\"\"):\"\"); }\n");
                        break;
                    case BUILTIN_REGEX_REPLACE:
                        fprintf(out, "    { Value __r=__stk[--__sp]; Value __p=__stk[--__sp]; Value __s=__stk[--__sp]; char* __o=lumin_regex_replace(__s.type==VAL_STRING?(lumin_str_cstr(&__s)?lumin_str_cstr(&__s):\"\"):\"\", __p.type==VAL_STRING?(lumin_str_cstr(&__p)?lumin_str_cstr(&__p):\"\"):\"\", __r.type==VAL_STRING?(lumin_str_cstr(&__r)?lumin_str_cstr(&__r):\"\"):\"\"); __stk[__sp++]=lumin_make_string(__o); free(__o); }\n");
                        break;
                    case BUILTIN_NOW:
                        fprintf(out, "    __stk[__sp++] = lumin_now();\n");
                        break;
                    case BUILTIN_TIMESTAMP:
                        fprintf(out, "    __stk[__sp++] = lumin_make_double(lumin_timestamp());\n");
                        break;
                    case BUILTIN_TIMESTAMP_MS:
                        fprintf(out, "    __stk[__sp++] = lumin_make_int(lumin_timestamp_ms());\n");
                        break;
                    case BUILTIN_SLEEP:
                        fprintf(out, "    { Value __v=__stk[--__sp]; lumin_sleep_ms((long long)lumin_extract_int(__v)); __stk[__sp++]=val_none(); }\n");
                        break;
                    case BUILTIN_DATE:
                        fprintf(out, "    { char* __r=lumin_date_str(); __stk[__sp++]=lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_TIME:
                        fprintf(out, "    { char* __r=lumin_time_str(); __stk[__sp++]=lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_DATETIME:
                        fprintf(out, "    { char* __r=lumin_datetime_str(); __stk[__sp++]=lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_FORMAT_TIME:
                        if(in.b >= 2)
                            fprintf(out, "    { Value __t=__stk[--__sp]; Value __f=__stk[--__sp]; double __ts=__t.type==VAL_DOUBLE?__t.v.d:(double)lumin_extract_int(__t); char* __r=lumin_format_time(__f.type==VAL_STRING?(lumin_str_cstr(&__f)?lumin_str_cstr(&__f):\"\"):\"\", __ts); __stk[__sp++]=lumin_make_string(__r); free(__r); }\n");
                        else
                            fprintf(out, "    { Value __f=__stk[--__sp]; char* __r=lumin_format_time(__f.type==VAL_STRING?(lumin_str_cstr(&__f)?lumin_str_cstr(&__f):\"\"):\"\", -1.0); __stk[__sp++]=lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_LOG_DEBUG:
                    case BUILTIN_LOG_INFO:
                    case BUILTIN_LOG_WARN:
                    case BUILTIN_LOG_ERROR:
                    case BUILTIN_LOG_FATAL:
                        if(in.b >= 2)
                            fprintf(out, "    { Value __m=__stk[--__sp]; __stk[--__sp]; char* __s=value_to_str(__m); lumin_log(%d, __s); free(__s); __stk[__sp++]=val_none(); }\n", in.a - BUILTIN_LOG_DEBUG);
                        else
                            fprintf(out, "    { Value __m=__stk[--__sp]; char* __s=value_to_str(__m); lumin_log(%d, __s); free(__s); __stk[__sp++]=val_none(); }\n", in.a - BUILTIN_LOG_DEBUG);
                        break;
                    case BUILTIN_HTTP_DELETE: m = "DELETE"; break;
                            case BUILTIN_HTTP_HEAD:   m = "HEAD"; break;
                            case BUILTIN_HTTP_PATCH:  m = "PATCH"; break;
                            default: break;
                        }
                        if(in.b == 1)
                            fprintf(out, "    { Value __r = lumin_http_request(\"%s\", __stk[__sp-1], val_none(), val_none()); __stk[__sp-1] = __r; __sp = __sp - 1 + 1; }\n", m);
                        else if(in.b == 2)
                            fprintf(out, "    { Value __r = lumin_http_request(\"%s\", __stk[__sp-2], __stk[__sp-1], val_none()); __stk[__sp-2] = __r; __sp = __sp - 2 + 1; }\n", m);
                        else
                            fprintf(out, "    { Value __r = lumin_http_request(\"%s\", __stk[__sp-3], __stk[__sp-2], __stk[__sp-1]); __stk[__sp-3] = __r; __sp = __sp - 3 + 1; }\n", m);
                        break;
                    }
                    case BUILTIN_VALUES:
                        fprintf(out, "    { Value __r = lumin_map_values(__stk[__sp - %d]); __stk[__sp - %d] = __r; __sp = __sp - %d + 1; }\n", in.b, in.b, in.b);
                        break;
                    case BUILTIN_MAP:
                    case BUILTIN_FILTER:
                    case BUILTIN_REDUCE: {
                        int argc = in.b;
                        fprintf(out, "    {\n");
                        fprintf(out, "        Value __fn, __arr, __init = val_none();\n");
                        if(argc == 3)
                            fprintf(out, "        __init = __stk[--__sp]; __fn = __stk[--__sp]; __arr = __stk[--__sp];\n");
                        else
                            fprintf(out, "        __fn = __stk[--__sp]; __arr = __stk[--__sp];\n");
                        if(in.a == BUILTIN_MAP) {
                            /* 字典 map：fn(value, key) → 新字典（键不变值映射） */
                            fprintf(out, "        if(__arr.type == VAL_MAP) {\n");
                            fprintf(out, "            if(__fn.type != VAL_FUNC) runtime_error(\"map() 第二个参数必须是函数\");\n");
                            fprintf(out, "            Value (*__cfm)(Value*, int) = (Value(*)(Value*, int))__fn.v.func.func_obj;\n");
                            fprintf(out, "            Value __mout = val_map();\n");
                            fprintf(out, "            MapIter __it; map_iter_init(&__it, __arr.v.map);\n");
                            fprintf(out, "            Value __mk, __mv;\n");
                            fprintf(out, "            while(map_iter_next(&__it, &__mk, &__mv)) {\n");
                            fprintf(out, "                Value __a2[2]; __a2[0] = __mv; __a2[1] = __mk;\n");
                            fprintf(out, "                Value __r = __cfm(__a2, 2);\n");
                            fprintf(out, "                lumin_map_set(&__mout, __mk, __r);\n");
                            fprintf(out, "            }\n");
                            fprintf(out, "            __stk[__sp++] = __mout;\n");
                            fprintf(out, "        } else {\n");
                        }
                        fprintf(out, "        if(__arr.type != VAL_ARRAY) runtime_error(\"map()/filter()/reduce() 第一个参数必须是数组\");\n");
                        fprintf(out, "        if(__fn.type != VAL_FUNC) runtime_error(\"map()/filter()/reduce() 第二个参数必须是函数\");\n");
                        fprintf(out, "        Value (*__cf)(Value*, int) = (Value(*)(Value*, int))__fn.v.func.func_obj;\n");
                        fprintf(out, "        int __n = __arr.v.array->len;\n");
                        if(in.a == BUILTIN_MAP) {
                            fprintf(out, "        Value __out = val_array(__n);\n");
                            fprintf(out, "        for(int __i = 0; __i < __n; __i++) {\n");
                            fprintf(out, "            Value __a1[1]; __a1[0] = __arr.v.array->items[__i];\n");
                            fprintf(out, "            Value __r = __cf(__a1, 1);\n");
                            fprintf(out, "            __out.v.array->items[__i] = __r;\n");
                            fprintf(out, "        }\n");
                            fprintf(out, "        __stk[__sp++] = __out;\n");
                        } else if(in.a == BUILTIN_FILTER) {
                            fprintf(out, "        Value __out = val_array(__n); int __cnt = 0;\n");
                            fprintf(out, "        for(int __i = 0; __i < __n; __i++) {\n");
                            fprintf(out, "            Value __a1[1]; __a1[0] = __arr.v.array->items[__i];\n");
                            fprintf(out, "            Value __r = __cf(__a1, 1);\n");
                            fprintf(out, "            if(lumin_to_bool(__r)) __out.v.array->items[__cnt++] = __arr.v.array->items[__i];\n");
                            fprintf(out, "        }\n");
                            fprintf(out, "        __out.v.array->len = __cnt;\n");
                            fprintf(out, "        __stk[__sp++] = __out;\n");
                        } else {
                            fprintf(out, "        Value __acc = __init;\n");
                            fprintf(out, "        for(int __i = 0; __i < __n; __i++) {\n");
                            fprintf(out, "            Value __a2[2]; __a2[0] = __acc; __a2[1] = __arr.v.array->items[__i];\n");
                            fprintf(out, "            __acc = __cf(__a2, 2);\n");
                            fprintf(out, "        }\n");
                            fprintf(out, "        __stk[__sp++] = __acc;\n");
                        }
                        if(in.a == BUILTIN_MAP) {
                            fprintf(out, "        }\n");  /* 闭合 map 字典分支的 else */
                        }
                        fprintf(out, "    }\n");
                        break;
                    }
                    case BUILTIN_AVG:
                        fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_avg(__v); }\n");
                        break;
                }
                break;
            case OPC_PRINT:
                fprintf(out, "    lumin_print(__stk[__sp-1]);\n");
                break;
            case OPC_TO_BOOL:
                fprintf(out, "    __stk[__sp-1] = lumin_make_bool(lumin_to_bool(__stk[__sp-1]));\n");
                break;
            case OPC_DUP:
                fprintf(out, "    __stk[__sp] = __stk[__sp-1]; __sp++;\n");
                break;
            case OPC_POP:
                fprintf(out, "    __sp--;\n");
                break;
            case OPC_TRY:
                /* C 级错误处理：全局 jmp_buf 栈（longjmp 后自动变量不可靠，索引从 __g_depth 反推）
                   自闭合结构：setjmp 成功 → goto L(body)；失败 → 恢复本层并 goto L(catch) */
                fprintf(out, "    { int __d = __g_depth; __g_ensure(__d + 2); __g_tgt[__d] = %d; __g_sp0[__d] = __sp; __g_prev[__d] = g_err_jmp;\n", in.a);
                fprintf(out, "      __g_tn[__d] = g_trace_n; __g_fn[__d] = __g_fin_n;\n");
                fprintf(out, "      g_err_jmp = &__g_jbs[__d];\n");
                fprintf(out, "      CFrame* __cf_save = gc_cframe_top();\n");
                fprintf(out, "      if(setjmp(__g_jbs[__d]) == 0) { __g_depth = __d + 1; goto L%d; }\n", i + 1);
                fprintf(out, "      int __d2 = __g_depth - 1;\n");
                fprintf(out, "      __sp = __g_sp0[__d2]; __g_depth = __d2; g_err_jmp = __g_prev[__d2];\n");
                fprintf(out, "      gc_cframe_restore(__cf_save);\n");
                fprintf(out, "      goto L%d;\n", in.a);
                fprintf(out, "    }\n");
                fprintf(out, "    L%d:;\n", i + 1);
                break;
            case OPC_ENDTRY:
                /* 无 finally 布局：正常路径恢复外层处理器（setjmp 结构已在 TRY 处闭合） */
                fprintf(out, "    if(__g_depth > 0) { __g_depth--; g_err_jmp = __g_prev[__g_depth]; }\n");
                break;
            case OPC_GET_ERR: {
                /* 错误对象：type/message + 调用栈回溯；随后截断残留到本 TRY 层 */
                fprintf(out, "    { char* __st = lumin_build_stack_trace();\n");
                fprintf(out, "      __stk[__sp++] = lumin_make_error(g_err_type, g_err_msg, __st);\n");
                fprintf(out, "      free(__st);\n");
                fprintf(out, "      g_trace_n = __g_tn[__g_depth]; __g_fin_n = __g_fn[__g_depth];\n");
                fprintf(out, "    }\n");
                break;
            }
            case OPC_THROW:
                /* throw：包装成错误对象抛出（字符串→type Error；错误对象→原样；map→type/message） */
                fprintf(out, "    { Value __v = __stk[--__sp];\n");
                fprintf(out, "      const char* __tp = \"Error\"; char* __msg = NULL;\n");
                fprintf(out, "      if(__v.type == VAL_ERROR) { __tp = __v.v.err.type ? __v.v.err.type : \"Error\"; __msg = strdup(__v.v.err.message ? __v.v.err.message : \"\"); }\n");
                fprintf(out, "      else if(__v.type == VAL_MAP) {\n");
                fprintf(out, "        if(lumin_map_has(__v, lumin_make_string(\"type\"))) { Value __tv = lumin_map_get(__v, lumin_make_string(\"type\")); if(__tv.type == VAL_STRING) __tp = lumin_str_cstr(&__tv); }\n");
                fprintf(out, "        if(lumin_map_has(__v, lumin_make_string(\"message\"))) { Value __mv = lumin_map_get(__v, lumin_make_string(\"message\")); if(__mv.type == VAL_STRING) __msg = strdup(lumin_str_cstr(&__mv)); }\n");
                fprintf(out, "      }\n");
                fprintf(out, "      if(!__msg) __msg = value_to_str(__v);\n");
                fprintf(out, "      g_err_type_set(__tp);\n");
                fprintf(out, "      g_err_msg_set(__msg);\n");
                fprintf(out, "      free(__msg);\n");
                fprintf(out, "      if(g_err_jmp) longjmp(*g_err_jmp, 1);\n");
                fprintf(out, "      fprintf(stderr, \"Runtime Error: %%s\\n\", g_err_msg); exit(EXIT_FAILURE);\n");
                fprintf(out, "    }\n");
                break;
            case OPC_FIN_PUSH: {
                /* finally 完成动作：1=JMP 2=RETHROW 3=BREAK 4=CONT 5=RETURN（b=目标 pc→label 编号） */
                int fidx = in.b ? fin_lab_idx_of(in.b) : 0;
                fprintf(out, "    __g_ensure(__g_fin_n + 2); __g_fin_act[__g_fin_n] = %d; __g_fin_tgt[__g_fin_n] = %d; __g_fin_dep[__g_fin_n] = __g_depth - 1; __g_fin_n++;\n", in.a, fidx);
                break;
            }
            case OPC_FINISH:
                /* 完成动作的目标在运行时才知道（__g_fin_tgt 存的是 label 编号），用跳转表 */
                fprintf(out, "    if(__g_fin_n <= 0) runtime_error(\"finally 完成栈为空\");\n");
                fprintf(out, "    { int __fa = __g_fin_act[--__g_fin_n];\n");
                fprintf(out, "      if(__fa == 1 || __fa == 3 || __fa == 4) { __g_depth = __g_fin_dep[__g_fin_n]; g_err_jmp = __g_prev[__g_fin_dep[__g_fin_n]]; goto *__g_fin_labs[__g_fin_tgt[__g_fin_n]]; }\n");
                fprintf(out, "      else if(__fa == 2) { if(g_err_jmp) longjmp(*g_err_jmp, 1); fprintf(stderr, \"Runtime Error: %%s\\n\", g_err_msg); exit(EXIT_FAILURE); }\n");
                if(!g_cur_fn)
                    fprintf(out, "      else if(__fa == 5) { __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0; gc_pop_cframe(); return 0; }\n");
                else
                    fprintf(out, "      else if(__fa == 5) { __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0; if(g_trace_n > 0) g_trace_n--; gc_pop_cframe(); { Value __v = __g_pend_val; return __v; } }\n");
                fprintf(out, "      else runtime_error(\"finally 完成动作未知\");\n");
                fprintf(out, "    }\n");
                break;
            case OPC_PEND_RETURN:
                /* 挂起返回：弹1存 __g_pend_val → 压 RETURN 动作 → 跳 finally */
                fprintf(out, "    __g_pend_val = __stk[--__sp];\n");
                fprintf(out, "    __g_fin_act[__g_fin_n] = 5; __g_fin_tgt[__g_fin_n] = 0; __g_fin_n++;\n");
                if(in.b) fprintf(out, "    goto L%d;\n", in.b);
                break;
            case OPC_JMP:
                /* 循环回边（向后跳转）插入 STW 安全点：编译通道无解释循环安全点，
                 * 长循环中需主动检查 GC 是否运行，避免标记期间并发修改栈值 */
                if(in.a < i) fprintf(out, "    gc_stw_check();\n");
                fprintf(out, "    goto L%d;\n", in.a);
                break;
            case OPC_JMP_IF_FALSE:
                fprintf(out, "    if (!lumin_to_bool(__stk[--__sp])) goto L%d;\n", in.a);
                break;
            case OPC_JMP_IF_TRUE:
                fprintf(out, "    if (lumin_to_bool(__stk[--__sp])) goto L%d;\n", in.a);
                break;
            case OPC_CALL: {
                /* STW 安全点：函数调用前检查 GC，避免参数弹出期间并发标记读到 torn Value */
                fprintf(out, "    gc_stw_check();\n");
                /* 帧链优先（VM 语义）：名字是局部/全局变量时按函数值动态调用，
                   与具名全局函数冲突时以变量为准（局部闭包遮蔽全局函数） */
                int is_var = (g_cur_fn && (fn_has_param(g_cur_fn, nm) || ns_has(&fn_locals, nm))) ||
                             ns_has(&g_globals, nm);
                if(is_var) {
                    int argc = in.b;
                    fprintf(out, "    {\n");
                    fprintf(out, "        Value __f = %s;\n", cvar(nm));
                    fprintf(out, "        if(__f.type != VAL_FUNC) runtime_error(\"尝试调用非函数: %s\");\n", nm);
                    fprintf(out, "        int __argc = %d;\n", argc);
                    fprintf(out, "        Value __args[%d];\n", argc > 0 ? argc : 1);
                    fprintf(out, "        for (int __k = 0; __k < __argc; __k++) __args[__k] = __stk[__sp - __argc + __k];\n");
                    fprintf(out, "        __sp -= __argc;\n");
                    fprintf(out, "        __stk[__sp++] = ((Value(*)(Value*, int))__f.v.func.func_obj)(__args, __argc);\n");
                    fprintf(out, "    }\n");
                    break;
                }
                BytecodeFunc* callee = ir_func_table_lookup(nm);
                if(!callee) {
                    fprintf(stderr, "codegen: 未定义函数: %s\n", nm);
                    exit(EXIT_FAILURE);
                }
                int argc = in.b;
                int fixed = callee->param_cnt;
                int nbind = (argc < fixed) ? argc : fixed;
                int restn = argc - fixed;
                if(restn < 0) restn = 0;
                fprintf(out, "    {\n");
                fprintf(out, "        int __argc = %d;\n", argc);
                fprintf(out, "        Value __args[%d];\n", argc > 0 ? argc : 1);
                fprintf(out, "        for (int __k = 0; __k < __argc; __k++) __args[__k] = __stk[__sp - __argc + __k];\n");
                fprintf(out, "        __sp -= __argc;\n");
                if(callee->has_variadic) {
                    fprintf(out, "        Value __rest = val_array(%d);\n", restn);
                    for(int k = 0; k < restn; k++)
                        fprintf(out, "        __rest.v.array->items[%d] = __args[%d];\n", k, fixed + k);
                    fprintf(out, "        __stk[__sp++] = lumin_func_%s(", nm);
                    for(int k = 0; k < fixed; k++) {
                        if(k) fprintf(out, ", ");
                        if(k < nbind) fprintf(out, "__args[%d]", k);
                        else fprintf(out, "val_none()");
                    }
                    if(fixed > 0) fprintf(out, ", ");
                    fprintf(out, "__rest);\n");
                } else {
                    fprintf(out, "        __stk[__sp++] = lumin_func_%s(", nm);
                    for(int k = 0; k < fixed; k++) {
                        if(k) fprintf(out, ", ");
                        if(k < nbind) fprintf(out, "__args[%d]", k);
                        else fprintf(out, "val_none()");
                    }
                    fprintf(out, ");\n");
                }
                fprintf(out, "    }\n");
                break;
            }
            case OPC_CALLV: {
                /* STW 安全点：函数调用前检查 GC */
                fprintf(out, "    gc_stw_check();\n");
                // 动态调用链 f(1)(2)：栈上函数值调用（wrap 指针签名 Value(*)(Value*, int)）
                int argc = in.b;
                fprintf(out, "    {\n");
                fprintf(out, "        Value __f = __stk[__sp - %d - 1];\n", argc);
                fprintf(out, "        if(__f.type != VAL_FUNC) runtime_error(\"尝试调用非函数值\");\n");
                fprintf(out, "        int __argc = %d;\n", argc);
                fprintf(out, "        Value __args[%d];\n", argc > 0 ? argc : 1);
                fprintf(out, "        for (int __k = 0; __k < __argc; __k++) __args[__k] = __stk[__sp - %d + __k];\n", argc);
                fprintf(out, "        __sp -= %d + 1;\n", argc);
                fprintf(out, "        __stk[__sp++] = ((Value(*)(Value*, int))__f.v.func.func_obj)(__args, __argc);\n");
                fprintf(out, "    }\n");
                break;
            }
            case OPC_RETURN:
                if(g_cur_fn) {
                    fprintf(out, "    __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0;\n");
                    fprintf(out, "    if(g_trace_n > 0) g_trace_n--;\n");
                    fprintf(out, "    gc_pop_cframe();\n");
                    fprintf(out, "    { Value __v = __stk[--__sp]; return __v; }\n");
                } else {
                    fprintf(out, "    gc_pop_cframe();\n");
                    fprintf(out, "    return 0;\n");
                }
                break;
            case OPC_RETURN_NIL:
                if(g_cur_fn) {
                    fprintf(out, "    __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0;\n");
                    fprintf(out, "    if(g_trace_n > 0) g_trace_n--;\n");
                    fprintf(out, "    gc_pop_cframe();\n");
                    fprintf(out, "    return val_none();\n");
                } else {
                    fprintf(out, "    gc_pop_cframe();\n");
                    fprintf(out, "    return 0;\n");
                }
                break;
            case OPC_HALT:
                fprintf(out, "    gc_pop_cframe();\n");
                fprintf(out, "    return 0;\n");
                break;
            default:
                break;
        }
        i++;
    }
}

// ---------------- 逃逸分析（位掩码流敏感） ----------------
/*
 * 设计目标：识别不逃逸的 OPC_ARRAY_LIT，将 ValueArray 结构体分配在 C 栈上。
 *
 * 保守策略（宁可漏优化不可出错）：
 *   1. 超过 64 个数组字面量 → 全部堆分配（位掩码上限）
 *   2. 数组出现在逃逸上下文 → 堆分配
 *      逃逸上下文：return 值、存全局变量、函数调用(OPC_CALL/CALLV)任意参数、
 *                  INDEX_SET 的容器和元素、THREAD/THREADLOCAL_SET 内置函数参数、
 *                  ARRAY_ADD/INSERT/ARRAY_SET 等存入数组的值参数、
 *                  THROW 抛出值、PEND_RETURN 挂起返回值、FINISH 处栈上所有值
 *   3. 循环体内（后向跳转 [target, src] 范围）的数组 → 堆分配
 *      （防止同一栈槽被多次迭代复用导致引用错误）
 *   4. 控制流合并按位 OR，不动点迭代最多 20 轮
 *   5. 不可达指令（栈深 -1）跳过
 *
 * 位掩码：每个 OPC_ARRAY_LIT 分配一个 bit（0..63），栈槽/变量持有 uint64_t 掩码
 *         表示"可能持有哪些数组"。
 */

/* 判断符号下标对应的变量是否为全局变量（非参数、非函数局部） */
static int ea_is_global(BytecodeFunc* fn, int sym_idx)
{
    if(sym_idx < 0 || sym_idx >= fn->sym_cnt) return 1;  /* 未知符号保守视为全局 */
    const char* nm = fn->syms[sym_idx];
    if(!nm) return 1;
    if(g_cur_fn && (fn_has_param(fn, nm) || ns_has(&fn_locals, nm))) return 0;
    return 1;  /* main 中所有变量都是全局 */
}

/* 泛化逃逸分析：对 target_op（OPC_ARRAY_LIT 或 OPC_MAP_LIT）的字面量做逃逸分析。
 * struct_result[i]=1：指令 i 处的 target_op 字面量不逃逸，结构体可栈分配。
 * items_result[i]=1：仅数组（track_items=1），items 缓冲区也可栈分配。
 * 调用方需预分配 struct_result / items_result（大小 fn->code_len，已 zero）。
 * track_items=0 时 items_result 可为 NULL。 */
static void analyze_escape_for(BytecodeFunc* fn, int target_op,
                                uint8_t* struct_result, uint8_t* items_result,
                                int track_items)
{
    int n = fn->code_len;
    int other_op = (target_op == OPC_ARRAY_LIT) ? OPC_MAP_LIT : OPC_ARRAY_LIT;

    /* 1. 为每个 target_op 字面量分配 bit 位 */
    int* lit_bit = (int*)malloc(sizeof(int) * n);
    int lit_cnt = 0;
    for(int i = 0; i < n; i++) {
        lit_bit[i] = -1;
        if(fn->code[i].op == target_op) {
            if(lit_cnt < 64) {
                lit_bit[i] = lit_cnt++;
            } else {
                /* >64 个字面量：保守回退，全部堆分配 */
                free(lit_bit);
                return;
            }
        }
    }
    if(lit_cnt == 0) { free(lit_bit); return; }

    /* 2. 获取每条指令的栈深 */
    int maxd = bc_analyze_stack(fn, NULL, 0);
    int* depths = (int*)malloc(sizeof(int) * n);
    bc_analyze_stack(fn, depths, n);

    /* 3. 每指令入口状态：栈槽掩码 + 变量掩码 */
    int stk_stride = maxd + 2;
    uint64_t* entry_stk = (uint64_t*)calloc((size_t)n * stk_stride, sizeof(uint64_t));
    uint64_t* entry_var = (uint64_t*)calloc((size_t)n * fn->sym_cnt, sizeof(uint64_t));
    uint64_t* tmp_stk = (uint64_t*)malloc(sizeof(uint64_t) * stk_stride);
    uint64_t* tmp_var = (uint64_t*)malloc(sizeof(uint64_t) * (fn->sym_cnt > 0 ? fn->sym_cnt : 1));

    uint64_t escaped = 0;   /* 累积逃逸的位掩码 */
    uint64_t may_grow = 0;  /* 累积可能被扩容的位掩码（仅数组 track_items 时使用） */

    /* 4. 不动点前向传播 */
    for(int iter = 0; iter < 20; iter++) {
        int changed = 0;

        for(int i = 0; i < n; i++) {
            Instruction in = fn->code[i];
            int depth = depths[i];
            if(depth < 0) continue;

            memcpy(tmp_stk, &entry_stk[i * stk_stride], sizeof(uint64_t) * stk_stride);
            memcpy(tmp_var, &entry_var[i * fn->sym_cnt], sizeof(uint64_t) * fn->sym_cnt);

            int sp = depth;
            uint64_t esc = 0;

            switch(in.op) {
                case OPC_LOAD_CONST:
                case OPC_GETFUNC:
                    tmp_stk[sp] = 0;
                    sp++;
                    break;

                case OPC_LOAD_VAR:
                    tmp_stk[sp] = (in.a >= 0 && in.a < fn->sym_cnt) ? tmp_var[in.a] : 0;
                    sp++;
                    break;

                case OPC_STORE_VAR: {
                    uint64_t m = (sp > 0) ? tmp_stk[sp - 1] : 0;
                    sp--;
                    if(in.a >= 0 && in.a < fn->sym_cnt) tmp_var[in.a] |= m;
                    tmp_stk[sp] = m;
                    sp++;
                    if(ea_is_global(fn, in.a)) esc |= m;
                    break;
                }

                /* 目标字面量：弹元素，标记逃逸，压自身 bit */
                case OPC_ARRAY_LIT:
                case OPC_MAP_LIT: {
                    int ne = (in.op == OPC_MAP_LIT) ? in.b * 2 : in.b;
                    for(int k = 0; k < ne && (sp - ne + k) >= 0; k++)
                        esc |= tmp_stk[sp - ne + k];
                    sp -= ne;
                    if(sp < 0) sp = 0;
                    if(in.op == target_op && lit_bit[i] >= 0) {
                        tmp_stk[sp] = (1ULL << lit_bit[i]);
                    } else {
                        tmp_stk[sp] = 0;  /* 非目标字面量：不追踪 */
                    }
                    sp++;
                    break;
                }

                case OPC_INDEX_GET:
                    sp -= 2;
                    if(sp < 0) sp = 0;
                    tmp_stk[sp] = 0;
                    sp++;
                    break;

                case OPC_INDEX_SET: {
                    uint64_t val_m = (sp > 0) ? tmp_stk[sp - 1] : 0;
                    uint64_t arr_m = (sp > 2) ? tmp_stk[sp - 3] : 0;
                    sp -= 3;
                    if(sp < 0) sp = 0;
                    esc |= arr_m;
                    esc |= val_m;
                    tmp_stk[sp] = 0;
                    sp++;
                    break;
                }

                case OPC_CALL: {
                    int argc = in.b;
                    for(int k = 0; k < argc && (sp - argc + k) >= 0; k++)
                        esc |= tmp_stk[sp - argc + k];
                    sp -= argc;
                    if(sp < 0) sp = 0;
                    tmp_stk[sp] = 0;
                    sp++;
                    break;
                }
                case OPC_CALLV: {
                    int argc = in.b;
                    for(int k = 0; k < argc && (sp - argc + k) >= 0; k++)
                        esc |= tmp_stk[sp - argc + k];
                    sp -= (argc + 1);
                    if(sp < 0) sp = 0;
                    tmp_stk[sp] = 0;
                    sp++;
                    break;
                }

                case OPC_RETURN: {
                    uint64_t m = (sp > 0) ? tmp_stk[sp - 1] : 0;
                    esc |= m;
                    sp--;
                    break;
                }
                case OPC_PEND_RETURN: {
                    uint64_t m = (sp > 0) ? tmp_stk[sp - 1] : 0;
                    esc |= m;
                    sp--;
                    break;
                }
                case OPC_THROW: {
                    uint64_t m = (sp > 0) ? tmp_stk[sp - 1] : 0;
                    esc |= m;
                    sp--;
                    break;
                }

                case OPC_BUILTIN: {
                    int argc = in.b;
                    int is_recv_returning = 0;
                    switch(in.a) {
                        case BUILTIN_ARRAY_CLEAR:
                        case BUILTIN_ARRAY_ADD:
                        case BUILTIN_INSERT:
                        case BUILTIN_DEL:
                        case BUILTIN_ARRAY_REMOVE:
                        case BUILTIN_ARRAY_SET:
                        case BUILTIN_ARRAY_ADDALL:
                            is_recv_returning = 1;
                            break;
                        default:
                            break;
                    }
                    uint64_t recv_m = (argc > 0 && sp >= argc) ? tmp_stk[sp - argc] : 0;

                    /* 扩容检测（仅数组 items 栈分配时需要） */
                    if(track_items) {
                        switch(in.a) {
                            case BUILTIN_ARRAY_ADD:
                                if(in.b >= 2) may_grow |= recv_m;
                                break;
                            case BUILTIN_INSERT:
                            case BUILTIN_ARRAY_ADDALL:
                                may_grow |= recv_m;
                                break;
                            default:
                                break;
                        }
                    }

                    if(in.a == BUILTIN_THREAD || in.a == BUILTIN_THREADLOCAL_SET) {
                        for(int k = 0; k < argc && (sp - argc + k) >= 0; k++)
                            esc |= tmp_stk[sp - argc + k];
                    }
                    else if(in.a == BUILTIN_ARRAY_ADD) {
                        if(in.b >= 2) {
                            if(sp > 0) esc |= tmp_stk[sp - 1];
                        }
                    }
                    else if(in.a == BUILTIN_INSERT || in.a == BUILTIN_ARRAY_SET) {
                        if(sp > 0) esc |= tmp_stk[sp - 1];
                    }
                    else if(in.a == BUILTIN_ARRAY_ADDALL) {
                        if(sp > 0) esc |= tmp_stk[sp - 1];
                    }

                    sp -= argc;
                    if(sp < 0) sp = 0;
                    tmp_stk[sp] = is_recv_returning ? recv_m : 0;
                    sp++;
                    break;
                }

                case OPC_ADD: case OPC_SUB: case OPC_MUL: case OPC_DIV: case OPC_MOD:
                case OPC_GT: case OPC_LT: case OPC_GE: case OPC_LE: case OPC_EQ: case OPC_NE:
                    sp -= 2;
                    if(sp < 0) sp = 0;
                    tmp_stk[sp] = 0;
                    sp++;
                    break;

                case OPC_NEG: case OPC_POS: case OPC_LOGIC_NOT: case OPC_TO_BOOL:
                case OPC_CAST_INT: case OPC_CAST_DOUBLE: case OPC_CAST_CHAR:
                case OPC_CAST_BOOL: case OPC_CAST_STRING: case OPC_CAST_ASCII:
                case OPC_CAST_BYTE: case OPC_CAST_INT8: case OPC_CAST_INT16:
                case OPC_CAST_INT32: case OPC_CAST_INT64: case OPC_CAST_UINT8:
                case OPC_CAST_UINT16: case OPC_CAST_UINT32: case OPC_CAST_UINT64:
                case OPC_CAST_LONG: case OPC_CAST_LONGLONG: case OPC_CAST_FLOAT:
                    sp -= 1;
                    if(sp < 0) sp = 0;
                    tmp_stk[sp] = 0;
                    sp++;
                    break;

                case OPC_PRE_INC: case OPC_POST_INC:
                case OPC_PRE_DEC: case OPC_POST_DEC:
                    tmp_stk[sp] = 0;
                    sp++;
                    break;

                case OPC_POP:
                    sp--;
                    if(sp < 0) sp = 0;
                    break;
                case OPC_DUP:
                    if(sp > 0) { tmp_stk[sp] = tmp_stk[sp - 1]; sp++; }
                    break;
                case OPC_PRINT:
                    break;

                case OPC_GET_ERR:
                    tmp_stk[sp] = 0;
                    sp++;
                    break;
                case OPC_TRY:
                case OPC_ENDTRY:
                case OPC_FIN_PUSH:
                    break;
                case OPC_FINISH:
                    for(int d = 0; d < sp; d++) esc |= tmp_stk[d];
                    break;

                case OPC_JMP_IF_FALSE:
                case OPC_JMP_IF_TRUE:
                    sp--;
                    if(sp < 0) sp = 0;
                    break;
                case OPC_JMP:
                    break;

                case OPC_RETURN_NIL:
                case OPC_HALT:
                case OPC_NOP:
                    break;

                default:
                    break;
            }

            escaped |= esc;

            /* 后继指令合并 */
            int succ[3];
            int succ_cnt = 0;
            if(in.op == OPC_JMP) {
                if(in.a >= 0 && in.a < n) succ[succ_cnt++] = in.a;
            } else if(in.op == OPC_JMP_IF_FALSE || in.op == OPC_JMP_IF_TRUE) {
                if(in.a >= 0 && in.a < n) succ[succ_cnt++] = in.a;
                if(i + 1 < n) succ[succ_cnt++] = i + 1;
            } else if(in.op == OPC_TRY) {
                if(i + 1 < n) succ[succ_cnt++] = i + 1;
                if(in.a > 0 && in.a < n) succ[succ_cnt++] = in.a;
            } else if(in.op == OPC_ENDTRY) {
                if(i + 1 < n) succ[succ_cnt++] = i + 1;
            } else if(in.op == OPC_PEND_RETURN) {
                if(in.b > 0 && in.b < n) succ[succ_cnt++] = in.b;
            } else if(in.op == OPC_RETURN || in.op == OPC_RETURN_NIL ||
                      in.op == OPC_HALT || in.op == OPC_THROW ||
                      in.op == OPC_FINISH) {
            } else {
                if(i + 1 < n) succ[succ_cnt++] = i + 1;
            }

            for(int s = 0; s < succ_cnt; s++) {
                int si = succ[s];
                uint64_t* dst_stk = &entry_stk[si * stk_stride];
                uint64_t* dst_var = &entry_var[si * fn->sym_cnt];
                for(int d = 0; d < stk_stride; d++) {
                    uint64_t old = dst_stk[d];
                    dst_stk[d] |= tmp_stk[d];
                    if(dst_stk[d] != old) changed = 1;
                }
                for(int v = 0; v < fn->sym_cnt; v++) {
                    uint64_t old = dst_var[v];
                    dst_var[v] |= tmp_var[v];
                    if(dst_var[v] != old) changed = 1;
                }
            }
        }

        if(!changed) break;
    }

    /* 5. 循环检测 */
    uint8_t* in_loop = (uint8_t*)calloc(n, sizeof(uint8_t));
    for(int i = 0; i < n; i++) {
        Instruction in = fn->code[i];
        if((in.op == OPC_JMP || in.op == OPC_JMP_IF_FALSE || in.op == OPC_JMP_IF_TRUE)
           && in.a >= 0 && in.a < i) {
            for(int k = in.a; k <= i; k++) in_loop[k] = 1;
        }
    }

    /* 6. 最终判定：非逃逸且非循环中的目标字面量 → 结构体栈分配 */
    for(int i = 0; i < n; i++) {
        if(fn->code[i].op == target_op && lit_bit[i] >= 0) {
            uint64_t bit = 1ULL << lit_bit[i];
            if(!(escaped & bit) && !in_loop[i]) {
                struct_result[i] = 1;
            }
        }
    }

    /* 7. items 栈分配判定（仅数组） */
    if(track_items && items_result) {
        for(int i = 0; i < n; i++) {
            if(struct_result[i] && lit_bit[i] >= 0) {
                uint64_t bit = 1ULL << lit_bit[i];
                int ne = fn->code[i].b;
                if(!(may_grow & bit) && ne > 0 && ne <= ITEMS_STACK_MAX) {
                    items_result[i] = 1;
                }
            }
        }
    }

    free(lit_bit);
    free(depths);
    free(entry_stk);
    free(entry_var);
    free(tmp_stk);
    free(tmp_var);
    free(in_loop);
    (void)other_op;
}

/* 标量替换分析：在逃逸分析之后运行。
 * 识别"不逃逸数组/map 字面量立即赋值给局部变量，且该变量仅用于常量下标读写或 len()"
 * 的模式，标记为可标量替换。代码生成时将其拆解为一组标量局部变量。
 *
 * 适用模式（保守）：
 *   a = [1,2,3];            // ARRAY_LIT + STORE_VAR + POP
 *   print(a[0]);            // LOAD_VAR a + LOAD_CONST 0 + INDEX_GET
 *   a[1] = 5;               // LOAD_VAR a + LOAD_CONST 1 + val + INDEX_SET
 *   print(len(a));          // LOAD_VAR a + BUILTIN_LEN
 * 不适用：动态下标、传参、return、迭代、多次赋值、非 POP 上下文等。
 */
static void analyze_scalar_replacement(BytecodeFunc* fn)
{
    int n = fn->code_len;
    int sc = fn->sym_cnt;

    /* 释放上一次结果 */
    if(g_scalar_var) { free(g_scalar_var); g_scalar_var = NULL; }
    if(g_scalar_kind) { free(g_scalar_kind); g_scalar_kind = NULL; }
    if(g_scalar_count) { free(g_scalar_count); g_scalar_count = NULL; }
    if(g_scalar_keys) {
        for(int v = 0; v < g_scalar_sym_cnt; v++)
            if(g_scalar_keys[v]) free(g_scalar_keys[v]);
        free(g_scalar_keys);
        g_scalar_keys = NULL;
    }
    g_scalar_sym_cnt = 0;
    if(sc == 0) return;

    g_scalar_var = (uint8_t*)calloc(sc, sizeof(uint8_t));
    g_scalar_kind = (uint8_t*)calloc(sc, sizeof(uint8_t));
    g_scalar_count = (int*)calloc(sc, sizeof(int));
    g_scalar_keys = (int**)calloc(sc, sizeof(int*));
    g_scalar_sym_cnt = sc;

    for(int i = 0; i < n; i++) {
        int is_arr = (fn->code[i].op == OPC_ARRAY_LIT);
        int is_map = (fn->code[i].op == OPC_MAP_LIT);
        if(!is_arr && !is_map) continue;

        /* 必须紧跟 STORE_VAR + POP（语句级赋值） */
        if(i + 2 >= n) continue;
        if(fn->code[i+1].op != OPC_STORE_VAR) continue;
        if(fn->code[i+2].op != OPC_POP) continue;

        int v = fn->code[i+1].a;
        if(v < 0 || v >= sc) continue;
        /* 必须是局部变量（非全局） */
        if(ea_is_global(fn, v)) continue;

        int cnt = fn->code[i].b;
        if(cnt <= 0 || cnt > SCALAR_REPL_MAX) continue;

        /* map：所有键值必须是单条 LOAD_CONST（常量 map 字面量）。
         * 值为多指令表达式时，键的偏移不固定，无法安全提取键映射 → 不标量替换。 */
        int* key_consts = NULL;
        if(is_map) {
            key_consts = (int*)malloc(sizeof(int) * cnt);
            int ok = 1;
            for(int k = 0; k < cnt; k++) {
                int kidx = i - 2 * cnt + 2 * k;       /* 键指令位置 */
                int vidx = i - 2 * cnt + 2 * k + 1;   /* 值指令位置 */
                if(kidx < 0 || vidx < 0) { ok = 0; break; }
                if(fn->code[kidx].op != OPC_LOAD_CONST) { ok = 0; break; }
                if(fn->code[vidx].op != OPC_LOAD_CONST) { ok = 0; break; }
                int ci = fn->code[kidx].a;
                if(ci < 0 || ci >= fn->const_cnt) { ok = 0; break; }
                if(fn->consts[ci].type != VAL_STRING) { ok = 0; break; }
                key_consts[k] = ci;
            }
            if(!ok) { free(key_consts); continue; }
        }

        /* 扫描该变量的所有使用：必须全部是常量下标 INDEX_GET/INDEX_SET 或 BUILTIN_LEN */
        int eligible = 1;
        int assign_count = 0;
        for(int j = 0; j < n; j++) {
            Instruction in = fn->code[j];
            if(in.op == OPC_STORE_VAR && in.a == v) {
                assign_count++;
                if(assign_count > 1) { eligible = 0; break; }
                continue;
            }
            if(in.op != OPC_LOAD_VAR || in.a != v) continue;

            /* LOAD_VAR 之后必须是：
             *   LOAD_CONST + INDEX_GET（常量下标读）
             *   LOAD_CONST + ... + INDEX_SET（常量下标写，值可以是任意表达式）
             *   BUILTIN_LEN
             */
            if(j + 1 >= n) { eligible = 0; break; }
            Instruction next = fn->code[j+1];

            if(next.op == OPC_BUILTIN && next.a == BUILTIN_LEN) {
                continue;  /* len(a) → 常量替换 */
            }

            if(next.op != OPC_LOAD_CONST) { eligible = 0; break; }
            int ci = next.a;
            if(ci < 0 || ci >= fn->const_cnt) { eligible = 0; break; }

            if(is_arr) {
                /* 数组下标必须是整数常量且在边界内 */
                if(fn->consts[ci].type != VAL_INT) { eligible = 0; break; }
                long long idx = fn->consts[ci].v.i;
                if(idx < 0 || idx >= cnt) { eligible = 0; break; }
            } else {
                /* map 键必须是字符串常量且匹配字面量中的某个键 */
                if(fn->consts[ci].type != VAL_STRING) { eligible = 0; break; }
                int key_match = 0;
                const char* access_key = lumin_str_cstr(&fn->consts[ci]);
                for(int k = 0; k < cnt; k++) {
                    int kci = key_consts[k];
                    if(kci >= 0 && kci < fn->const_cnt && fn->consts[kci].type == VAL_STRING) {
                        if(strcmp(lumin_str_cstr(&fn->consts[kci]), access_key) == 0) {
                            key_match = 1; break;
                        }
                    }
                }
                if(!key_match) { eligible = 0; break; }
            }

            /* 检查 LOAD_CONST 之后是 INDEX_GET 还是 INDEX_SET */
            if(j + 2 >= n) { eligible = 0; break; }
            Instruction next2 = fn->code[j+2];
            if(next2.op == OPC_INDEX_GET) {
                continue;  /* a[idx] 读 */
            }
            /* INDEX_SET：LOAD_VAR + LOAD_CONST(idx) + 值 + INDEX_SET，值占 1 条指令 */
            if(next2.op != OPC_INDEX_SET) {
                /* 值可能是多条指令（如表达式），找到对应的 INDEX_SET */
                /* 简化：要求值是单条指令（LOAD_CONST/LOAD_VAR/GETFUNC 等） */
                if(j + 3 >= n || fn->code[j+3].op != OPC_INDEX_SET) {
                    eligible = 0; break;
                }
            }
            /* INDEX_SET 合法 */
        }

        if(eligible && assign_count == 1) {
            g_scalar_var[v] = 1;
            g_scalar_kind[v] = is_arr ? 0 : 1;
            g_scalar_count[v] = cnt;
            if(is_map) {
                g_scalar_keys[v] = key_consts;
            } else {
                free(key_consts);
            }
        } else {
            if(key_consts) free(key_consts);
        }
    }
}

// ---------------- 函数/主函数发射 ----------------

static void emit_func_proto(BytecodeFunc* fn)
{
    fprintf(out, "static Value lumin_func_%s(", fn->name);
    int total = fn->param_cnt + (fn->has_variadic ? 1 : 0);
    for(int i = 0; i < total; i++) {
        if(i) fprintf(out, ", ");
        fprintf(out, "Value");
    }
    fprintf(out, ");\n");
}

static void emit_func_def(BytecodeFunc* fn)
{
    collect_func_locals(fn);
    g_cur_fn = fn;  /* 逃逸分析中用于全局/局部判定 */
    /* 逃逸分析：数组（含 items）+ map */
    int n = fn->code_len;
    g_stack_alloc = (uint8_t*)calloc(n, sizeof(uint8_t));
    g_items_stack_alloc = (uint8_t*)calloc(n, sizeof(uint8_t));
    g_map_stack_alloc = (uint8_t*)calloc(n, sizeof(uint8_t));
    analyze_escape_for(fn, OPC_ARRAY_LIT, g_stack_alloc, g_items_stack_alloc, 1);
    analyze_escape_for(fn, OPC_MAP_LIT, g_map_stack_alloc, NULL, 0);
    analyze_scalar_replacement(fn);
    /* 收集本函数内 FIN_PUSH 的目标（finally/循环结束 label） */
    fin_lab_cnt = 0;
    for(int i = 0; i < fn->code_len; i++) {
        Instruction in = fn->code[i];
        if(in.op == OPC_FIN_PUSH && in.b) fin_lab_idx_of(in.b);
    }
    fprintf(out, "static Value lumin_func_%s(", fn->name);
    int total = fn->param_cnt + (fn->has_variadic ? 1 : 0);
    for(int i = 0; i < total; i++) {
        if(i) fprintf(out, ", ");
        fprintf(out, "Value lmloc_%s", fn->params[i]);
    }
    int maxd = bc_analyze_stack(fn, NULL, 0);
    fprintf(out, ")\n{\n");
    fprintf(out, "    Value __stk[%d];\n", maxd + 2);
    fprintf(out, "    int __sp = 0;\n");
    if(fin_lab_cnt > 0) {
        fprintf(out, "    static void* __g_fin_labs[%d] = { ", fin_lab_cnt);
        for(int k = 0; k < fin_lab_cnt; k++)
            fprintf(out, "&&L%d%s", fin_lab_pcs[k], (k + 1 < fin_lab_cnt) ? ", " : "");
        fprintf(out, " };\n");
    }
    fprintf(out, "    int __g_d0 = __g_depth; jmp_buf* __g_gj0 = g_err_jmp; int __g_fin0 = __g_fin_n;\n");
    fprintf(out, "    g_trace_push(\"%s\");\n", fn->name);
    for(int i = 0; i < fn_locals.count; i++) {
        fprintf(out, "    Value lmloc_%s = val_none();\n", fn_locals.names[i]);
    }
    /* 栈分配数组声明：逃逸分析判定为不逃逸的 OPC_ARRAY_LIT（标量替换的跳过） */
    for(int i = 0; i < fn->code_len; i++) {
        if(g_stack_alloc && g_stack_alloc[i] && !is_scalar_replaced_lit(fn, i)) {
            fprintf(out, "    ValueArray __arr_stk_%d;\n", i);
        }
    }
    /* items 栈缓冲区声明：完全栈分配数组的 items 在 C 栈上（标量替换的跳过） */
    for(int i = 0; i < fn->code_len; i++) {
        if(g_items_stack_alloc && g_items_stack_alloc[i] && !is_scalar_replaced_lit(fn, i)) {
            int ne = fn->code[i].b;
            fprintf(out, "    Value __items_stk_%d[%d];\n", i, ne);
        }
    }
    /* map 栈分配声明：不逃逸的 OPC_MAP_LIT（标量替换的跳过） */
    for(int i = 0; i < fn->code_len; i++) {
        if(g_map_stack_alloc && g_map_stack_alloc[i] && !is_scalar_replaced_lit(fn, i)) {
            fprintf(out, "    ValueMap __map_stk_%d;\n", i);
        }
    }
    /* 标量替换变量声明：每个被标量替换的变量的每个元素一个标量 */
    for(int v = 0; v < fn->sym_cnt; v++) {
        if(g_scalar_var && g_scalar_var[v]) {
            for(int k = 0; k < g_scalar_count[v]; k++) {
                fprintf(out, "    Value __sr_v%d_e%d = val_none();\n", v, k);
            }
        }
    }
    /* GC 根注册：编译通道 CFrame 帧链 push
     * local_ptrs = 参数 + 函数局部变量 + 标量替换变量（均为 C 栈上 Value，取地址） */
    {
        int _total_params = fn->param_cnt + (fn->has_variadic ? 1 : 0);
        int _sr_total = 0;
        for(int v = 0; v < fn->sym_cnt; v++)
            if(g_scalar_var && g_scalar_var[v]) _sr_total += g_scalar_count[v];
        int _nlocals = _total_params + fn_locals.count + _sr_total;
        int _arr_size = _nlocals > 0 ? _nlocals : 1;
        fprintf(out, "    Value* __local_ptrs[%d] = { ", _arr_size);
        int _idx = 0;
        for(int i = 0; i < _total_params; i++) {
            if(_idx) fprintf(out, ", ");
            fprintf(out, "&lmloc_%s", fn->params[i]);
            _idx++;
        }
        for(int i = 0; i < fn_locals.count; i++) {
            if(_idx) fprintf(out, ", ");
            fprintf(out, "&lmloc_%s", fn_locals.names[i]);
            _idx++;
        }
        for(int v = 0; v < fn->sym_cnt; v++) {
            if(g_scalar_var && g_scalar_var[v]) {
                for(int k = 0; k < g_scalar_count[v]; k++) {
                    if(_idx) fprintf(out, ", ");
                    fprintf(out, "&__sr_v%d_e%d", v, k);
                    _idx++;
                }
            }
        }
        if(_nlocals == 0) fprintf(out, "NULL");
        fprintf(out, " };\n");
        fprintf(out, "    CFrame __frame;\n");
        fprintf(out, "    __frame.stack = __stk;\n");
        fprintf(out, "    __frame.sp = &__sp;\n");
        fprintf(out, "    __frame.local_ptrs = __local_ptrs;\n");
        fprintf(out, "    __frame.nlocals = %d;\n", _nlocals);
        fprintf(out, "    gc_push_cframe(&__frame);\n");
        fprintf(out, "    gc_stw_check();\n");
    }
    emit_insns(fn);
    g_cur_fn = NULL;
    if(g_stack_alloc) { free(g_stack_alloc); g_stack_alloc = NULL; }
    g_stack_alloc_len = 0;
    if(g_items_stack_alloc) { free(g_items_stack_alloc); g_items_stack_alloc = NULL; }
    g_items_stack_alloc_len = 0;
    if(g_map_stack_alloc) { free(g_map_stack_alloc); g_map_stack_alloc = NULL; }
    g_map_stack_alloc_len = 0;
    if(g_scalar_var) { free(g_scalar_var); g_scalar_var = NULL; }
    if(g_scalar_kind) { free(g_scalar_kind); g_scalar_kind = NULL; }
    if(g_scalar_count) { free(g_scalar_count); g_scalar_count = NULL; }
    if(g_scalar_keys) {
        for(int v = 0; v < g_scalar_sym_cnt; v++)
            if(g_scalar_keys[v]) free(g_scalar_keys[v]);
        free(g_scalar_keys);
        g_scalar_keys = NULL;
    }
    g_scalar_sym_cnt = 0;
    fprintf(out, "}\n\n");
}

// 生成统一签名包装（Value(*)(Value*, int)）与函数表：高阶函数调用入口
static void emit_func_wraps(void)
{
    int cnt = ir_func_table_count();
    for(int i = 0; i < cnt; i++) {
        BytecodeFunc* fn = ir_func_table_get(i);
        fprintf(out, "static Value lum_wrap_%d(Value* a, int n)\n{\n", i);
        for(int k = 0; k < fn->param_cnt; k++)
            fprintf(out, "    Value p%d = (n > %d) ? a[%d] : val_none();\n", k, k, k);
        if(fn->has_variadic) {
            // 变参打包：n - fixed 个尾部实参进数组（动态调用经 wrap 时实参在 a[]）
            fprintf(out, "    Value __rest = val_array(n > %d ? n - %d : 0);\n", fn->param_cnt, fn->param_cnt);
            fprintf(out, "    for(int __k = 0; __k < __rest.v.array->len; __k++) __rest.v.array->items[__k] = a[%d + __k];\n", fn->param_cnt);
        }
        fprintf(out, "    return lumin_func_%s(", fn->name);
        int total = fn->param_cnt + (fn->has_variadic ? 1 : 0);
        for(int k = 0; k < total; k++) {
            if(k) fprintf(out, ", ");
            if(k < fn->param_cnt) fprintf(out, "p%d", k);
            else {
                fprintf(out, "__rest");
            }
        }
        fprintf(out, ");\n}\n\n");
    }
    fprintf(out, "static Value (*const lumin_cfunc_tbl[])(Value*, int) = {\n");
    for(int i = 0; i < cnt; i++)
        fprintf(out, "    lum_wrap_%d,\n", i);
    fprintf(out, "};\n\n");
}

static void emit_main(BytecodeFunc* main_fn)
{
    // 全局变量：main 指令流里的全部变量引用
    memset(&g_globals, 0, sizeof(g_globals));
    scan_var_refs(main_fn, &g_globals, 1);
    for(int i = 0; i < g_globals.count; i++) {
        fprintf(out, "static Value lmvar_%s = {0};\n", g_globals.names[i]);
    }
    fprintf(out, "\n");

    // 函数原型（前向引用/递归）
    for(int i = 0; i < ir_func_table_count(); i++) {
        emit_func_proto(ir_func_table_get(i));
    }
    // 高阶包装前置声明（函数体内 GETFUNC 先于 wraps 定义使用）
    for(int i = 0; i < ir_func_table_count(); i++) {
        fprintf(out, "static Value lum_wrap_%d(Value*, int);\n", i);
    }
    fprintf(out, "\n");

    // 函数定义
    for(int i = 0; i < ir_func_table_count(); i++) {
        emit_func_def(ir_func_table_get(i));
    }

    // 高阶函数统一调用包装 + 函数表（VM 端 VAL_FUNC 指向 RuntimeFunc，C 端指向此包装）
    emit_func_wraps();

    // main
    int maxd = bc_analyze_stack(main_fn, NULL, 0);
    /* main 逃逸分析：g_cur_fn=NULL，所有变量视为全局（存全局即逃逸） */
    g_cur_fn = NULL;
    memset(&fn_locals, 0, sizeof(fn_locals));
    {
        int mn = main_fn->code_len;
        g_stack_alloc = (uint8_t*)calloc(mn, sizeof(uint8_t));
        g_items_stack_alloc = (uint8_t*)calloc(mn, sizeof(uint8_t));
        g_map_stack_alloc = (uint8_t*)calloc(mn, sizeof(uint8_t));
        analyze_escape_for(main_fn, OPC_ARRAY_LIT, g_stack_alloc, g_items_stack_alloc, 1);
        analyze_escape_for(main_fn, OPC_MAP_LIT, g_map_stack_alloc, NULL, 0);
        analyze_scalar_replacement(main_fn);
    }
    fprintf(out, "int main(void){\n");
    fprintf(out, "    Value __stk[%d];\n", maxd + 2);
    fprintf(out, "    int __sp = 0;\n");
    /* 栈分配数组声明 */
    for(int i = 0; i < main_fn->code_len; i++) {
        if(g_stack_alloc && g_stack_alloc[i]) {
            fprintf(out, "    ValueArray __arr_stk_%d;\n", i);
        }
    }
    /* items 栈缓冲区声明 */
    for(int i = 0; i < main_fn->code_len; i++) {
        if(g_items_stack_alloc && g_items_stack_alloc[i]) {
            int ne = main_fn->code[i].b;
            fprintf(out, "    Value __items_stk_%d[%d];\n", i, ne);
        }
    }
    /* map 栈分配声明 */
    for(int i = 0; i < main_fn->code_len; i++) {
        if(g_map_stack_alloc && g_map_stack_alloc[i]) {
            fprintf(out, "    ValueMap __map_stk_%d;\n", i);
        }
    }
    /* 标量替换变量声明（main 中全为全局变量，通常不会触发） */
    for(int v = 0; v < main_fn->sym_cnt; v++) {
        if(g_scalar_var && g_scalar_var[v]) {
            for(int k = 0; k < g_scalar_count[v]; k++) {
                fprintf(out, "    Value __sr_v%d_e%d = val_none();\n", v, k);
            }
        }
    }
    /* 函数边界保存（RETURN/FINISH act=5 恢复用），与 emit_func_def 一致 */
    fprintf(out, "    int __g_d0 = __g_depth; jmp_buf* __g_gj0 = g_err_jmp; int __g_fin0 = __g_fin_n;\n");
    /* main 内 finally 完成动作目标收集（与 emit_func_def 一致，否则 FINISH 引用未定义的 __g_fin_labs） */
    fin_lab_cnt = 0;
    for(int i = 0; i < main_fn->code_len; i++) {
        Instruction in = main_fn->code[i];
        if(in.op == OPC_FIN_PUSH && in.b) fin_lab_idx_of(in.b);
    }
    if(fin_lab_cnt > 0) {
        fprintf(out, "    static void* __g_fin_labs[%d] = { ", fin_lab_cnt);
        for(int k = 0; k < fin_lab_cnt; k++)
            fprintf(out, "&&L%d%s", fin_lab_pcs[k], (k + 1 < fin_lab_cnt) ? ", " : "");
        fprintf(out, " };\n");
    }
    /* GC 根注册：main 的 CFrame push
     * local_ptrs = 全局变量（lmvar_xxx，文件级 static）+ main 内标量替换变量 */
    {
        int _sr_total = 0;
        for(int v = 0; v < main_fn->sym_cnt; v++)
            if(g_scalar_var && g_scalar_var[v]) _sr_total += g_scalar_count[v];
        int _nlocals = g_globals.count + _sr_total;
        int _arr_size = _nlocals > 0 ? _nlocals : 1;
        fprintf(out, "    Value* __local_ptrs[%d] = { ", _arr_size);
        int _idx = 0;
        for(int i = 0; i < g_globals.count; i++) {
            if(_idx) fprintf(out, ", ");
            fprintf(out, "&lmvar_%s", g_globals.names[i]);
            _idx++;
        }
        for(int v = 0; v < main_fn->sym_cnt; v++) {
            if(g_scalar_var && g_scalar_var[v]) {
                for(int k = 0; k < g_scalar_count[v]; k++) {
                    if(_idx) fprintf(out, ", ");
                    fprintf(out, "&__sr_v%d_e%d", v, k);
                    _idx++;
                }
            }
        }
        if(_nlocals == 0) fprintf(out, "NULL");
        fprintf(out, " };\n");
        fprintf(out, "    CFrame __frame;\n");
        fprintf(out, "    __frame.stack = __stk;\n");
        fprintf(out, "    __frame.sp = &__sp;\n");
        fprintf(out, "    __frame.local_ptrs = __local_ptrs;\n");
        fprintf(out, "    __frame.nlocals = %d;\n", _nlocals);
        fprintf(out, "    gc_push_cframe(&__frame);\n");
        fprintf(out, "    gc_stw_check();\n");
    }
    g_cur_fn = NULL;
    emit_insns(main_fn);
    if(g_stack_alloc) { free(g_stack_alloc); g_stack_alloc = NULL; }
    g_stack_alloc_len = 0;
    if(g_items_stack_alloc) { free(g_items_stack_alloc); g_items_stack_alloc = NULL; }
    g_items_stack_alloc_len = 0;
    if(g_map_stack_alloc) { free(g_map_stack_alloc); g_map_stack_alloc = NULL; }
    g_map_stack_alloc_len = 0;
    if(g_scalar_var) { free(g_scalar_var); g_scalar_var = NULL; }
    if(g_scalar_kind) { free(g_scalar_kind); g_scalar_kind = NULL; }
    if(g_scalar_count) { free(g_scalar_count); g_scalar_count = NULL; }
    if(g_scalar_keys) {
        for(int v = 0; v < g_scalar_sym_cnt; v++)
            if(g_scalar_keys[v]) free(g_scalar_keys[v]);
        free(g_scalar_keys);
        g_scalar_keys = NULL;
    }
    g_scalar_sym_cnt = 0;
    fprintf(out, "}\n\n");
}

// ---------------- 入口 ----------------

void ir_cgen_file(const char* out_c_path, BytecodeFunc* main_fn)
{
    out = fopen(out_c_path, "w");
    if(!out) {
        perror("open output c file failed");
        return;
    }

    fwrite(src_runtime_full_runtime_full_h, 1, src_runtime_full_runtime_full_h_len, out);
    fputs("\n\n", out);
    fprintf(out, "#include <stdio.h>\n");
    fprintf(out, "#include <stdlib.h>\n");
    fprintf(out, "#include <string.h>\n\n");
    fwrite(src_runtime_full_runtime_full_c, 1, src_runtime_full_runtime_full_c_len, out);

    emit_main(main_fn);
    fclose(out);
}
