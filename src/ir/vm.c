// 字节码 VM 执行器
// 指令语义与 ast_interp.c 对齐（栈帧链变量、调用绑定、return 深拷贝、break/continue 编译期跳转）。
#include "vm.h"
#include "ast/stackframe.h"
#include "ast/func_compile.h"
#include "ast/ast_runtime_sym.h"
#include "runtime/lm_value.h"
#include "runtime/lm_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

/* try/catch 错误处理器栈（VM 侧；C 生成侧用局部 jmp_buf） */
static jmp_buf vm_jbs[64];
static jmp_buf* vm_prev[64];
static int vm_depth = 0;
static int vm_sp[64];
static int vm_target[64];   /* 每层的 catch 目标（longjmp 后自动变量不可靠） */
static int vm_tn[64];       /* 每层 TRY 时的调用栈深度（GET_ERR 截断残留） */
static int vm_fn[64];       /* 每层 TRY 时的 finally 完成栈深度 */
static int vm_fin_act[64];  /* finally 完成动作：1=JMP 2=RETHROW 3=BREAK 4=CONT 5=RETURN */
static int vm_fin_tgt[64];
static int vm_fin_dep[64];  /* FIN_PUSH 时的恢复深度（FINISH act=1/3/4 恢复，防循环内 depth 漂移） */
static int vm_fin_n = 0;
static Value vm_pend_val;   /* 挂起返回的值（PEND_RETURN 存，FINISH act5 恢复） */

#define VM_STACK_MAX 256

// 未定义变量/函数：统一报错退出（与 ast_interp.c 输出一致）
static void runtime_undefined(const char* what, const char* name)
{
    fprintf(stderr, "Runtime Error: 未定义%s: %s\n", what, name);
    exit(EXIT_FAILURE);
}

static Value vm_run(BytecodeFunc* bf, StackFrame* frame, EvalCtx* ctx);


