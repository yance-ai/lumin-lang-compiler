#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include "lm_runtime.h"
#include "lm_map.h"
#include "lm_thread.h"
#include "lm_lock.h"
#include "lm_tls.h"
#include "lm_http.h"
#include "lm_json.h"
#include "lm_charset.h"
#include "lm_crypto.h"
#include "lm_regex.h"
#include "lm_time.h"
#include "lm_qs.h"
#include "lumyr_value.h"

static Value __g_gen_send_val = {0};
static int __g_gen_in_generator = 0;


__attribute__((weak)) void lumyr_interp_scan_captures(const RuntimeFunc* rf, void (*mark)(Value)) { (void)rf; (void)mark; }

/* ===== 生成器组合操作运行时支持 ===== */
typedef enum { WRAP_NONE=0, WRAP_MAP=1, WRAP_FILTER=2, WRAP_SKIP=3, WRAP_TAKE=4, WRAP_ENUMERATE=5, WRAP_CHAIN=6, WRAP_ZIP=7 } WrapType;
typedef struct lumyr_gen_wrap {
    Value (*next)(void*, Value);
    int __state;
    int is_wrapped;
    int wrap_type;
    void* wrapped_gen;
    void* wrapped_gen2;
    Value wrap_fn;
    int wrap_arg;
    int wrap_index;
} lumyr_gen_wrap;

static Value lumyr_gen_wrap_next(void* __gptr, Value __send_val) {
    lumyr_gen_wrap* g = (lumyr_gen_wrap*)__gptr;
    if(g->__state == -1) return val_none();
    Value (*wnext)(void*, Value) = *(Value(**)(void*,Value))g->wrapped_gen;
    switch(g->wrap_type) {
        case WRAP_MAP: {
            Value v = wnext(g->wrapped_gen, val_none());
            if(v.type == VAL_NONE) { g->__state = -1; return val_none(); }
            Value args[1]; args[0] = v;
            Value (*fn)(Value*,int) = (Value(*)(Value*,int))((RuntimeFunc*)g->wrap_fn.v.func.func_obj)->entry;
            return fn(args, 1);
        }
        case WRAP_FILTER: {
            while(1) {
                Value v = wnext(g->wrapped_gen, val_none());
                if(v.type == VAL_NONE) { g->__state = -1; return val_none(); }
                Value args[1]; args[0] = v;
                Value (*fn)(Value*,int) = (Value(*)(Value*,int))((RuntimeFunc*)g->wrap_fn.v.func.func_obj)->entry;
                Value r = fn(args, 1);
                if(lumyr_to_bool(r)) return v;
            }
        }
        case WRAP_SKIP: {
            while(g->wrap_index < g->wrap_arg) {
                Value v = wnext(g->wrapped_gen, val_none());
                if(v.type == VAL_NONE) { g->__state = -1; return val_none(); }
                g->wrap_index++;
            }
            Value v = wnext(g->wrapped_gen, val_none());
            if(v.type == VAL_NONE) { g->__state = -1; return val_none(); }
            return v;
        }
        case WRAP_TAKE: {
            if(g->wrap_index >= g->wrap_arg) { g->__state = -1; return val_none(); }
            Value v = wnext(g->wrapped_gen, val_none());
            if(v.type == VAL_NONE) { g->__state = -1; return val_none(); }
            g->wrap_index++;
            return v;
        }
        case WRAP_ENUMERATE: {
            Value v = wnext(g->wrapped_gen, val_none());
            if(v.type == VAL_NONE) { g->__state = -1; return val_none(); }
            Value arr = val_array(2);
            arr.v.array->items[0] = lumyr_make_int(g->wrap_index);
            arr.v.array->items[1] = v;
            g->wrap_index++;
            return arr;
        }
        case WRAP_CHAIN: {
            if(g->wrap_index == 0) {
                Value v = wnext(g->wrapped_gen, val_none());
                if(v.type != VAL_NONE) return v;
                g->wrap_index = 1;
            }
            Value (*wnext2)(void*,Value) = *(Value(**)(void*,Value))g->wrapped_gen2;
            Value v = wnext2(g->wrapped_gen2, val_none());
            if(v.type == VAL_NONE) { g->__state = -1; return val_none(); }
            return v;
        }
        case WRAP_ZIP: {
            Value (*wnext2)(void*,Value) = *(Value(**)(void*,Value))g->wrapped_gen2;
            Value v1 = wnext(g->wrapped_gen, val_none());
            Value v2 = wnext2(g->wrapped_gen2, val_none());
            if(v1.type == VAL_NONE || v2.type == VAL_NONE) { g->__state = -1; return val_none(); }
            Value arr = val_array(2);
            arr.v.array->items[0] = v1;
            arr.v.array->items[1] = v2;
            return arr;
        }
        default: g->__state = -1; return val_none();
    }
}

