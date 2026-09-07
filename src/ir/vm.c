// 字节码 VM 执行器
// 指令语义与 ast_interp.c 对齐（栈帧链变量、调用绑定、return 深拷贝、break/continue 编译期跳转）。
#include "vm.h"
#include "ast/stackframe.h"
#include "ast/func_compile.h"
#include "ast/ast_runtime_sym.h"
#include "runtime/lm_value.h"
#include "runtime/lm_runtime.h"
#include "runtime/gc_runtime.h"
#include "runtime/lm_thread.h"
#include "runtime/lm_lock.h"
#include "runtime/lm_tls.h"
#include "runtime/lm_http.h"
#include "runtime/lm_json.h"
#include "runtime/lm_qs.h"
#include "runtime/lm_charset.h"
#include "runtime/lm_crypto.h"
#include "runtime/lm_regex.h"
#include "runtime/lm_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

/* try/catch 错误处理器栈（VM 侧；C 生成侧用局部 jmp_buf）：动态扩容，无硬上限。
 * 注意 jmp_buf 经 realloc 移动时内容整体拷贝，setjmp 后再 longjmp(vm_jbs[d]) 语义不变。 */
static _Thread_local jmp_buf* vm_jbs = NULL;
static _Thread_local jmp_buf** vm_prev = NULL;
static _Thread_local int vm_depth = 0;
static _Thread_local int* vm_sp = NULL;
static _Thread_local int* vm_target = NULL;   /* 每层的 catch 目标（longjmp 后自动变量不可靠） */
static _Thread_local int* vm_tn = NULL;       /* 每层 TRY 时的调用栈深度（GET_ERR 截断残留） */
static _Thread_local int* vm_fn = NULL;       /* 每层 TRY 时的 finally 完成栈深度 */
static _Thread_local int* vm_fin_act = NULL;  /* finally 完成动作：1=JMP 2=RETHROW 3=BREAK 4=CONT 5=RETURN */
static _Thread_local int* vm_fin_tgt = NULL;
static _Thread_local int* vm_fin_dep = NULL;  /* FIN_PUSH 时的恢复深度（FINISH act=1/3/4 恢复，防循环内 depth 漂移） */
static _Thread_local int vm_fin_n = 0;
static _Thread_local int vm_cap = 0;          /* 错误处理器栈容量 */
static _Thread_local Value vm_pend_val;   /* 挂起返回的值（PEND_RETURN 存，FINISH act5 恢复） */

static void vm_ensure(int need)
{
    if(need <= vm_cap) return;
    int nc = vm_cap > 0 ? vm_cap * 2 : 64;
    jmp_buf* nj = (jmp_buf*)realloc(vm_jbs, (size_t)nc * sizeof(jmp_buf));
    if(!nj) { fprintf(stderr, "vm: try 栈扩容内存不足\n"); exit(EXIT_FAILURE); }
    vm_jbs = nj;
    jmp_buf** np = (jmp_buf**)realloc(vm_prev, (size_t)nc * sizeof(jmp_buf*));
    if(!np) { fprintf(stderr, "vm: try 栈扩容内存不足\n"); exit(EXIT_FAILURE); }
    vm_prev = np;
    int* na = (int*)realloc(vm_sp, (size_t)nc * sizeof(int));
    if(!na) { fprintf(stderr, "vm: try 栈扩容内存不足\n"); exit(EXIT_FAILURE); }
    vm_sp = na;
    int* nt = (int*)realloc(vm_target, (size_t)nc * sizeof(int));
    if(!nt) { fprintf(stderr, "vm: try 栈扩容内存不足\n"); exit(EXIT_FAILURE); }
    vm_target = nt;
    int* nn = (int*)realloc(vm_tn, (size_t)nc * sizeof(int));
    if(!nn) { fprintf(stderr, "vm: try 栈扩容内存不足\n"); exit(EXIT_FAILURE); }
    vm_tn = nn;
    int* nf = (int*)realloc(vm_fn, (size_t)nc * sizeof(int));
    if(!nf) { fprintf(stderr, "vm: try 栈扩容内存不足\n"); exit(EXIT_FAILURE); }
    vm_fn = nf;
    int* nfa = (int*)realloc(vm_fin_act, (size_t)nc * sizeof(int));
    if(!nfa) { fprintf(stderr, "vm: try 栈扩容内存不足\n"); exit(EXIT_FAILURE); }
    vm_fin_act = nfa;
    int* nft = (int*)realloc(vm_fin_tgt, (size_t)nc * sizeof(int));
    if(!nft) { fprintf(stderr, "vm: try 栈扩容内存不足\n"); exit(EXIT_FAILURE); }
    vm_fin_tgt = nft;
    int* nfd = (int*)realloc(vm_fin_dep, (size_t)nc * sizeof(int));
    if(!nfd) { fprintf(stderr, "vm: try 栈扩容内存不足\n"); exit(EXIT_FAILURE); }
    vm_fin_dep = nfd;
    vm_cap = nc;
}

// 未定义变量/函数：统一报错退出（与 ast_interp.c 输出一致）
static void runtime_undefined(const char* what, const char* name)
{
    fprintf(stderr, "Runtime Error: 未定义%s: %s\n", what, name);
    exit(EXIT_FAILURE);
}

static Value vm_run(BytecodeFunc* bf, StackFrame* frame, EvalCtx* ctx);


// 线程参数（VM 通道）：函数 + 全局帧。全局变量存 vm_run_main 的顶层帧，
// 线程函数经 parent 链访问；data 由线程体消费后 free。
typedef struct {
    RuntimeFunc* rf;
    StackFrame* global_frame;
} VmThreadArg;

/* 当前线程的全局帧（主线程 = vm_run_main 的 top；线程体启动时从 data 继承并写入本线程 TLS） */
static _Thread_local StackFrame* s_global_frame = NULL;

// 线程体（VM 通道）：线程内执行 RuntimeFunc，与 vm_call_rf 语义一致
static void vm_thread_body(ThreadLaunch* t)
{
    VmThreadArg* a = (VmThreadArg*)t->data;
    RuntimeFunc* rf = a->rf;
    StackFrame* saved_global = s_global_frame;
    s_global_frame = a->global_frame;
    StackFrame* callee = stackframe_new(a->global_frame);
    if(interp_func_is_payload(rf)) {
        int pcnt = interp_func_param_cnt(rf);
        int i = 0;
        for(; i < pcnt; i++) {
            const char* pname = interp_func_param_name(rf, i);
            Value bound = (i < t->argc) ? t->args[i] : val_none();
            stackframe_bind(callee, pname, bound);
        }
        if(interp_func_has_variadic(rf)) {
            const char* vname = interp_func_param_name(rf, pcnt);
            int rest = t->argc - i;
            if(rest < 0) rest = 0;
            Value arr = val_array(rest);
            for(int k = 0; k < rest; k++)
                arr.v.array->items[k] = t->args[i + k];
            stackframe_bind(callee, vname, arr);
        }
    }
    RuntimeFunc* prev_rf = interp_set_current_rf(rf);
    EvalCtx ctx = {0};
    g_trace_push("<thread>");
    Value r = rf->entry(t->argc, t->args, &ctx, callee);
    if(g_trace_n > 0) g_trace_n--;
    interp_set_current_rf(prev_rf);
    stackframe_destroy(callee);
    s_global_frame = saved_global;
    lumin_thread_set_result(t, r);
    free(a);
}

// 通过函数值调用（高阶函数内部使用）：与 OPC_CALL 的调用语义一致
static Value vm_call_rf(RuntimeFunc* rf, Value* args, int argc, StackFrame* parent, EvalCtx* ctx)
{
    StackFrame* callee = stackframe_new(parent);
    if(interp_func_is_payload(rf)) {
        int pcnt = interp_func_param_cnt(rf);
        int i = 0;
        for(; i < pcnt; i++) {
            const char* pname = interp_func_param_name(rf, i);
            Value bound = (i < argc) ? args[i] : val_none();
            stackframe_bind(callee, pname, bound);
        }
        if(interp_func_has_variadic(rf)) {
            const char* vname = interp_func_param_name(rf, pcnt);
            int rest = argc - i;
            if(rest < 0) rest = 0;
            Value arr = val_array(rest);
            for(int k = 0; k < rest; k++) {
                arr.v.array->items[k] = args[i + k];
            }
            stackframe_bind(callee, vname, arr);
        }
    }
    RuntimeFunc* prev_rf = interp_set_current_rf(rf);
    int saved_break = ctx->hit_break;
    int saved_cont = ctx->hit_continue;
    ctx->hit_break = 0;
    ctx->hit_continue = 0;
    g_trace_push("<anonymous>");
    Value ret = rf->entry(argc, args, ctx, callee);
    if(g_trace_n > 0) g_trace_n--;
    ctx->hit_break = saved_break;
    ctx->hit_continue = saved_cont;
    interp_set_current_rf(prev_rf);
    stackframe_destroy(callee);
    return ret;
}