// 通过函数值调用（高阶函数内部使用）：与 OPC_CALL 的调用语义一致
static Value vm_call_rf(RuntimeFunc* rf, Value* args, int argc, StackFrame* parent, EvalCtx* ctx)
{
    StackFrame* callee = stackframe_new(parent);
    if(interp_func_is_payload(rf)) {
        int pcnt = interp_func_param_cnt(rf);
        int i = 0;
        for(; i < pcnt; i++) {
            const char* pname = interp_func_param_name(rf, i);
            Value bound = (i < argc) ? val_clone(&args[i]) : val_none();
            stackframe_bind(callee, pname, bound);
        }
        if(interp_func_has_variadic(rf)) {
            const char* vname = interp_func_param_name(rf, pcnt);
            int rest = argc - i;
            if(rest < 0) rest = 0;
            Value arr = val_array(rest);
            for(int k = 0; k < rest; k++) {
                arr.v.array.items[k] = val_clone(&args[i + k]);
            }
            stackframe_bind(callee, vname, arr);
        }
    }
    RuntimeFunc* prev_rf = interp_set_current_rf(rf);
    int saved_break = ctx->hit_break;
    int saved_cont = ctx->hit_continue;
    ctx->hit_break = 0;
    ctx->hit_continue = 0;
    if(g_trace_n < 64) g_trace[g_trace_n++] = "<anonymous>";
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
    Value ret = vm_run(main_fn, top, &local_ctx);
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
    // 静态栈深度分析：精确分配执行栈，并校验 IR 栈平衡
    int maxd = bc_analyze_stack(bf, NULL, 0);
    if(maxd < 0) exit(EXIT_FAILURE);   // 已打印下溢位置
    if(maxd + 2 > VM_STACK_MAX) {
        char buf[128];
        snprintf(buf, sizeof(buf), "vm: 函数 %s 需要栈深 %d 超过上限 %d",
                 bf->name ? bf->name : "<main>", maxd, VM_STACK_MAX);
        runtime_error(buf);
    }
    Value* stack = (Value*)malloc(sizeof(Value) * (maxd + 2));
    if(!stack) { perror("vm_run"); exit(EXIT_FAILURE); }
    int sp = 0;
    int pc = 0;
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
                stack[sp++] = val_clone(&bf->consts[in.a]);
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
                Value* vp = stackframe_get(frame, name);
                if(!vp) runtime_undefined("变量", name);
                stack[sp++] = *vp;
                break;
            }
            case OPC_STORE_VAR: {
                const char* name = bf->syms[in.a];
                Value v = stack[--sp];
                stackframe_set(frame, name, val_clone(&v));
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
            case OPC_PRE_INC:  { const char* n = bf->syms[in.a]; Value* vp = stackframe_get(frame, n);
                                 if(!vp) runtime_undefined("变量", n);
                                 stack[sp++] = lumin_pre_inc(vp); break; }
            case OPC_POST_INC: { const char* n = bf->syms[in.a]; Value* vp = stackframe_get(frame, n);
                                 if(!vp) runtime_undefined("变量", n);
                                 stack[sp++] = lumin_post_inc(vp); break; }
            case OPC_PRE_DEC:  { const char* n = bf->syms[in.a]; Value* vp = stackframe_get(frame, n);
                                 if(!vp) runtime_undefined("变量", n);
                                 stack[sp++] = lumin_pre_dec(vp); break; }
            case OPC_POST_DEC: { const char* n = bf->syms[in.a]; Value* vp = stackframe_get(frame, n);
                                 if(!vp) runtime_undefined("变量", n);
                                 stack[sp++] = lumin_post_dec(vp); break; }
            case OPC_CAST_INT:    { Value v = stack[--sp]; stack[sp++] = lumin_cast_int(v); break; }
            case OPC_CAST_DOUBLE: { Value v = stack[--sp]; stack[sp++] = lumin_cast_double(v); break; }
            case OPC_CAST_CHAR:   { Value v = stack[--sp]; stack[sp++] = lumin_cast_char(v); break; }
            case OPC_CAST_BOOL:   { Value v = stack[--sp]; stack[sp++] = lumin_cast_bool(v); break; }
            case OPC_CAST_STRING: { Value v = stack[--sp]; stack[sp++] = lumin_cast_string(v); break; }
            case OPC_CAST_ASCII:  { Value v = stack[--sp]; stack[sp++] = lumin_cast_ascii(v); break; }
            case OPC_LOGIC_NOT:   { Value v = stack[--sp]; stack[sp++] = lumin_logic_not(v); break; }
            case OPC_ARRAY_LIT: {
                int n = in.b;
                Value arr = val_array(n);
                for(int k = 0; k < n; k++)
                    arr.v.array.items[k] = val_clone(&stack[sp - n + k]);
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
                        Value arr = stack[--sp];
                        stack[sp++] = lumin_del(arr, idx);
                        break;
                    }
                    case BUILTIN_INSERT: {
                        Value val = stack[--sp];
                        Value idx = stack[--sp];
                        Value arr = stack[--sp];
                        stack[sp++] = lumin_insert(arr, idx, val);
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
                    case BUILTIN_MAP:
                    case BUILTIN_FILTER:
                    case BUILTIN_REDUCE: {
                        int argc = in.b;
                        Value fn, arr, init = val_none();
                        if(argc == 3) { init = stack[--sp]; fn = stack[--sp]; arr = stack[--sp]; }
                        else { fn = stack[--sp]; arr = stack[--sp]; }
                        if(arr.type != VAL_ARRAY) runtime_error("map()/filter()/reduce() 第一个参数必须是数组");
                        if(fn.type != VAL_FUNC) runtime_error("map()/filter()/reduce() 第二个参数必须是函数");
                        RuntimeFunc* rf = fn.v.func.func_obj;
                        int n = arr.v.array.len;
                        if(in.a == BUILTIN_MAP) {
                            Value out = val_array(n);
                            for(int i = 0; i < n; i++) {
                                Value a1[1] = { arr.v.array.items[i] };
                                Value r = vm_call_rf(rf, a1, 1, frame, ctx);
                                out.v.array.items[i] = val_clone(&r);
                            }
                            stack[sp++] = out;
                        } else if(in.a == BUILTIN_FILTER) {
                            Value out = val_array(n);
                            int cnt = 0;
                            for(int i = 0; i < n; i++) {
                                Value a1[1] = { arr.v.array.items[i] };
                                Value r = vm_call_rf(rf, a1, 1, frame, ctx);
                                if(lumin_to_bool(r)) out.v.array.items[cnt++] = val_clone(&arr.v.array.items[i]);
                            }
                            out.v.array.len = cnt;
                            stack[sp++] = out;
                        } else {
                            Value acc = init;
                            for(int i = 0; i < n; i++) {
                                Value a2[2] = { acc, arr.v.array.items[i] };
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
                /* longjmp 后自动变量值未定义：catch 目标按层 static 保存 */
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
                if(vm_depth > 0) {
                    g_trace_n = vm_tn[vm_depth - 1];
                    vm_fin_n = vm_fn[vm_depth - 1];
                }
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
                    if(lumin_map_has(v, "type")) {
                        Value tv = lumin_map_get(v, lumin_make_string("type"));
                        if(tv.type == VAL_STRING) type = tv.v.s;
                    }
                    if(lumin_map_has(v, "message")) {
                        Value mv = lumin_map_get(v, lumin_make_string("message"));
                        if(mv.type == VAL_STRING) msg = strdup(mv.v.s);
                    }
                }
                if(!msg) msg = value_to_str(v);
                snprintf(g_err_type, sizeof(g_err_type), "%s", type);
                snprintf(g_err_msg, sizeof(g_err_msg), "%s", msg);
                free(msg);
                if(g_err_jmp) longjmp(*g_err_jmp, 1);
                fprintf(stderr, "Runtime Error: %s\n", g_err_msg);
                exit(EXIT_FAILURE);
            }
            case OPC_FIN_PUSH:
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
                    free(stack);
                    return val_clone(&v);
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
                Value* fv = stackframe_get(frame, fname);
                if(fv && fv->type == VAL_FUNC) func_val = *fv;
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
                        Value bound = (i < argc) ? val_clone(&eval_args[i]) : val_none();
                        stackframe_bind(callee, pname, bound);
                    }
                    if(interp_func_has_variadic(rf)) {
                        const char* vname = interp_func_param_name(rf, pcnt);
                        int rest = argc - i;
                        if(rest < 0) rest = 0;
                        Value arr = val_array(rest);
                        for(int k = 0; k < rest; k++) {
                            arr.v.array.items[k] = val_clone(&eval_args[i + k]);
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
                if(g_trace_n < 64) g_trace[g_trace_n++] = fname;
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
                        Value bound = (i < argc) ? val_clone(&eval_args[i]) : val_none();
                        stackframe_bind(callee, pname, bound);
                    }
                    if(interp_func_has_variadic(rf)) {
                        const char* vname = interp_func_param_name(rf, pcnt);
                        int rest = argc - i;
                        if(rest < 0) rest = 0;
                        Value arr = val_array(rest);
                        for(int k = 0; k < rest; k++) {
                            arr.v.array.items[k] = val_clone(&eval_args[i + k]);
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
                free(stack);
                return val_clone(&v);
            }
            case OPC_RETURN_NIL:
                vm_depth = saved_depth;
                g_err_jmp = saved_gj;
                vm_fin_n = saved_fin;
                free(stack);
                return val_none();
            case OPC_HALT:
                vm_depth = saved_depth;
                g_err_jmp = saved_gj;
                vm_fin_n = saved_fin;
                free(stack);
                return val_none();
            default:
                runtime_error("vm: 未知指令");
        }
    }
}
