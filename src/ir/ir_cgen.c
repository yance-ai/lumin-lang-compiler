// IR → C 生成器：把字节码指令流翻译为 C（栈模拟 + goto 标号），语义与 VM 一致。
// 变量两级收集（全局/函数局部+参数）与旧 codegen 对齐：全局 = main 中引用的名字；
// 函数局部 = 参数 + 函数内 STORE/INC/DEC 的名字（排除参数与全局）。
#include "ir_cgen.h"
#include "ir_compile.h"
#include "ast/lumin_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// xxd 嵌入的运行时资源（codegen 同款）
extern unsigned char src_runtime_full_runtime_full_h[];
extern unsigned int src_runtime_full_runtime_full_h_len;
extern unsigned char src_runtime_full_runtime_full_c[];
extern unsigned int src_runtime_full_runtime_full_c_len;

#define C_NAME_MAX 128

typedef struct {
    char* names[C_NAME_MAX];
    int count;
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
    if(s->count < C_NAME_MAX) s->names[s->count++] = (char*)name;
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
        if(!fn_has_param(fn, n) && !ns_has(&g_globals, n)) ns_add(&fn_locals, n);
    }
}

// ---------------- 指令翻译 ----------------

// 判断指令 idx 是否为跳转目标
static int is_jump_target(BytecodeFunc* fn, int idx)
{
    for(int i = 0; i < fn->code_len; i++) {
        Instruction in = fn->code[i];
        if((in.op == OPC_JMP || in.op == OPC_JMP_IF_FALSE || in.op == OPC_JMP_IF_TRUE) && in.a == idx)
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
            case OPC_LOAD_VAR:
                fprintf(out, "    __stk[__sp++] = %s;\n", cvar(nm));
                break;
            case OPC_STORE_VAR:
                fprintf(out, "    { Value __v = __stk[--__sp]; %s = val_clone(&__v); __stk[__sp++] = __v; }\n", cvar(nm));
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
            case OPC_LOGIC_NOT:   fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_logic_not(__v); }\n"); break;
            case OPC_ARRAY_LIT: {
                int n = in.b;
                fprintf(out, "    {\n");
                fprintf(out, "        Value __arr = val_array(%d);\n", n);
                for(int k = 0; k < n; k++)
                    fprintf(out, "        __arr.v.array.items[%d] = val_clone(&__stk[__sp - %d + %d]);\n", k, n, k);
                fprintf(out, "        __sp = __sp - %d + 1;\n", n);
                fprintf(out, "        __stk[__sp - 1] = __arr;\n");
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
                        fprintf(out, "    { Value __v = __stk[--__sp]; __stk[__sp++] = lumin_range(__v); }\n");
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
                        fprintf(out, "        __rest.v.array.items[%d] = __args[%d];\n", k, fixed + k);
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
            case OPC_RETURN:
                if(g_cur_fn)
                    fprintf(out, "    { Value __v = __stk[--__sp]; return val_clone(&__v); }\n");
                else
                    fprintf(out, "    return 0;\n");
                break;
            case OPC_RETURN_NIL:
                if(g_cur_fn)
                    fprintf(out, "    return val_none();\n");
                else
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
    for(int i = 0; i < fn_locals.count; i++) {
        fprintf(out, "    Value lmloc_%s = val_none();\n", fn_locals.names[i]);
    }
    g_cur_fn = fn;
    emit_insns(fn);
    g_cur_fn = NULL;
    fprintf(out, "}\n\n");
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
    fprintf(out, "\n");

    // 函数定义
    for(int i = 0; i < ir_func_table_count(); i++) {
        emit_func_def(ir_func_table_get(i));
    }

    // main
    int maxd = bc_analyze_stack(main_fn, NULL, 0);
    fprintf(out, "int main(void){\n");
    fprintf(out, "    Value __stk[%d];\n", maxd + 2);
    fprintf(out, "    int __sp = 0;\n");
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