static Value lumyr_wrap_create(int wtype, Value g1, Value g2, Value fn, int arg) {
    if(g1.type != VAL_GENERATOR) runtime_error("组合操作第一个参数必须是生成器");
    lumyr_gen_wrap* wg = (lumyr_gen_wrap*)calloc(1, sizeof(lumyr_gen_wrap));
    wg->next = lumyr_gen_wrap_next;
    wg->is_wrapped = 1;
    wg->wrap_type = wtype;
    wg->wrapped_gen = g1.v.generator;
    if(g2.type == VAL_GENERATOR) wg->wrapped_gen2 = g2.v.generator;
    wg->wrap_fn = fn;
    wg->wrap_arg = arg;
    wg->wrap_index = 0;
    Value gv; gv.type = VAL_GENERATOR; gv.v.generator = (void*)wg;
    return gv;
}

/* ===== 生成器组合操作支持结束 ===== */

typedef struct {
    int x;
    int y;
} lumyr_struct_Point;

static lumyr_struct_Point lmvar_p1__s;
static Value lmvar_p1;
static lumyr_struct_Point lmvar_p2__s;
static Value lmvar_p2;

static Value lumyr_func_modify_by_value(Value);
static Value lumyr_func_modify_by_ref(Value*);
static Value lum_wrap_0(Value*, int, void*);
static Value lum_wrap_1(Value*, int, void*);
static RuntimeFunc lum_wrap_0_rf;
static RuntimeFunc lum_wrap_1_rf;

