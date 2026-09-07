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
        case VAL_STRING: fprintf(f, "lumin_make_string("); emit_c_string_lit(f, v->v.s); fprintf(f, ")"); break;
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
    for(int i = 0; i < fn->code_len; i++) {
        if(is_jump_target(fn, i)) fprintf(out, "L%d:;\n", i);
        Instruction in = fn->code[i];
        const char* nm = (in.a >= 0 && in.a < fn->sym_cnt) ? fn->syms[in.a] : NULL;
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
                fprintf(out, "    {\n");
                fprintf(out, "        Value __arr = val_array(%d);\n", n);
                for(int k = 0; k < n; k++)
                    fprintf(out, "        __arr.v.array->items[%d] = __stk[__sp - %d + %d];\n", k, n, k);
                fprintf(out, "        __sp = __sp - %d + 1;\n", n);
                fprintf(out, "        __stk[__sp - 1] = __arr;\n");
                fprintf(out, "    }\n");
                break;
            }
            case OPC_MAP_LIT: {
                int n = in.b;
                fprintf(out, "    {\n");
                fprintf(out, "        Value __m = lumin_map_lit(&__stk[__sp - %d], %d);\n", 2 * n, n);
                fprintf(out, "        __sp = __sp - %d + 1;\n", 2 * n);
                fprintf(out, "        __stk[__sp - 1] = __m;\n");
                fprintf(out, "    }\n");
                break;
            }
            case OPC_INDEX_GET:
                fprintf(out, "    { Value __idx = __stk[--__sp], __c = __stk[--__sp]; __stk[__sp++] = lumin_index_get(__c, __idx); }\n");
                break;
            case OPC_INDEX_SET:
                fprintf(out, "    { Value __val = __stk[--__sp], __idx = __stk[--__sp], __arr = __stk[--__sp]; __stk[__sp++] = lumin_array_set(__arr, __idx, __val); }\n");
                break;
            case OPC_BUILTIN:
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
                        fprintf(out, "    { Value __n = __stk[--__sp]; if(__n.type != VAL_STRING) runtime_error(\"threadlocal_get() 名字参数必须是字符串\"); __stk[__sp++] = lumin_tls_get(__n.v.s); }\n");
                        break;
                    case BUILTIN_THREADLOCAL_SET:
                        fprintf(out, "    { Value __v = __stk[--__sp]; Value __n = __stk[--__sp]; if(__n.type != VAL_STRING) runtime_error(\"threadlocal_set() 名字参数必须是字符串\"); lumin_tls_set(__n.v.s, __v); __stk[__sp++] = __v; }\n");
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
                            fprintf(out, "    { Value __e = __stk[--__sp]; Value __v = __stk[--__sp]; __stk[__sp++] = lumin_json_parse_enc(__v.v.s, __e); }\n");
                        else
                            fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_json_parse_enc(__v.v.s, val_none()); }\n");
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
                            fprintf(out, "    { Value __e = __stk[--__sp]; Value __v = __stk[--__sp]; if(__v.type == VAL_MAP || __v.type == VAL_ARRAY) { char* __q = lumin_qs_stringify_enc(__v, __e); __stk[__sp++] = lumin_make_string(__q); free(__q); } else if(__v.type == VAL_STRING) { __stk[__sp++] = lumin_qs_parse_enc(__v.v.s, __e); } else runtime_error(\"qs() 参数必须是字典/数组（序列化）或字符串（解析）\"); }\n");
                        else
                            fprintf(out, "    { Value __v = __stk[--__sp]; if(__v.type == VAL_MAP || __v.type == VAL_ARRAY) { char* __q = lumin_qs_stringify_enc(__v, val_none()); __stk[__sp++] = lumin_make_string(__q); free(__q); } else if(__v.type == VAL_STRING) { __stk[__sp++] = lumin_qs_parse_enc(__v.v.s, val_none()); } else runtime_error(\"qs() 参数必须是字典/数组（序列化）或字符串（解析）\"); }\n");
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
                        fprintf(out, "    { Value __v = __stk[--__sp]; char* __r = lumin_url_encode(__v.type==VAL_STRING?(__v.v.s?__v.v.s:\"\"):\"\"); __stk[__sp++] = lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_DECODE_URL:
                        fprintf(out, "    { Value __v = __stk[--__sp]; char* __r = lumin_url_decode(__v.type==VAL_STRING?(__v.v.s?__v.v.s:\"\"):\"\"); __stk[__sp++] = lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_MD5:
                        fprintf(out, "    { Value __v = __stk[--__sp]; const char* __i = __v.type==VAL_STRING?(__v.v.s?__v.v.s:\"\"):\"\"; char* __r = lumin_md5_hex(__i, (int)strlen(__i)); __stk[__sp++] = lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_ENCODE_BASE64:
                        fprintf(out, "    { Value __v = __stk[--__sp]; const char* __i = __v.type==VAL_STRING?(__v.v.s?__v.v.s:\"\"):\"\"; char* __r = lumin_base64_encode(__i, (int)strlen(__i)); __stk[__sp++] = lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_DECODE_BASE64:
                        fprintf(out, "    { Value __v = __stk[--__sp]; int __ol=0; char* __r = lumin_base64_decode(__v.type==VAL_STRING?(__v.v.s?__v.v.s:\"\"):\"\", &__ol); __stk[__sp++] = lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_REGEX_MATCH:
                        fprintf(out, "    { Value __p=__stk[--__sp]; Value __s=__stk[--__sp]; __stk[__sp++]=lumin_make_bool(lumin_regex_match(__s.type==VAL_STRING?(__s.v.s?__s.v.s:\"\"):\"\", __p.type==VAL_STRING?(__p.v.s?__p.v.s:\"\"):\"\")); }\n");
                        break;
                    case BUILTIN_REGEX_SEARCH:
                        fprintf(out, "    { Value __p=__stk[--__sp]; Value __s=__stk[--__sp]; __stk[__sp++]=lumin_regex_search(__s.type==VAL_STRING?(__s.v.s?__s.v.s:\"\"):\"\", __p.type==VAL_STRING?(__p.v.s?__p.v.s:\"\"):\"\"); }\n");
                        break;
                    case BUILTIN_REGEX_REPLACE:
                        fprintf(out, "    { Value __r=__stk[--__sp]; Value __p=__stk[--__sp]; Value __s=__stk[--__sp]; char* __o=lumin_regex_replace(__s.type==VAL_STRING?(__s.v.s?__s.v.s:\"\"):\"\", __p.type==VAL_STRING?(__p.v.s?__p.v.s:\"\"):\"\", __r.type==VAL_STRING?(__r.v.s?__r.v.s:\"\"):\"\"); __stk[__sp++]=lumin_make_string(__o); free(__o); }\n");
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
                            fprintf(out, "    { Value __t=__stk[--__sp]; Value __f=__stk[--__sp]; double __ts=__t.type==VAL_DOUBLE?__t.v.d:(double)lumin_extract_int(__t); char* __r=lumin_format_time(__f.type==VAL_STRING?(__f.v.s?__f.v.s:\"\"):\"\", __ts); __stk[__sp++]=lumin_make_string(__r); free(__r); }\n");
                        else
                            fprintf(out, "    { Value __f=__stk[--__sp]; char* __r=lumin_format_time(__f.type==VAL_STRING?(__f.v.s?__f.v.s:\"\"):\"\", -1.0); __stk[__sp++]=lumin_make_string(__r); free(__r); }\n");
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
                            fprintf(out, "    { Value __e = __stk[--__sp]; Value __v = __stk[--__sp]; __stk[__sp++] = lumin_json_parse_enc(__v.v.s, __e); }\n");
                        else
                            fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_json_parse_enc(__v.v.s, val_none()); }\n");
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
                            fprintf(out, "    { Value __e = __stk[--__sp]; Value __v = __stk[--__sp]; if(__v.type == VAL_MAP || __v.type == VAL_ARRAY) { char* __q = lumin_qs_stringify_enc(__v, __e); __stk[__sp++] = lumin_make_string(__q); free(__q); } else if(__v.type == VAL_STRING) { __stk[__sp++] = lumin_qs_parse_enc(__v.v.s, __e); } else runtime_error(\"qs() 参数必须是字典/数组（序列化）或字符串（解析）\"); }\n");
                        else
                            fprintf(out, "    { Value __v = __stk[--__sp]; if(__v.type == VAL_MAP || __v.type == VAL_ARRAY) { char* __q = lumin_qs_stringify_enc(__v, val_none()); __stk[__sp++] = lumin_make_string(__q); free(__q); } else if(__v.type == VAL_STRING) { __stk[__sp++] = lumin_qs_parse_enc(__v.v.s, val_none()); } else runtime_error(\"qs() 参数必须是字典/数组（序列化）或字符串（解析）\"); }\n");
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
                        fprintf(out, "    { Value __v = __stk[--__sp]; char* __r = lumin_url_encode(__v.type==VAL_STRING?(__v.v.s?__v.v.s:\"\"):\"\"); __stk[__sp++] = lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_DECODE_URL:
                        fprintf(out, "    { Value __v = __stk[--__sp]; char* __r = lumin_url_decode(__v.type==VAL_STRING?(__v.v.s?__v.v.s:\"\"):\"\"); __stk[__sp++] = lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_MD5:
                        fprintf(out, "    { Value __v = __stk[--__sp]; const char* __i = __v.type==VAL_STRING?(__v.v.s?__v.v.s:\"\"):\"\"; char* __r = lumin_md5_hex(__i, (int)strlen(__i)); __stk[__sp++] = lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_ENCODE_BASE64:
                        fprintf(out, "    { Value __v = __stk[--__sp]; const char* __i = __v.type==VAL_STRING?(__v.v.s?__v.v.s:\"\"):\"\"; char* __r = lumin_base64_encode(__i, (int)strlen(__i)); __stk[__sp++] = lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_DECODE_BASE64:
                        fprintf(out, "    { Value __v = __stk[--__sp]; int __ol=0; char* __r = lumin_base64_decode(__v.type==VAL_STRING?(__v.v.s?__v.v.s:\"\"):\"\", &__ol); __stk[__sp++] = lumin_make_string(__r); free(__r); }\n");
                        break;
                    case BUILTIN_REGEX_MATCH:
                        fprintf(out, "    { Value __p=__stk[--__sp]; Value __s=__stk[--__sp]; __stk[__sp++]=lumin_make_bool(lumin_regex_match(__s.type==VAL_STRING?(__s.v.s?__s.v.s:\"\"):\"\", __p.type==VAL_STRING?(__p.v.s?__p.v.s:\"\"):\"\")); }\n");
                        break;
                    case BUILTIN_REGEX_SEARCH:
                        fprintf(out, "    { Value __p=__stk[--__sp]; Value __s=__stk[--__sp]; __stk[__sp++]=lumin_regex_search(__s.type==VAL_STRING?(__s.v.s?__s.v.s:\"\"):\"\", __p.type==VAL_STRING?(__p.v.s?__p.v.s:\"\"):\"\"); }\n");
                        break;
                    case BUILTIN_REGEX_REPLACE:
                        fprintf(out, "    { Value __r=__stk[--__sp]; Value __p=__stk[--__sp]; Value __s=__stk[--__sp]; char* __o=lumin_regex_replace(__s.type==VAL_STRING?(__s.v.s?__s.v.s:\"\"):\"\", __p.type==VAL_STRING?(__p.v.s?__p.v.s:\"\"):\"\", __r.type==VAL_STRING?(__r.v.s?__r.v.s:\"\"):\"\"); __stk[__sp++]=lumin_make_string(__o); free(__o); }\n");
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
                            fprintf(out, "    { Value __t=__stk[--__sp]; Value __f=__stk[--__sp]; double __ts=__t.type==VAL_DOUBLE?__t.v.d:(double)lumin_extract_int(__t); char* __r=lumin_format_time(__f.type==VAL_STRING?(__f.v.s?__f.v.s:\"\"):\"\", __ts); __stk[__sp++]=lumin_make_string(__r); free(__r); }\n");
                        else
                            fprintf(out, "    { Value __f=__stk[--__sp]; char* __r=lumin_format_time(__f.type==VAL_STRING?(__f.v.s?__f.v.s:\"\"):\"\", -1.0); __stk[__sp++]=lumin_make_string(__r); free(__r); }\n");
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
                fprintf(out, "      if(setjmp(__g_jbs[__d]) == 0) { __g_depth = __d + 1; goto L%d; }\n", i + 1);
                fprintf(out, "      int __d2 = __g_depth - 1;\n");
                fprintf(out, "      __sp = __g_sp0[__d2]; __g_depth = __d2; g_err_jmp = __g_prev[__d2];\n");
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
                fprintf(out, "        if(lumin_map_has(__v, lumin_make_string(\"type\"))) { Value __tv = lumin_map_get(__v, lumin_make_string(\"type\")); if(__tv.type == VAL_STRING) __tp = __tv.v.s; }\n");
                fprintf(out, "        if(lumin_map_has(__v, lumin_make_string(\"message\"))) { Value __mv = lumin_map_get(__v, lumin_make_string(\"message\")); if(__mv.type == VAL_STRING) __msg = strdup(__mv.v.s); }\n");
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
                    fprintf(out, "      else if(__fa == 5) { __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0; return 0; }\n");
                else
                    fprintf(out, "      else if(__fa == 5) { __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0; if(g_trace_n > 0) g_trace_n--; { Value __v = __g_pend_val; return __v; } }\n");
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
                fprintf(out, "    goto L%d;\n", in.a);
                break;
            case OPC_JMP_IF_FALSE:
                fprintf(out, "    if (!lumin_to_bool(__stk[--__sp])) goto L%d;\n", in.a);
                break;
            case OPC_JMP_IF_TRUE:
                fprintf(out, "    if (lumin_to_bool(__stk[--__sp])) goto L%d;\n", in.a);
                break;
            case OPC_CALL: {
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
                    fprintf(out, "    { Value __v = __stk[--__sp]; return __v; }\n");
                } else
                    fprintf(out, "    return 0;\n");
                break;
            case OPC_RETURN_NIL:
                if(g_cur_fn) {
                    fprintf(out, "    __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0;\n");
                    fprintf(out, "    if(g_trace_n > 0) g_trace_n--;\n");
                    fprintf(out, "    return val_none();\n");
                } else
                    fprintf(out, "    return 0;\n");
                break;
            case OPC_HALT:
                fprintf(out, "    return 0;\n");
                break;
            default:
                break;
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
    g_cur_fn = fn;
    emit_insns(fn);
    g_cur_fn = NULL;
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
    fprintf(out, "int main(void){\n");
    fprintf(out, "    Value __stk[%d];\n", maxd + 2);
    fprintf(out, "    int __sp = 0;\n");
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
    g_cur_fn = NULL;
    emit_insns(main_fn);
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