Value vm_run_main(BytecodeFunc* main_fn)
{
    EvalCtx local_ctx = {0};
    StackFrame* top = stackframe_new(NULL);
    stackframe_set_shared(top);              // 全局共享帧：多线程沿 parent 链访问需加锁
    stackframe_set(top, "log", val_map());   // 预定义 log 对象（方法链 log.xxx）
    StackFrame* saved_global = s_global_frame;
    s_global_frame = top;
    Value ret = vm_run(main_fn, top, &local_ctx);
    s_global_frame = saved_global;
    stackframe_destroy(top);
    return ret;
}

// 函数入口：帧已由调用点建好并绑定参数，这里直接执行函数体字节码
Value vm_func_entry(int arg_cnt, const Value* args, EvalCtx* ctx, StackFrame* frame)
{
    (void)arg_cnt;
    (void)args;
    RuntimeFunc* self = interp_current_rf();
    if(!self || !interp_func_is_payload(self)) {
        runtime_error("vm_func_entry: 缺少当前函数上下文");
        return val_none();
    }
    InterpFuncPayload* pl = (InterpFuncPayload*)self->captures;
    if(!pl->bytecode) return val_none();
    return vm_run(pl->bytecode, frame, ctx);
}

static Value vm_run(BytecodeFunc* bf, StackFrame* frame, EvalCtx* ctx)
{
    // 静态栈深度分析：精确分配执行栈（动态，无硬上限），并校验 IR 栈平衡
    int maxd = bc_analyze_stack(bf, NULL, 0);
    if(maxd < 0) exit(EXIT_FAILURE);   // 已打印下溢位置
    Value* stack = (Value*)malloc(sizeof(Value) * (maxd + 2));
    if(!stack) { perror("vm_run"); exit(EXIT_FAILURE); }
    int sp = 0;
    int pc = 0;
    /* 注册 GC 根：当前线程的 VM 栈与帧链，供 gc_alloc 自动触发回收时扫描 */
    gc_set_roots(stack, &sp, frame);
    /* 函数边界隔离 try 状态：进入保存，所有退出点恢复（try 内 return 不能泄漏） */
    int saved_depth = vm_depth;
    jmp_buf* saved_gj = g_err_jmp;
    int saved_fin = vm_fin_n;

    if(getenv("LUMIN_BC_DUMP")) {
        fprintf(stderr, "== bc dump: %s (code_len=%d, max_stack=%d) ==\n",
                bf->name ? bf->name : "<main>", bf->code_len, maxd);
        for(int i = 0; i < bf->code_len; i++) {
            Instruction in = bf->code[i];
            const char* n = (in.a >= 0 && in.a < bf->sym_cnt) ? bf->syms[in.a] : "?";
            fprintf(stderr, "  %4d: op=%d a=%d(%s) b=%d\n", i, (int)in.op, in.a, n, in.b);
        }
    }

    for(;;) {
        Instruction in = bf->code[pc++];
        switch(in.op) {
            case OPC_NOP:
                break;
            case OPC_LOAD_CONST:
                stack[sp++] = bf->consts[in.a];
                break;
            case OPC_GETFUNC: {
                const char* fname = bf->syms[in.a];
                Value fv = val_none();
                if(sym_has(fname)) fv = sym_get(fname);
                else runtime_undefined("函数", fname);
                stack[sp++] = fv;
                break;
            }
            case OPC_LOAD_VAR: {
                const char* name = bf->syms[in.a];
                _Bool fnd = 0;
                Value vv = stackframe_get(frame, name, &fnd);
                if(!fnd) runtime_undefined("变量", name);
                stack[sp++] = vv;
                break;
            }
            case OPC_STORE_VAR: {
                const char* name = bf->syms[in.a];
                Value v = stack[--sp];
                /* 词法遮蔽：函数内赋值 = 绑定当前帧局部（C 语义：局部变量遮蔽全局同名）；
                   不再沿链更新父帧/全局。顶层（main 帧）赋值仍写入全局帧。 */
                stackframe_bind(frame, name, v);
                stack[sp++] = v;             // 原值压回（表达式值）
                break;
            }
            case OPC_ADD: { Value r = stack[--sp], l = stack[--sp]; stack[sp++] = lumin_add(l, r); break; }
            case OPC_SUB: { Value r = stack[--sp], l = stack[--sp]; stack[sp++] = lumin_sub(l, r); break; }
            case OPC_MUL: { Value r = stack[--sp], l = stack[--sp]; stack[sp++] = lumin_mul(l, r); break; }
            case OPC_DIV: { Value r = stack[--sp], l = stack[--sp]; stack[sp++] = lumin_div(l, r); break; }
            case OPC_MOD: { Value r = stack[--sp], l = stack[--sp]; stack[sp++] = lumin_mod(l, r); break; }
            case OPC_GT:  { Value r = stack[--sp], l = stack[--sp]; stack[sp++] = lumin_gt(l, r); break; }
            case OPC_LT:  { Value r = stack[--sp], l = stack[--sp]; stack[sp++] = lumin_lt(l, r); break; }
            case OPC_GE:  { Value r = stack[--sp], l = stack[--sp]; stack[sp++] = lumin_ge(l, r); break; }
            case OPC_LE:  { Value r = stack[--sp], l = stack[--sp]; stack[sp++] = lumin_le(l, r); break; }
            case OPC_EQ:  { Value r = stack[--sp], l = stack[--sp]; stack[sp++] = lumin_eq(l, r); break; }
            case OPC_NE:  { Value r = stack[--sp], l = stack[--sp]; stack[sp++] = lumin_ne(l, r); break; }
            case OPC_NEG: { Value v = stack[--sp]; stack[sp++] = lumin_unary_minus(v); break; }
            case OPC_POS: { Value v = stack[--sp]; stack[sp++] = lumin_unary_plus(v); break; }
            case OPC_PRE_INC:  { const char* n = bf->syms[in.a]; _Bool fnd = 0;
                                 Value __old = stackframe_get(frame, n, &fnd);
                                 if(!fnd) runtime_undefined("变量", n);
                                 Value __nv = lumin_pre_inc(&__old);
                                 stackframe_bind(frame, n, __nv);          // 词法遮蔽：写当前帧
                                 stack[sp++] = __nv; break; }
            case OPC_POST_INC: { const char* n = bf->syms[in.a]; _Bool fnd = 0;
                                 Value __old = stackframe_get(frame, n, &fnd);
                                 if(!fnd) runtime_undefined("变量", n);
                                 Value __nv = lumin_post_inc(&__old);
                                 stackframe_bind(frame, n, __old);          // 参数已被改为新值
                                 stack[sp++] = __nv; break; }               // 返回值 = 旧值
            case OPC_PRE_DEC:  { const char* n = bf->syms[in.a]; _Bool fnd = 0;
                                 Value __old = stackframe_get(frame, n, &fnd);
                                 if(!fnd) runtime_undefined("变量", n);
                                 Value __nv = lumin_pre_dec(&__old);
                                 stackframe_bind(frame, n, __nv);          // 词法遮蔽：写当前帧
                                 stack[sp++] = __nv; break; }
            case OPC_POST_DEC: { const char* n = bf->syms[in.a]; _Bool fnd = 0;
                                 Value __old = stackframe_get(frame, n, &fnd);
                                 if(!fnd) runtime_undefined("变量", n);
                                 Value __nv = lumin_post_dec(&__old);
                                 stackframe_bind(frame, n, __old);          // 参数已被改为新值
                                 stack[sp++] = __nv; break; }               // 返回值 = 旧值
            case OPC_CAST_INT:    { Value v = stack[--sp]; stack[sp++] = lumin_cast_int(v); break; }
            case OPC_CAST_DOUBLE: { Value v = stack[--sp]; stack[sp++] = lumin_cast_double(v); break; }
            case OPC_CAST_CHAR:   { Value v = stack[--sp]; stack[sp++] = lumin_cast_char(v); break; }
            case OPC_CAST_BOOL:   { Value v = stack[--sp]; stack[sp++] = lumin_cast_bool(v); break; }
            case OPC_CAST_STRING: { Value v = stack[--sp]; stack[sp++] = lumin_cast_string(v); break; }
            case OPC_CAST_ASCII:  { Value v = stack[--sp]; stack[sp++] = lumin_cast_ascii(v); break; }
            case OPC_CAST_BYTE:   { Value v = stack[--sp]; stack[sp++] = lumin_cast_byte(v); break; }
            case OPC_CAST_INT8:   { Value v = stack[--sp]; stack[sp++] = lumin_cast_int8(v); break; }
            case OPC_CAST_INT16:  { Value v = stack[--sp]; stack[sp++] = lumin_cast_int16(v); break; }
            case OPC_CAST_INT32:  { Value v = stack[--sp]; stack[sp++] = lumin_cast_int32(v); break; }
            case OPC_CAST_INT64:  { Value v = stack[--sp]; stack[sp++] = lumin_cast_int64(v); break; }
            case OPC_CAST_UINT8:  { Value v = stack[--sp]; stack[sp++] = lumin_cast_uint8(v); break; }
            case OPC_CAST_UINT16: { Value v = stack[--sp]; stack[sp++] = lumin_cast_uint16(v); break; }
            case OPC_CAST_UINT32: { Value v = stack[--sp]; stack[sp++] = lumin_cast_uint32(v); break; }
            case OPC_CAST_UINT64: { Value v = stack[--sp]; stack[sp++] = lumin_cast_uint64(v); break; }
            case OPC_CAST_LONG: { Value v = stack[--sp]; stack[sp++] = lumin_cast_long(v); break; }
            case OPC_CAST_LONGLONG: { Value v = stack[--sp]; stack[sp++] = lumin_cast_longlong(v); break; }
            case OPC_CAST_FLOAT: { Value v = stack[--sp]; stack[sp++] = lumin_cast_float(v); break; }
            case OPC_LOGIC_NOT:   { Value v = stack[--sp]; stack[sp++] = lumin_logic_not(v); break; }
            case OPC_ARRAY_LIT: {
                int n = in.b;
                Value arr = val_array(n);
                for(int k = 0; k < n; k++)
                    arr.v.array->items[k] = stack[sp - n + k];
                sp = sp - n + 1;
                stack[sp - 1] = arr;
                break;
            }
            case OPC_MAP_LIT: {
                int n = in.b;
                Value m = lumin_map_lit(&stack[sp - 2 * n], n);
                sp = sp - 2 * n + 1;
                stack[sp - 1] = m;
                break;
            }
            case OPC_INDEX_GET: {
                Value idx = stack[--sp];
                Value c = stack[--sp];
                stack[sp++] = lumin_index_get(c, idx);
                break;
            }
            case OPC_INDEX_SET: {
                Value val = stack[--sp];
                Value idx = stack[--sp];
                Value arr = stack[--sp];
                stack[sp++] = lumin_array_set(arr, idx, val);
                break;
            }
            case OPC_BUILTIN: {
                int argc = in.b;
                switch(in.a) {
                    case BUILTIN_LEN: {
                        Value v = stack[--sp];
                        stack[sp++] = lumin_len(v);
                        break;
                    }
                    case BUILTIN_TYPE: {
                        Value v = stack[--sp];
                        stack[sp++] = lumin_type(v);
                        break;
                    }
                    case BUILTIN_INPUT: {
                        stack[sp++] = lumin_input();
                        break;
                    }
                    case BUILTIN_RANGE: {
                        int n = in.b;
                        Value r = lumin_range_n(&stack[sp - n], n);
                        stack[sp - n] = r;
                        sp = sp - n + 1;
                        break;
                    }
                    case BUILTIN_SUBSTR: {
                        Value n = stack[--sp];
                        Value st = stack[--sp];
                        Value s = stack[--sp];
                        stack[sp++] = lumin_substr(s, st, n);
                        break;
                    }
                    case BUILTIN_TOUPPER: {
                        Value v = stack[--sp];
                        stack[sp++] = lumin_toupper(v);
                        break;
                    }
                    case BUILTIN_TOLOWER: {
                        Value v = stack[--sp];
                        stack[sp++] = lumin_tolower(v);
                        break;
                    }
                    case BUILTIN_SPLIT: {
                        Value sep = stack[--sp];
                        Value s = stack[--sp];
                        stack[sp++] = lumin_split(s, sep);
                        break;
                    }
                    case BUILTIN_DEL: {
                        Value idx = stack[--sp];
                        lumin_del(&stack[sp-1], idx);
                        break;
                    }
                    case BUILTIN_INSERT: {
                        Value val = stack[--sp];
                        Value idx = stack[--sp];
                        lumin_insert(&stack[sp-1], idx, val);
                        break;
                    }
                    case BUILTIN_FLOOR: { Value v = stack[--sp]; stack[sp++] = lumin_floor(v); break; }
                    case BUILTIN_CEIL:  { Value v = stack[--sp]; stack[sp++] = lumin_ceil(v); break; }
                    case BUILTIN_ABS:   { Value v = stack[--sp]; stack[sp++] = lumin_abs(v); break; }
                    case BUILTIN_SQRT:  { Value v = stack[--sp]; stack[sp++] = lumin_sqrt(v); break; }
                    case BUILTIN_MAX:
                    case BUILTIN_MIN: {
                        int n = in.b;
                        Value r = (in.a == BUILTIN_MAX) ? lumin_max(&stack[sp - n], n) : lumin_min(&stack[sp - n], n);
                        stack[sp - n] = r;
                        sp = sp - n + 1;
                        break;
                    }
                    case BUILTIN_JOIN: {
                        Value sep = stack[--sp];
                        Value arr = stack[--sp];
                        stack[sp++] = lumin_join(arr, sep);
                        break;
                    }
                    case BUILTIN_CONTAINS: {
                        Value needle = stack[--sp];
                        Value hay = stack[--sp];
                        stack[sp++] = lumin_contains(hay, needle);
                        break;
                    }
                    case BUILTIN_REPEAT: {
                        Value n = stack[--sp];
                        Value s = stack[--sp];
                        stack[sp++] = lumin_repeat(s, n);
                        break;
                    }
                    case BUILTIN_REPLACE: {
                        Value to = stack[--sp];
                        Value from = stack[--sp];
                        Value s = stack[--sp];
                        stack[sp++] = lumin_replace(s, from, to);
                        break;
                    }
                    case BUILTIN_SUM: { Value v = stack[--sp]; stack[sp++] = lumin_sum(v); break; }
                    case BUILTIN_AVG: { Value v = stack[--sp]; stack[sp++] = lumin_avg(v); break; }
                    case BUILTIN_FORMAT: {
                        int n = in.b;
                        Value r = lumin_format(&stack[sp - n], n);
                        stack[sp - n] = r;
                        sp = sp - n + 1;
                        break;
                    }
                    case BUILTIN_SORT:    { Value v = stack[--sp]; stack[sp++] = lumin_sort(v); break; }
                    case BUILTIN_REVERSE:{ Value v = stack[--sp]; stack[sp++] = lumin_reverse(v); break; }
                    case BUILTIN_STRIP:   { Value v = stack[--sp]; stack[sp++] = lumin_strip(v); break; }
                    case BUILTIN_STARTSWITH: { Value r = stack[--sp], l = stack[--sp]; stack[sp++] = lumin_startswith(l, r); break; }
                    case BUILTIN_ENDSWITH:   { Value r = stack[--sp], l = stack[--sp]; stack[sp++] = lumin_endswith(l, r); break; }
                    case BUILTIN_READ_FILE:  { int n2 = in.b; Value r = lumin_read_file(&stack[sp - n2], n2); stack[sp - n2] = r; sp = sp - n2 + 1; break; }
                    case BUILTIN_WRITE_FILE: { int n2 = in.b; Value r = lumin_write_file(&stack[sp - n2], n2); stack[sp - n2] = r; sp = sp - n2 + 1; break; }
                    case BUILTIN_FILE_EXISTS:{ int n2 = in.b; Value r = lumin_file_exists(&stack[sp - n2], n2); stack[sp - n2] = r; sp = sp - n2 + 1; break; }
                    case BUILTIN_KEYS:     { int n2 = in.b; Value r = lumin_map_keys(stack[sp - n2]); stack[sp - n2] = r; sp = sp - n2 + 1; break; }
                    case BUILTIN_VALUES:   { int n2 = in.b; Value r = lumin_map_values(stack[sp - n2]); stack[sp - n2] = r; sp = sp - n2 + 1; break; }
                    case BUILTIN_THREAD: {
                        int argc = in.b;
                        if(argc < 1) runtime_error("thread() 至少需要一个函数参数");
                        Value fn = stack[sp - argc];
                        if(fn.type != VAL_FUNC) runtime_error("thread() 第一个参数必须是函数");
                        RuntimeFunc* rf = fn.v.func.func_obj;
                        int nargs = argc - 1;
                        VmThreadArg* a = (VmThreadArg*)malloc(sizeof(VmThreadArg));
                        if(!a) runtime_error("thread: 内存不足");
                        a->rf = rf;
                        a->global_frame = s_global_frame;
                        int tid = lumin_thread_start(vm_thread_body, (void*)a, (nargs > 0) ? &stack[sp - nargs] : NULL, nargs);
                        stack[sp - argc] = lumin_make_int(tid);
                        sp = sp - argc + 1;
                        break;
                    }
                    case BUILTIN_THREAD_JOIN: {
                        Value idv = stack[--sp];
                        if(idv.type != VAL_INT) runtime_error("thread_join() 参数必须是线程id（整数）");
                        stack[sp++] = lumin_thread_join((int)idv.v.i);
                        break;
                    }
                    case BUILTIN_MUTEX:    { stack[sp++] = lumin_make_int(lumin_mutex_create()); break; }
                    case BUILTIN_RMUTEX:   { stack[sp++] = lumin_make_int(lumin_rmutex_create()); break; }
                    case BUILTIN_RWLOCK:   { stack[sp++] = lumin_make_int(lumin_rwlock_create()); break; }
                    case BUILTIN_SPINLOCK: { stack[sp++] = lumin_make_int(lumin_spinlock_create()); break; }
                    case BUILTIN_LOCK:   { Value v = stack[--sp]; if(v.type != VAL_INT) runtime_error("lock() 参数必须是锁id（整数）"); lumin_lock((int)v.v.i); stack[sp++] = v; break; }
                    case BUILTIN_UNLOCK: { Value v = stack[--sp]; if(v.type != VAL_INT) runtime_error("unlock() 参数必须是锁id（整数）"); lumin_unlock((int)v.v.i); stack[sp++] = v; break; }
                    case BUILTIN_TRYLOCK: {
                        Value v = stack[--sp];
                        if(v.type != VAL_INT) runtime_error("trylock() 参数必须是锁id（整数）");
                        stack[sp++] = lumin_make_bool(lumin_trylock((int)v.v.i));
                        break;
                    }
                    case BUILTIN_RDLOCK: { Value v = stack[--sp]; if(v.type != VAL_INT) runtime_error("rdlock() 参数必须是锁id（整数）"); lumin_rdlock((int)v.v.i); stack[sp++] = v; break; }
                    case BUILTIN_WRLOCK: { Value v = stack[--sp]; if(v.type != VAL_INT) runtime_error("wrlock() 参数必须是锁id（整数）"); lumin_wrlock((int)v.v.i); stack[sp++] = v; break; }
                    case BUILTIN_TRYRDLOCK: {
                        Value v = stack[--sp];
                        if(v.type != VAL_INT) runtime_error("tryrdlock() 参数必须是锁id（整数）");
                        stack[sp++] = lumin_make_bool(lumin_tryrdlock((int)v.v.i));
                        break;
                    }
                    case BUILTIN_TRYWRLOCK: {
                        Value v = stack[--sp];
                        if(v.type != VAL_INT) runtime_error("trywrlock() 参数必须是锁id（整数）");
                        stack[sp++] = lumin_make_bool(lumin_trywrlock((int)v.v.i));
                        break;
                    }
                    case BUILTIN_CONDVAR: { stack[sp++] = lumin_make_int(lumin_condvar_create()); break; }
                    case BUILTIN_COND_WAIT: {
                        Value lk = stack[--sp];
                        Value cd = stack[--sp];
                        if(lk.type != VAL_INT) runtime_error("cond_wait() 锁参数必须是锁id（整数）");
                        if(cd.type != VAL_INT) runtime_error("cond_wait() 条件参数必须是条件id（整数）");
                        lumin_cond_wait((int)cd.v.i, (int)lk.v.i);
                        stack[sp++] = lk;    // 压回原值（表达式值）
                        break;
                    }
                    case BUILTIN_COND_SIGNAL: { Value v = stack[--sp]; if(v.type != VAL_INT) runtime_error("cond_signal() 参数必须是条件id（整数）"); lumin_cond_signal((int)v.v.i); stack[sp++] = v; break; }
                    case BUILTIN_COND_BROADCAST: { Value v = stack[--sp]; if(v.type != VAL_INT) runtime_error("cond_broadcast() 参数必须是条件id（整数）"); lumin_cond_broadcast((int)v.v.i); stack[sp++] = v; break; }
                    case BUILTIN_COND_TIMEDWAIT: {
                        Value ms = stack[--sp];
                        Value lk = stack[--sp];
                        Value cd = stack[--sp];
                        if(ms.type != VAL_INT) runtime_error("cond_wait_timeout() 超时参数必须是整数毫秒");
                        if(lk.type != VAL_INT) runtime_error("cond_wait_timeout() 锁参数必须是锁id（整数）");
                        if(cd.type != VAL_INT) runtime_error("cond_wait_timeout() 条件参数必须是条件id（整数）");
                        stack[sp++] = lumin_make_bool(lumin_cond_timedwait((int)cd.v.i, (int)lk.v.i, ms.v.i));
                        break;
                    }
                    case BUILTIN_THREADLOCAL_GET: {
                        Value nm = stack[--sp];
                        if(nm.type != VAL_STRING) runtime_error("threadlocal_get() 名字参数必须是字符串");
                        stack[sp++] = lumin_tls_get(nm.v.s);
                        break;
                    }
                    case BUILTIN_THREADLOCAL_SET: {
                        Value v = stack[--sp];
                        Value nm = stack[--sp];
                        if(nm.type != VAL_STRING) runtime_error("threadlocal_set() 名字参数必须是字符串");
                        lumin_tls_set(nm.v.s, v);
                        stack[sp++] = v;    // 压回原值（表达式值）
                        break;
                    }
                    case BUILTIN_HTTP_GET:
                    case BUILTIN_HTTP_POST:
                    case BUILTIN_HTTP_PUT:
                    case BUILTIN_ARRAY_ADD: {
                        if(in.b == 3) {
                            Value v = stack[--sp];
                            Value k = stack[--sp];
                            Value m = stack[--sp];
                            stack[sp++] = lumin_map_add(m, k, v);
                        } else {
                            Value v = stack[--sp];
                            lumin_array_add(&stack[sp-1], v);
                        }
                        break;
                    }
                    case BUILTIN_ARRAY_REMOVE: {
                        Value idx = stack[--sp];
                        lumin_del(&stack[sp-1], idx);
                        break;
                    }
                    case BUILTIN_ARRAY_INDEXOF: {
                        Value x = stack[--sp];
                        Value arr = stack[--sp];
                        stack[sp++] = lumin_index_of(arr, x);
                        break;
                    }
                    case BUILTIN_ARRAY_GET: {
                        Value i = stack[--sp];
                        Value arr = stack[--sp];
                        stack[sp++] = lumin_array_get_safe(arr, i);
                        break;
                    }
                    case BUILTIN_ARRAY_SET: {
                        Value v = stack[--sp];
                        Value i = stack[--sp];
                        Value arr = stack[--sp];
                        stack[sp++] = lumin_array_set_method(arr, i, v);
                        break;
                    }
                    case BUILTIN_ARRAY_FIRST: {
                        Value arr = stack[--sp];
                        stack[sp++] = lumin_array_first(arr);
                        break;
                    }
                    case BUILTIN_ARRAY_LAST: {
                        Value arr = stack[--sp];
                        stack[sp++] = lumin_array_last(arr);
                        break;
                    }
                    case BUILTIN_ARRAY_CLEAR: {
                        lumin_array_clear(&stack[sp-1]);
                        break;
                    }
                    case BUILTIN_MAP_HAS: {
                        Value k = stack[--sp];
                        Value m = stack[--sp];
                        stack[sp++] = lumin_make_bool(lumin_map_has(m, k));
                        break;
                    }
                    case BUILTIN_JSON: {
                        Value enc = val_none();
                        Value v;
                        if(in.b >= 2) { enc = stack[--sp]; v = stack[--sp]; }
                        else { v = stack[--sp]; }
                        stack[sp++] = lumin_json_parse_enc(v.v.s, enc);
                        break;
                    }
                    case BUILTIN_STRINGIFY: {
                        Value enc = val_none();
                        Value v;
                        if(in.b >= 2) { enc = stack[--sp]; v = stack[--sp]; }
                        else { v = stack[--sp]; }
                        char* js = lumin_json_stringify_enc(v, enc);
                        Value r = lumin_make_string(js);
                        free(js);
                        stack[sp++] = r;
                        break;
                    }
                    case BUILTIN_ARRAY_FLAT: {
                        Value depth = lumin_make_int(1);
                        Value v;
                        if(in.b >= 2) { depth = stack[--sp]; v = stack[--sp]; }
                        else { v = stack[--sp]; }
                        stack[sp++] = lumin_array_flat(v, lumin_extract_int(depth));
                        break;
                    }
                    case BUILTIN_QS: {
                        Value enc = val_none();
                        Value v;
                        if(in.b >= 2) { enc = stack[--sp]; v = stack[--sp]; }
                        else { v = stack[--sp]; }
                        if(v.type == VAL_MAP || v.type == VAL_ARRAY) {
                            char* q = lumin_qs_stringify_enc(v, enc);
                            stack[sp++] = lumin_make_string(q);
                            free(q);
                        } else if(v.type == VAL_STRING) {
                            stack[sp++] = lumin_qs_parse_enc(v.v.s, enc);
                        } else {
                            runtime_error("qs() 参数必须是字典/数组（序列化）或字符串（解析）");
                        }
                        break;
                    }
                    case BUILTIN_ARRAY_ADDALL: {
                        Value b = stack[--sp];
                        lumin_array_addall(&stack[sp-1], b);
                        break;
                    }
                    case BUILTIN_BYTES: {
                        Value enc = val_none();
                        Value v;
                        if(in.b >= 2) { enc = stack[--sp]; v = stack[--sp]; }
                        else { v = stack[--sp]; }
                        stack[sp++] = lumin_to_bytes(v, enc);
                        break;
                    }
                    case BUILTIN_STR: {
                        Value enc = val_none();
                        Value v;
                        if(in.b >= 2) { enc = stack[--sp]; v = stack[--sp]; }
                        else { v = stack[--sp]; }
                        stack[sp++] = lumin_from_bytes(v, enc);
                        break;
                    }
                    case BUILTIN_ENCODE: {
                        Value enc = val_none();
                        Value v;
                        if(in.b >= 2) { enc = stack[--sp]; v = stack[--sp]; }
                        else { v = stack[--sp]; }
                        stack[sp++] = lumin_to_bytes(v, enc);
                        break;
                    }
                    case BUILTIN_DECODE: {
                        Value enc = val_none();
                        Value v;
                        if(in.b >= 2) { enc = stack[--sp]; v = stack[--sp]; }
                        else { v = stack[--sp]; }
                        stack[sp++] = lumin_from_bytes(v, enc);
                        break;
                    }
                    case BUILTIN_ENCODE_URL: {
                        Value v = stack[--sp];
                        char* r = lumin_url_encode(v.type == VAL_STRING ? (v.v.s ? v.v.s : "") : "");
                        stack[sp++] = lumin_make_string(r);
                        free(r);
                        break;
                    }
                    case BUILTIN_DECODE_URL: {
                        Value v = stack[--sp];
                        char* r = lumin_url_decode(v.type == VAL_STRING ? (v.v.s ? v.v.s : "") : "");
                        stack[sp++] = lumin_make_string(r);
                        free(r);
                        break;
                    }
                    case BUILTIN_MD5: {
                        Value v = stack[--sp];
                        const char* inp = v.type == VAL_STRING ? (v.v.s ? v.v.s : "") : "";
                        char* r = lumin_md5_hex(inp, (int)strlen(inp));
                        stack[sp++] = lumin_make_string(r);
                        free(r);
                        break;
                    }
                    case BUILTIN_ENCODE_BASE64: {
                        Value v = stack[--sp];
                        const char* inp = v.type == VAL_STRING ? (v.v.s ? v.v.s : "") : "";
                        char* r = lumin_base64_encode(inp, (int)strlen(inp));
                        stack[sp++] = lumin_make_string(r);
                        free(r);
                        break;
                    }
                    case BUILTIN_DECODE_BASE64: {
                        Value v = stack[--sp];
                        int olen = 0;
                        char* r = lumin_base64_decode(v.type == VAL_STRING ? (v.v.s ? v.v.s : "") : "", &olen);
                        stack[sp++] = lumin_make_string(r);
                        free(r);
                        break;
                    }
                    case BUILTIN_REGEX_MATCH: {
                        Value pat = stack[--sp];
                        Value str = stack[--sp];
                        stack[sp++] = lumin_make_bool(lumin_regex_match(
                            str.type == VAL_STRING ? (str.v.s ? str.v.s : "") : "",
                            pat.type == VAL_STRING ? (pat.v.s ? pat.v.s : "") : ""));
                        break;
                    }
                    case BUILTIN_REGEX_SEARCH: {
                        Value pat = stack[--sp];
                        Value str = stack[--sp];
                        stack[sp++] = lumin_regex_search(
                            str.type == VAL_STRING ? (str.v.s ? str.v.s : "") : "",
                            pat.type == VAL_STRING ? (pat.v.s ? pat.v.s : "") : "");
                        break;
                    }
                    case BUILTIN_REGEX_REPLACE: {
                        Value repl = stack[--sp];
                        Value pat = stack[--sp];
                        Value str = stack[--sp];
                        char* r = lumin_regex_replace(
                            str.type == VAL_STRING ? (str.v.s ? str.v.s : "") : "",
                            pat.type == VAL_STRING ? (pat.v.s ? pat.v.s : "") : "",
                            repl.type == VAL_STRING ? (repl.v.s ? repl.v.s : "") : "");
                        stack[sp++] = lumin_make_string(r);
                        free(r);
                        break;
                    }
                    case BUILTIN_NOW: {
                        stack[sp++] = lumin_now();
                        break;
                    }
                    case BUILTIN_TIMESTAMP: {
                        stack[sp++] = lumin_make_double(lumin_timestamp());
                        break;
                    }
                    case BUILTIN_TIMESTAMP_MS: {
                        stack[sp++] = lumin_make_int(lumin_timestamp_ms());
                        break;
                    }
                    case BUILTIN_SLEEP: {
                        Value v = stack[--sp];
                        lumin_sleep_ms((long long)lumin_extract_int(v));
                        stack[sp++] = val_none();
                        break;
                    }
                    case BUILTIN_DATE: {
                        char* r = lumin_date_str();
                        stack[sp++] = lumin_make_string(r);
                        free(r);
                        break;
                    }
                    case BUILTIN_TIME: {
                        char* r = lumin_time_str();
                        stack[sp++] = lumin_make_string(r);
                        free(r);
                        break;
                    }
                    case BUILTIN_DATETIME: {
                        char* r = lumin_datetime_str();
                        stack[sp++] = lumin_make_string(r);
                        free(r);
                        break;
                    }
                    case BUILTIN_FORMAT_TIME: {
                        Value ts = val_none();
                        Value fmt;
                        if(in.b >= 2) { ts = stack[--sp]; fmt = stack[--sp]; }
                        else { fmt = stack[--sp]; }
                        double tsv = (ts.type == VAL_NONE) ? -1.0 : (ts.type == VAL_DOUBLE ? ts.v.d : (double)lumin_extract_int(ts));
                        char* r = lumin_format_time(fmt.type == VAL_STRING ? (fmt.v.s ? fmt.v.s : "") : "", tsv);
                        stack[sp++] = lumin_make_string(r);
                        free(r);
                        break;
                    }
                    case BUILTIN_LOG_DEBUG:
                    case BUILTIN_LOG_INFO:
                    case BUILTIN_LOG_WARN:
                    case BUILTIN_LOG_ERROR:
                    case BUILTIN_LOG_FATAL: {
                        int lvl = in.a - BUILTIN_LOG_DEBUG;
                        Value msg;
                        if(in.b >= 2) { msg = stack[--sp]; stack[--sp]; }  // 方法链：先弹消息，再丢弃 receiver
                        else { msg = stack[--sp]; }
                        char* ms = value_to_str(msg);
                        lumin_log(lvl, ms);
                        free(ms);
                        stack[sp++] = val_none();
                        break;
                    }
                    case BUILTIN_HTTP_DELETE:
                    case BUILTIN_HTTP_HEAD:
                    case BUILTIN_HTTP_PATCH: {
                        int n = in.b;
                        if(n < 1 || n > 3) runtime_error("requests 请求需要 1~3 个参数：url、可选 params、可选 config");
                        const char* m = "GET";
                        switch(in.a) {
                            case BUILTIN_HTTP_POST:   m = "POST"; break;
                            case BUILTIN_HTTP_PUT:    m = "PUT"; break;
                            case BUILTIN_ARRAY_ADD: {
                        if(in.b == 3) {
                            Value v = stack[--sp];
                            Value k = stack[--sp];
                            Value m = stack[--sp];
                            stack[sp++] = lumin_map_add(m, k, v);
                        } else {
                            Value v = stack[--sp];
                            lumin_array_add(&stack[sp-1], v);
                        }
                        break;
                    }
                    case BUILTIN_ARRAY_REMOVE: {
                        Value idx = stack[--sp];
                        lumin_del(&stack[sp-1], idx);
                        break;
                    }
                    case BUILTIN_ARRAY_INDEXOF: {
                        Value x = stack[--sp];
                        Value arr = stack[--sp];
                        stack[sp++] = lumin_index_of(arr, x);
                        break;
                    }
                    case BUILTIN_ARRAY_GET: {
                        Value i = stack[--sp];
                        Value arr = stack[--sp];
                        stack[sp++] = lumin_array_get_safe(arr, i);
                        break;
                    }
                    case BUILTIN_ARRAY_SET: {
                        Value v = stack[--sp];
                        Value i = stack[--sp];
                        Value arr = stack[--sp];
                        stack[sp++] = lumin_array_set_method(arr, i, v);
                        break;
                    }
                    case BUILTIN_ARRAY_FIRST: {
                        Value arr = stack[--sp];
                        stack[sp++] = lumin_array_first(arr);
                        break;
                    }
                    case BUILTIN_ARRAY_LAST: {
                        Value arr = stack[--sp];
                        stack[sp++] = lumin_array_last(arr);
                        break;
                    }
                    case BUILTIN_ARRAY_CLEAR: {
                        lumin_array_clear(&stack[sp-1]);
                        break;
                    }
                    case BUILTIN_MAP_HAS: {
                        Value k = stack[--sp];
                        Value m = stack[--sp];
                        stack[sp++] = lumin_make_bool(lumin_map_has(m, k));
                        break;
                    }
                    case BUILTIN_JSON: {
                        Value enc = val_none();
                        Value v;
                        if(in.b >= 2) { enc = stack[--sp]; v = stack[--sp]; }
                        else { v = stack[--sp]; }
                        stack[sp++] = lumin_json_parse_enc(v.v.s, enc);
                        break;
                    }
                    case BUILTIN_STRINGIFY: {
                        Value enc = val_none();
                        Value v;
                        if(in.b >= 2) { enc = stack[--sp]; v = stack[--sp]; }
                        else { v = stack[--sp]; }
                        char* js = lumin_json_stringify_enc(v, enc);
                        Value r = lumin_make_string(js);
                        free(js);
                        stack[sp++] = r;
                        break;
                    }
                    case BUILTIN_ARRAY_FLAT: {
                        Value depth = lumin_make_int(1);
                        Value v;
                        if(in.b >= 2) { depth = stack[--sp]; v = stack[--sp]; }
                        else { v = stack[--sp]; }
                        stack[sp++] = lumin_array_flat(v, lumin_extract_int(depth));
                        break;
                    }
                    case BUILTIN_QS: {
                        Value enc = val_none();
                        Value v;
                        if(in.b >= 2) { enc = stack[--sp]; v = stack[--sp]; }
                        else { v = stack[--sp]; }
                        if(v.type == VAL_MAP || v.type == VAL_ARRAY) {
                            char* q = lumin_qs_stringify_enc(v, enc);
                            stack[sp++] = lumin_make_string(q);
                            free(q);
                        } else if(v.type == VAL_STRING) {
                            stack[sp++] = lumin_qs_parse_enc(v.v.s, enc);
                        } else {
                            runtime_error("qs() 参数必须是字典/数组（序列化）或字符串（解析）");
                        }
                        break;
                    }
                    case BUILTIN_ARRAY_ADDALL: {
                        Value b = stack[--sp];
                        lumin_array_addall(&stack[sp-1], b);
                        break;
                    }
                    case BUILTIN_BYTES: {
                        Value enc = val_none();
                        Value v;
                        if(in.b >= 2) { enc = stack[--sp]; v = stack[--sp]; }
                        else { v = stack[--sp]; }
                        stack[sp++] = lumin_to_bytes(v, enc);
                        break;
                    }
                    case BUILTIN_STR: {
                        Value enc = val_none();
                        Value v;
                        if(in.b >= 2) { enc = stack[--sp]; v = stack[--sp]; }
                        else { v = stack[--sp]; }
                        stack[sp++] = lumin_from_bytes(v, enc);
                        break;
                    }
                    case BUILTIN_ENCODE: {
                        Value enc = val_none();
                        Value v;
                        if(in.b >= 2) { enc = stack[--sp]; v = stack[--sp]; }
                        else { v = stack[--sp]; }
                        stack[sp++] = lumin_to_bytes(v, enc);
                        break;
                    }
                    case BUILTIN_DECODE: {
                        Value enc = val_none();
                        Value v;
                        if(in.b >= 2) { enc = stack[--sp]; v = stack[--sp]; }
                        else { v = stack[--sp]; }
                        stack[sp++] = lumin_from_bytes(v, enc);
                        break;
                    }
                    case BUILTIN_ENCODE_URL: {
                        Value v = stack[--sp];
                        char* r = lumin_url_encode(v.type == VAL_STRING ? (v.v.s ? v.v.s : "") : "");
                        stack[sp++] = lumin_make_string(r);
                        free(r);
                        break;
                    }
                    case BUILTIN_DECODE_URL: {
                        Value v = stack[--sp];
                        char* r = lumin_url_decode(v.type == VAL_STRING ? (v.v.s ? v.v.s : "") : "");
                        stack[sp++] = lumin_make_string(r);
                        free(r);
                        break;
                    }
                    case BUILTIN_MD5: {
                        Value v = stack[--sp];
                        const char* inp = v.type == VAL_STRING ? (v.v.s ? v.v.s : "") : "";
                        char* r = lumin_md5_hex(inp, (int)strlen(inp));
                        stack[sp++] = lumin_make_string(r);
                        free(r);
                        break;
                    }
                    case BUILTIN_ENCODE_BASE64: {
                        Value v = stack[--sp];
                        const char* inp = v.type == VAL_STRING ? (v.v.s ? v.v.s : "") : "";
                        char* r = lumin_base64_encode(inp, (int)strlen(inp));
                        stack[sp++] = lumin_make_string(r);
                        free(r);
                        break;
                    }
                    case BUILTIN_DECODE_BASE64: {
                        Value v = stack[--sp];
                        int olen = 0;
                        char* r = lumin_base64_decode(v.type == VAL_STRING ? (v.v.s ? v.v.s : "") : "", &olen);
                        stack[sp++] = lumin_make_string(r);
                        free(r);
                        break;
                    }
                    case BUILTIN_REGEX_MATCH: {
                        Value pat = stack[--sp];
                        Value str = stack[--sp];
                        stack[sp++] = lumin_make_bool(lumin_regex_match(
                            str.type == VAL_STRING ? (str.v.s ? str.v.s : "") : "",
                            pat.type == VAL_STRING ? (pat.v.s ? pat.v.s : "") : ""));
                        break;
                    }
                    case BUILTIN_REGEX_SEARCH: {
                        Value pat = stack[--sp];
                        Value str = stack[--sp];
                        stack[sp++] = lumin_regex_search(
                            str.type == VAL_STRING ? (str.v.s ? str.v.s : "") : "",
                            pat.type == VAL_STRING ? (pat.v.s ? pat.v.s : "") : "");
                        break;
                    }
                    case BUILTIN_REGEX_REPLACE: {
                        Value repl = stack[--sp];
                        Value pat = stack[--sp];
                        Value str = stack[--sp];
                        char* r = lumin_regex_replace(
                            str.type == VAL_STRING ? (str.v.s ? str.v.s : "") : "",
                            pat.type == VAL_STRING ? (pat.v.s ? pat.v.s : "") : "",
                            repl.type == VAL_STRING ? (repl.v.s ? repl.v.s : "") : "");
                        stack[sp++] = lumin_make_string(r);
                        free(r);
                        break;
                    }
                    case BUILTIN_NOW: {
                        stack[sp++] = lumin_now();
                        break;
                    }
                    case BUILTIN_TIMESTAMP: {
                        stack[sp++] = lumin_make_double(lumin_timestamp());
                        break;
                    }
                    case BUILTIN_TIMESTAMP_MS: {
                        stack[sp++] = lumin_make_int(lumin_timestamp_ms());
                        break;
                    }
                    case BUILTIN_SLEEP: {
                        Value v = stack[--sp];
                        lumin_sleep_ms((long long)lumin_extract_int(v));
                        stack[sp++] = val_none();
                        break;
                    }
                    case BUILTIN_DATE: {
                        char* r = lumin_date_str();
                        stack[sp++] = lumin_make_string(r);
                        free(r);
                        break;
                    }
                    case BUILTIN_TIME: {
                        char* r = lumin_time_str();
                        stack[sp++] = lumin_make_string(r);
                        free(r);
                        break;
                    }
                    case BUILTIN_DATETIME: {
                        char* r = lumin_datetime_str();
                        stack[sp++] = lumin_make_string(r);
                        free(r);
                        break;
                    }
                    case BUILTIN_FORMAT_TIME: {
                        Value ts = val_none();
                        Value fmt;
                        if(in.b >= 2) { ts = stack[--sp]; fmt = stack[--sp]; }
                        else { fmt = stack[--sp]; }
                        double tsv = (ts.type == VAL_NONE) ? -1.0 : (ts.type == VAL_DOUBLE ? ts.v.d : (double)lumin_extract_int(ts));
                        char* r = lumin_format_time(fmt.type == VAL_STRING ? (fmt.v.s ? fmt.v.s : "") : "", tsv);
                        stack[sp++] = lumin_make_string(r);
                        free(r);
                        break;
                    }
                    case BUILTIN_LOG_DEBUG:
                    case BUILTIN_LOG_INFO:
                    case BUILTIN_LOG_WARN:
                    case BUILTIN_LOG_ERROR:
                    case BUILTIN_LOG_FATAL: {
                        int lvl = in.a - BUILTIN_LOG_DEBUG;
                        Value msg;
                        if(in.b >= 2) { msg = stack[--sp]; stack[--sp]; }  // 方法链：先弹消息，再丢弃 receiver
                        else { msg = stack[--sp]; }
                        char* ms = value_to_str(msg);
                        lumin_log(lvl, ms);
                        free(ms);
                        stack[sp++] = val_none();
                        break;
                    }
                    case BUILTIN_HTTP_DELETE: m = "DELETE"; break;
                            case BUILTIN_HTTP_HEAD:   m = "HEAD"; break;
                            case BUILTIN_HTTP_PATCH:  m = "PATCH"; break;
                            default: break;
                        }
                        Value url    = stack[sp - n];
                        Value params = (n >= 2) ? stack[sp - n + 1] : val_none();
                        Value config = (n >= 3) ? stack[sp - n + 2] : val_none();
                        stack[sp - n] = lumin_http_request(m, url, params, config);
                        sp = sp - n + 1;
                        break;
                    }
                    case BUILTIN_MAP:
                    case BUILTIN_FILTER:
                    case BUILTIN_REDUCE: {
                        int argc = in.b;
                        Value fn, arr, init = val_none();
                        if(argc == 3) { init = stack[--sp]; fn = stack[--sp]; arr = stack[--sp]; }
                        else { fn = stack[--sp]; arr = stack[--sp]; }
                        if(arr.type == VAL_MAP && in.a == BUILTIN_MAP) {
                            /* 字典 map：fn(value, key) → 新字典（键不变值映射） */
                            if(fn.type != VAL_FUNC) runtime_error("map() 第二个参数必须是函数");
                            RuntimeFunc* mrf = fn.v.func.func_obj;
                            Value mout = val_map();
                            MapIter it; map_iter_init(&it, arr.v.map);
                            Value mk, mv;
                            while(map_iter_next(&it, &mk, &mv)) {
                                Value a2[2];
                                a2[0] = mv;
                                a2[1] = mk;
                                Value r = vm_call_rf(mrf, a2, 2, frame, ctx);
                                lumin_map_set(&mout, mk, r);
                            }
                            stack[sp++] = mout;
                            break;
                        }
                        if(arr.type != VAL_ARRAY) runtime_error("map()/filter()/reduce() 第一个参数必须是数组");
                        if(fn.type != VAL_FUNC) runtime_error("map()/filter()/reduce() 第二个参数必须是函数");
                        RuntimeFunc* rf = fn.v.func.func_obj;
                        int n = arr.v.array->len;
                        if(in.a == BUILTIN_MAP) {
                            Value out = val_array(n);
                            for(int i = 0; i < n; i++) {
                                Value a1[1] = { arr.v.array->items[i] };
                                Value r = vm_call_rf(rf, a1, 1, frame, ctx);
                                out.v.array->items[i] = r;
                            }
                            stack[sp++] = out;
                        } else if(in.a == BUILTIN_FILTER) {
                            Value out = val_array(n);
                            int cnt = 0;
                            for(int i = 0; i < n; i++) {
                                Value a1[1] = { arr.v.array->items[i] };
                                Value r = vm_call_rf(rf, a1, 1, frame, ctx);
                                if(lumin_to_bool(r)) out.v.array->items[cnt++] = arr.v.array->items[i];
                            }
                            out.v.array->len = cnt;
                            stack[sp++] = out;
                        } else {
                            Value acc = init;
                            for(int i = 0; i < n; i++) {
                                Value a2[2] = { acc, arr.v.array->items[i] };
                                acc = vm_call_rf(rf, a2, 2, frame, ctx);
                            }
                            stack[sp++] = acc;
                        }
                        break;
                    }
                    default:
                        runtime_error("未知内置函数");
                        break;
                }
                (void)argc;
                break;
            }
            case OPC_PRINT:
                lumin_print(stack[sp - 1]);
                break;
            case OPC_TO_BOOL:
                stack[sp - 1] = lumin_make_bool(lumin_to_bool(stack[sp - 1]));
                break;
            case OPC_DUP:
                stack[sp] = stack[sp - 1];
                sp++;
                break;
            case OPC_POP:
                sp--;
                break;
            case OPC_TRY: {
                /* longjmp 后自动变量值未定义：catch 目标按层保存 */
                vm_ensure(vm_depth + 2);
                int d = vm_depth;
                vm_target[d] = in.a;
                vm_sp[d] = sp;
                vm_prev[d] = g_err_jmp;
                vm_tn[d] = g_trace_n;
                vm_fn[d] = vm_fin_n;
                g_err_jmp = &vm_jbs[d];
                if(setjmp(vm_jbs[d]) == 0) {
                    vm_depth = d + 1;
                } else {
                    /* longjmp 后局部变量值未定义：本层索引从 static vm_depth 反推 */
                    int d2 = vm_depth - 1;
                    sp = vm_sp[d2];
                    vm_depth = d2;
                    g_err_jmp = vm_prev[d2];
                    /* trace/fin 栈不在此截断：GET_ERR 用完整残留生成回溯后再截断 */
                    pc = vm_target[d2];
                }
                break;
            }
            case OPC_ENDTRY:
                if(vm_depth > 0) { vm_depth--; g_err_jmp = vm_prev[vm_depth]; }
                break;
            case OPC_GET_ERR: {
                /* 错误对象化：type/message + 调用栈回溯；然后截断残留到 TRY 层 */
                char* st = lumin_build_stack_trace();
                stack[sp++] = lumin_make_error(g_err_type, g_err_msg, st);
                free(st);
                /* TRY 存 vm_tn[d]/vm_fn[d]（d=TRY 层深度），GET_ERR 时 vm_depth=d → 直接用 vm_depth */
                g_trace_n = vm_tn[vm_depth];
                vm_fin_n = vm_fn[vm_depth];
                break;
            }
            case OPC_THROW: {
                /* 弹1：包装成错误对象抛出（字符串→type Error；错误对象→原样；map→type/message 字段） */
                Value v = stack[--sp];
                const char* type = "Error";
                char* msg = NULL;
                if(v.type == VAL_ERROR) {
                    type = v.v.err.type ? v.v.err.type : "Error";
                    msg = strdup(v.v.err.message ? v.v.err.message : "");
                } else if(v.type == VAL_MAP) {
                    if(lumin_map_has(v, lumin_make_string("type"))) {
                        Value tv = lumin_map_get(v, lumin_make_string("type"));
                        if(tv.type == VAL_STRING) type = tv.v.s;
                    }
                    if(lumin_map_has(v, lumin_make_string("message"))) {
                        Value mv = lumin_map_get(v, lumin_make_string("message"));
                        if(mv.type == VAL_STRING) msg = strdup(mv.v.s);
                    }
                }
                if(!msg) msg = value_to_str(v);
                g_err_type_set(type);
                g_err_msg_set(msg);
                free(msg);
                if(g_err_jmp) longjmp(*g_err_jmp, 1);
                fprintf(stderr, "Runtime Error: %s\n", g_err_msg);
                exit(EXIT_FAILURE);
            }
            case OPC_FIN_PUSH:
                vm_ensure(vm_fin_n + 2);
                vm_fin_act[vm_fin_n] = in.a;
                vm_fin_tgt[vm_fin_n] = in.b;
                vm_fin_dep[vm_fin_n] = vm_depth - 1;   /* try 前深度（异常入口 act=2 不使用） */
                vm_fin_n++;
                break;
            case OPC_FINISH: {
                if(vm_fin_n <= 0) runtime_error("finally 完成栈为空");
                int act = vm_fin_act[--vm_fin_n];
                if(act == 1 || act == 3 || act == 4) {
                    vm_depth = vm_fin_dep[vm_fin_n];   /* 退出 try 保护区，恢复层深度 */
                    g_err_jmp = vm_prev[vm_fin_dep[vm_fin_n]];  /* 还原 TRY 前 handler */
                    pc = vm_fin_tgt[vm_fin_n];
                } else if(act == 2) {
                    /* RETHROW：错误消息/类型仍在 g_err_msg/g_err_type，向上一层冒泡 */
                    if(g_err_jmp) longjmp(*g_err_jmp, 1);
                    fprintf(stderr, "Runtime Error: %s\n", g_err_msg);
                    exit(EXIT_FAILURE);
                } else if(act == 5) {
                    /* RETURN：恢复函数返回 */
                    Value v = vm_pend_val;
                    vm_depth = saved_depth;
                    g_err_jmp = saved_gj;
                    gc_set_roots(NULL, NULL, NULL);
                    free(stack);
                    return v;
                } else {
                    runtime_error("finally 完成动作未知");
                }
                break;
            }
            case OPC_PEND_RETURN: {
                /* 弹1（返回值）→ 压 RETURN 动作 → 跳 finally（b=0 则直接返回） */
                vm_pend_val = stack[--sp];
                vm_fin_act[vm_fin_n] = 5;
                vm_fin_tgt[vm_fin_n] = 0;
                vm_fin_n++;
                if(in.b) pc = in.b;
                break;
            }
            case OPC_JMP:
                pc = in.a;
                break;
            case OPC_JMP_IF_FALSE:
                if(!lumin_to_bool(stack[--sp])) pc = in.a;
                break;
            case OPC_JMP_IF_TRUE:
                if(lumin_to_bool(stack[--sp])) pc = in.a;
                break;
            case OPC_CALL: {
                const char* fname = bf->syms[in.a];
                int argc = in.b;
                // 1. 查函数：帧链 VAL_FUNC → 全局函数表
                Value func_val;
                _Bool fnd = 0;
                Value gv = stackframe_get(frame, fname, &fnd);
                if(fnd && gv.type == VAL_FUNC) func_val = gv;
                else if(sym_has(fname)) func_val = sym_get(fname);
                else runtime_undefined("函数", fname);
                if(func_val.type != VAL_FUNC) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "尝试调用非函数: %s", fname);
                    runtime_error(buf);
                }
                RuntimeFunc* rf = func_val.v.func.func_obj;
                // 2. 实参：栈顶 argc 个（指针指向 VM 栈，entry 内绑定完成前有效）
                Value* eval_args = (sp > 0) ? &stack[sp - argc] : NULL;
                // 3. 新帧 + 参数绑定（与 interp AST_CALL 一致）
                StackFrame* callee = stackframe_new(frame);
                if(interp_func_is_payload(rf)) {
                    int pcnt = interp_func_param_cnt(rf);
                    int i = 0;
                    for(; i < pcnt; i++) {
                        const char* pname = interp_func_param_name(rf, i);
                        Value bound = (i < argc) ? eval_args[i] : val_none();
                        stackframe_bind(callee, pname, bound);
                    }
                    if(interp_func_has_variadic(rf)) {
                        const char* vname = interp_func_param_name(rf, pcnt);
                        int rest = argc - i;
                        if(rest < 0) rest = 0;
                        Value arr = val_array(rest);
                        for(int k = 0; k < rest; k++) {
                            arr.v.array->items[k] = eval_args[i + k];
                        }
                        stackframe_bind(callee, vname, arr);
                    }
                }
                // 4. 调用 entry：设置当前函数、隔离 break/continue、消费 return
                RuntimeFunc* prev_rf = interp_set_current_rf(rf);
                int saved_break = ctx->hit_break;
                int saved_cont = ctx->hit_continue;
                ctx->hit_break = 0;
                ctx->hit_continue = 0;
                /* 调用栈回溯：入栈函数名（longjmp 跳过 pop 由 GET_ERR 截断） */
                g_trace_push(fname);
                Value ret = rf->entry(argc, eval_args, ctx, callee);
                if(g_trace_n > 0) g_trace_n--;
                ctx->hit_break = saved_break;
                ctx->hit_continue = saved_cont;
                interp_set_current_rf(prev_rf);
                stackframe_destroy(callee);
                // 5. 弹出实参（不销毁，与 interp 口径一致），压入返回值
                sp -= argc;
                stack[sp++] = ret;
                break;
            }
            case OPC_CALLV: {
                // 动态调用链 f(1)(2)：栈顶 b 个为实参，其下一位是函数值
                int argc = in.b;
                Value func_val = (sp - argc - 1 >= 0) ? stack[sp - argc - 1] : val_none();
                if(func_val.type != VAL_FUNC) runtime_error("尝试调用非函数值");
                RuntimeFunc* rf = func_val.v.func.func_obj;
                Value* eval_args = (sp > 0) ? &stack[sp - argc] : NULL;
                StackFrame* callee = stackframe_new(frame);
                if(interp_func_is_payload(rf)) {
                    int pcnt = interp_func_param_cnt(rf);
                    int i = 0;
                    for(; i < pcnt; i++) {
                        const char* pname = interp_func_param_name(rf, i);
                        Value bound = (i < argc) ? eval_args[i] : val_none();
                        stackframe_bind(callee, pname, bound);
                    }
                    if(interp_func_has_variadic(rf)) {
                        const char* vname = interp_func_param_name(rf, pcnt);
                        int rest = argc - i;
                        if(rest < 0) rest = 0;
                        Value arr = val_array(rest);
                        for(int k = 0; k < rest; k++) {
                            arr.v.array->items[k] = eval_args[i + k];
                        }
                        stackframe_bind(callee, vname, arr);
                    }
                }
                RuntimeFunc* prev_rf = interp_set_current_rf(rf);
                int saved_break = ctx->hit_break;
                int saved_cont = ctx->hit_continue;
                ctx->hit_break = 0;
                ctx->hit_continue = 0;
                Value ret = rf->entry(argc, eval_args, ctx, callee);
                ctx->hit_break = saved_break;
                ctx->hit_continue = saved_cont;
                interp_set_current_rf(prev_rf);
                stackframe_destroy(callee);
                sp -= argc + 1;   // 弹实参 + 函数值
                stack[sp++] = ret;
                break;
            }
            case OPC_RETURN: {
                Value v = stack[--sp];
                vm_depth = saved_depth;
                g_err_jmp = saved_gj;
                vm_fin_n = saved_fin;
                gc_set_roots(NULL, NULL, NULL);
                free(stack);
                return v;
            }
            case OPC_RETURN_NIL:
                vm_depth = saved_depth;
                g_err_jmp = saved_gj;
                vm_fin_n = saved_fin;
                gc_set_roots(NULL, NULL, NULL);
                free(stack);
                return val_none();
            case OPC_HALT:
                vm_depth = saved_depth;
                g_err_jmp = saved_gj;
                vm_fin_n = saved_fin;
                gc_set_roots(NULL, NULL, NULL);
                free(stack);
                return val_none();
            default:
                runtime_error("vm: 未知指令");
        }
    }
}