static Value lumyr_func_modify_by_value(Value lmloc_p)
{
    Value __stk[4];
    int __sp = 0;
    int __g_d0 = __g_depth; jmp_buf* __g_gj0 = g_err_jmp; int __g_fin0 = __g_fin_n;
    g_trace_push("modify_by_value");
    volatile Value* __local_ptrs[1] = { &lmloc_p };
    CFrame __frame;
    __frame.stack = __stk;
    __frame.sp = &__sp;
    __frame.stack_size = 4;
    __frame.local_ptrs = (Value**)__local_ptrs;
    __frame.nlocals = 1;
    gc_push_cframe(&__frame);
    gc_stw_check_fast();
    __stk[__sp++] = lumyr_make_int(999);
    { Value __v = __stk[--__sp]; Value __c = lmloc_p; lumyr_array_set(__c, lumyr_make_string("x"), __v); __stk[__sp++] = __v; }
    __sp--;
    __stk[__sp++] = lumyr_make_string("inside value: p.x =");
    { Value __c = lmloc_p; __stk[__sp++] = lumyr_index_get(__c, lumyr_make_string("x")); }
    { int __pcnt = 2; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0;
    if(g_trace_n > 0) g_trace_n--;
    gc_pop_cframe();
    return val_none();
}

static Value lumyr_func_modify_by_ref(Value* lmloc_p)
{
    Value __stk[4];
    int __sp = 0;
    int __g_d0 = __g_depth; jmp_buf* __g_gj0 = g_err_jmp; int __g_fin0 = __g_fin_n;
    g_trace_push("modify_by_ref");
    volatile Value* __local_ptrs[1] = { lmloc_p };
    CFrame __frame;
    __frame.stack = __stk;
    __frame.sp = &__sp;
    __frame.stack_size = 4;
    __frame.local_ptrs = (Value**)__local_ptrs;
    __frame.nlocals = 1;
    gc_push_cframe(&__frame);
    gc_stw_check_fast();
    __stk[__sp++] = lumyr_make_int(888);
    { Value __v = __stk[--__sp];
        ((lumyr_struct_Point*)(*lmloc_p).v.struct_ptr)->x = (int)__v.v.i;
        __stk[__sp++] = __v;
    }
    __sp--;
    __stk[__sp++] = lumyr_make_string("inside ref: p.x =");
    __stk[__sp++] = lumyr_make_int((long long)((lumyr_struct_Point*)(*lmloc_p).v.struct_ptr)->x);
    { int __pcnt = 2; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0;
    if(g_trace_n > 0) g_trace_n--;
    gc_pop_cframe();
    return val_none();
}

static Value lum_wrap_0(Value* a, int n, void* __ctx)
{
    Value p0 = (n > 0) ? a[0] : val_none();
    Value __wrap_ret = lumyr_func_modify_by_value(p0);
    return __wrap_ret;
}

static RuntimeFunc lum_wrap_0_rf = { (FuncEntry*)lum_wrap_0, 1, 0, NULL, 0 };

static Value lum_wrap_1(Value* a, int n, void* __ctx)
{
    Value p0 = (n > 0) ? a[0] : val_none();
    Value __wrap_ret = lumyr_func_modify_by_ref(&p0);
    if(n > 0) a[0] = p0;
    return __wrap_ret;
}

static RuntimeFunc lum_wrap_1_rf = { (FuncEntry*)lum_wrap_1, 1, 0, NULL, 0 };

static Value (*const lumyr_cfunc_tbl[])(Value*, int, void*) = {
    lum_wrap_0,
    lum_wrap_1,
};

int main(void){
    Value __stk[10];
    int __sp = 0;
    lmvar_p1 = lumyr_make_struct_ptr(&lmvar_p1__s);
    lmvar_p2 = lumyr_make_struct_ptr(&lmvar_p2__s);
    int __g_d0 = __g_depth; jmp_buf* __g_gj0 = g_err_jmp; int __g_fin0 = __g_fin_n;
    volatile Value* __local_ptrs[1] = { NULL };
    CFrame __frame;
    __frame.stack = __stk;
    __frame.sp = &__sp;
    __frame.stack_size = 10;
    __frame.local_ptrs = (Value**)__local_ptrs;
    __frame.nlocals = 0;
    gc_push_cframe(&__frame);
    gc_stw_check_fast();
    __stk[__sp++] = val_none();
    __sp--;
    __stk[__sp++] = lumyr_make_string("=== Ref Parameter Test ===");
    { int __pcnt = 1; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    __stk[__sp++] = lumyr_make_string("--- 值传递（默认）---");
    { int __pcnt = 1; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    __stk[__sp++] = lumyr_make_string("__mapname__");
    gc_stw_check_fast();
    __stk[__sp++] = lumyr_make_string("Point");
    __stk[__sp++] = lumyr_make_string("__structname__");
    __stk[__sp++] = lumyr_make_string("Point");
    __stk[__sp++] = lumyr_make_string("x");
    __stk[__sp++] = lumyr_make_int(10);
    __stk[__sp++] = lumyr_make_string("y");
    __stk[__sp++] = lumyr_make_int(20);
    {
        Value __m = lumyr_map_lit(&__stk[__sp - 8], 4);
        __sp = __sp - 8 + 1;
        __stk[__sp - 1] = __m;
    }
    gc_stw_check_fast();
    { Value __v = __stk[--__sp];
        ((lumyr_struct_Point*)lmvar_p1.v.struct_ptr)->x = (int)lumyr_map_get(__v, lumyr_make_string("x")).v.i;
        ((lumyr_struct_Point*)lmvar_p1.v.struct_ptr)->y = (int)lumyr_map_get(__v, lumyr_make_string("y")).v.i;
        __stk[__sp++] = __v;
    }
    __sp--;
    __stk[__sp++] = lumyr_make_string("before: p1.x =");
    __stk[__sp++] = lumyr_make_int((long long)((lumyr_struct_Point*)lmvar_p1.v.struct_ptr)->x);
    { int __pcnt = 2; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    {
        Value __kv[8];
        __kv[0] = lumyr_make_string("x");
        __kv[1] = lumyr_make_int((long long)((lumyr_struct_Point*)lmvar_p1.v.struct_ptr)->x);
        __kv[2] = lumyr_make_string("y");
        __kv[3] = lumyr_make_int((long long)((lumyr_struct_Point*)lmvar_p1.v.struct_ptr)->y);
        __kv[4] = lumyr_make_string("__mapname__");
        __kv[5] = lumyr_make_string("Point");
        __kv[6] = lumyr_make_string("__structname__");
        __kv[7] = lumyr_make_string("Point");
        Value __v = lumyr_map_lit(__kv, 4);
        __stk[__sp++] = __v;
    }
    gc_stw_check_fast();
    {
        int __lmin_argc = 1;
        Value __args[1];
        for (int __k = 0; __k < __lmin_argc; __k++) __args[__k] = __stk[__sp - __lmin_argc + __k];
        __sp -= __lmin_argc;
        __stk[__sp++] = lumyr_func_modify_by_value(__args[0]);
    }
    __sp--;
    __stk[__sp++] = lumyr_make_string("after:  p1.x =");
    __stk[__sp++] = lumyr_make_int((long long)((lumyr_struct_Point*)lmvar_p1.v.struct_ptr)->x);
    { int __pcnt = 2; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    __stk[__sp++] = lumyr_make_int((long long)((lumyr_struct_Point*)lmvar_p1.v.struct_ptr)->x);
    __stk[__sp++] = lumyr_make_int(10);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumyr_eq(__l, __r); __sp--; }
    gc_stw_check_fast();
    if (!lumyr_to_bool(__stk[--__sp])) goto L33;
    __stk[__sp++] = lumyr_make_string("PASS: 值传递，函数内修改不影响外部");
    { int __pcnt = 1; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    goto L35;
L33:;
    __stk[__sp++] = lumyr_make_string("FAIL: 值传递，函数内修改影响了外部");
    { int __pcnt = 1; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
L35:;
    __stk[__sp++] = lumyr_make_string("\\n--- 引用传递（ref）---");
    { int __pcnt = 1; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    gc_stw_check_fast();
    __stk[__sp++] = lumyr_make_string("__mapname__");
    __stk[__sp++] = lumyr_make_string("Point");
    __stk[__sp++] = lumyr_make_string("__structname__");
    __stk[__sp++] = lumyr_make_string("Point");
    __stk[__sp++] = lumyr_make_string("x");
    __stk[__sp++] = lumyr_make_int(10);
    __stk[__sp++] = lumyr_make_string("y");
    __stk[__sp++] = lumyr_make_int(20);
    gc_stw_check_fast();
    {
        Value __m = lumyr_map_lit(&__stk[__sp - 8], 4);
        __sp = __sp - 8 + 1;
        __stk[__sp - 1] = __m;
    }
    { Value __v = __stk[--__sp];
        ((lumyr_struct_Point*)lmvar_p2.v.struct_ptr)->x = (int)lumyr_map_get(__v, lumyr_make_string("x")).v.i;
        ((lumyr_struct_Point*)lmvar_p2.v.struct_ptr)->y = (int)lumyr_map_get(__v, lumyr_make_string("y")).v.i;
        __stk[__sp++] = __v;
    }
    __sp--;
    __stk[__sp++] = lumyr_make_string("before: p2.x =");
    __stk[__sp++] = lumyr_make_int((long long)((lumyr_struct_Point*)lmvar_p2.v.struct_ptr)->x);
    { int __pcnt = 2; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    __stk[__sp++] = lmvar_p2;
    gc_stw_check_fast();
    {
        int __lmin_argc = 1;
        Value __args[1];
        for (int __k = 0; __k < __lmin_argc; __k++) __args[__k] = __stk[__sp - __lmin_argc + __k];
        __sp -= __lmin_argc;
        __stk[__sp++] = lumyr_func_modify_by_ref(&__args[0]);
        __stk[__sp - 1 - 1] = __args[0];
    }
    __sp--;
    __stk[__sp++] = lumyr_make_string("after:  p2.x =");
    __stk[__sp++] = lumyr_make_int((long long)((lumyr_struct_Point*)lmvar_p2.v.struct_ptr)->x);
    { int __pcnt = 2; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    __stk[__sp++] = lumyr_make_int((long long)((lumyr_struct_Point*)lmvar_p2.v.struct_ptr)->x);
    __stk[__sp++] = lumyr_make_int(888);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumyr_eq(__l, __r); __sp--; }
    gc_stw_check_fast();
    if (!lumyr_to_bool(__stk[--__sp])) goto L64;
    __stk[__sp++] = lumyr_make_string("PASS: 引用传递，函数内修改影响外部");
    { int __pcnt = 1; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    goto L66;
L64:;
    __stk[__sp++] = lumyr_make_string("FAIL: 引用传递，函数内修改没有影响外部");
    { int __pcnt = 1; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
L66:;
    __stk[__sp++] = lumyr_make_string("\\n=== Done ===");
    { int __pcnt = 1; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    gc_stw_check_fast();
    gc_pop_cframe();
    return 0;
}

