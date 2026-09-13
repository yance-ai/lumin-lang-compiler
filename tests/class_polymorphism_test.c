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

typedef struct lumyr_class_Animal lumyr_class_Animal;
struct lumyr_class_Animal {
    long long name;
};

typedef struct lumyr_class_Dog lumyr_class_Dog;
struct lumyr_class_Dog {
    lumyr_class_Animal parent;
    long long name;
    long long breed;
};

typedef struct lumyr_class_Cat lumyr_class_Cat;
struct lumyr_class_Cat {
    lumyr_class_Animal parent;
    long long name;
    long long color;
};

static Value lmvar_d = {0};
static Value lmvar_c = {0};
static Value lmvar_animals = {0};
static Value lmvar_i = {0};

static Value lumyr_func_speak(Value);
static Value lumyr_func_get_name(Value);
static Value lumyr_func_Animal_speak_1(Value);
static Value lumyr_func_Animal_get_name_1(Value);
static Value lumyr_func_Dog_speak_1(Value);
static Value lumyr_func_Cat_speak_1(Value);
static Value lumyr_func_make_animal_speak(Value);
static Value lum_wrap_0(Value*, int, void*);
static Value lum_wrap_1(Value*, int, void*);
static Value lum_wrap_2(Value*, int, void*);
static Value lum_wrap_3(Value*, int, void*);
static Value lum_wrap_4(Value*, int, void*);
static Value lum_wrap_5(Value*, int, void*);
static Value lum_wrap_6(Value*, int, void*);
static Value lum_wrap_7(Value*, int, void*);
static Value lum_wrap_8(Value*, int, void*);
static RuntimeFunc lum_wrap_0_rf;
static RuntimeFunc lum_wrap_1_rf;
static RuntimeFunc lum_wrap_2_rf;
static RuntimeFunc lum_wrap_3_rf;
static RuntimeFunc lum_wrap_4_rf;
static RuntimeFunc lum_wrap_5_rf;
static RuntimeFunc lum_wrap_6_rf;
static RuntimeFunc lum_wrap_7_rf;
static RuntimeFunc lum_wrap_8_rf;

static Value lumyr_func_speak(Value lmloc_self)
{
    Value __stk[3];
    int __sp = 0;
    int __g_d0 = __g_depth; jmp_buf* __g_gj0 = g_err_jmp; int __g_fin0 = __g_fin_n;
    g_trace_push("speak");
    volatile Value* __local_ptrs[1] = { &lmloc_self };
    CFrame __frame;
    __frame.stack = __stk;
    __frame.sp = &__sp;
    __frame.stack_size = 3;
    __frame.local_ptrs = (Value**)__local_ptrs;
    __frame.nlocals = 1;
    gc_push_cframe(&__frame);
    gc_stw_check_fast();
    __stk[__sp++] = lumyr_make_string("Animal speaks");
    { int __pcnt = 1; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0;
    if(g_trace_n > 0) g_trace_n--;
    gc_pop_cframe();
    return val_none();
}

static Value lumyr_func_get_name(Value lmloc_self)
{
    Value __stk[4];
    int __sp = 0;
    int __g_d0 = __g_depth; jmp_buf* __g_gj0 = g_err_jmp; int __g_fin0 = __g_fin_n;
    g_trace_push("get_name");
    volatile Value* __local_ptrs[1] = { &lmloc_self };
    CFrame __frame;
    __frame.stack = __stk;
    __frame.sp = &__sp;
    __frame.stack_size = 4;
    __frame.local_ptrs = (Value**)__local_ptrs;
    __frame.nlocals = 1;
    gc_push_cframe(&__frame);
    gc_stw_check_fast();
    __stk[__sp++] = lmloc_self;
    __stk[__sp++] = lumyr_make_string("name");
    { Value __c = __stk[__sp-2], __idx = __stk[__sp-1]; __stk[__sp-2] = lumyr_index_get(__c, __idx); __sp--; }
    __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0;
    if(g_trace_n > 0) g_trace_n--;
    { Value __v = __stk[--__sp]; gc_pop_cframe(); gc_protect_push(__v); gc_protect_pop(); return __v; }
}

static Value lumyr_func_Animal_speak_1(Value lmloc_self)
{
    Value __stk[3];
    int __sp = 0;
    int __g_d0 = __g_depth; jmp_buf* __g_gj0 = g_err_jmp; int __g_fin0 = __g_fin_n;
    g_trace_push("speak");
    volatile Value* __local_ptrs[1] = { &lmloc_self };
    CFrame __frame;
    __frame.stack = __stk;
    __frame.sp = &__sp;
    __frame.stack_size = 3;
    __frame.local_ptrs = (Value**)__local_ptrs;
    __frame.nlocals = 1;
    gc_push_cframe(&__frame);
    gc_stw_check_fast();
    __stk[__sp++] = lumyr_make_string("Animal speaks");
    { int __pcnt = 1; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0;
    if(g_trace_n > 0) g_trace_n--;
    gc_pop_cframe();
    return val_none();
}

static Value lumyr_func_Animal_get_name_1(Value lmloc_self)
{
    Value __stk[4];
    int __sp = 0;
    int __g_d0 = __g_depth; jmp_buf* __g_gj0 = g_err_jmp; int __g_fin0 = __g_fin_n;
    g_trace_push("get_name");
    volatile Value* __local_ptrs[1] = { &lmloc_self };
    CFrame __frame;
    __frame.stack = __stk;
    __frame.sp = &__sp;
    __frame.stack_size = 4;
    __frame.local_ptrs = (Value**)__local_ptrs;
    __frame.nlocals = 1;
    gc_push_cframe(&__frame);
    gc_stw_check_fast();
    __stk[__sp++] = lmloc_self;
    __stk[__sp++] = lumyr_make_string("name");
    { Value __c = __stk[__sp-2], __idx = __stk[__sp-1]; __stk[__sp-2] = lumyr_index_get(__c, __idx); __sp--; }
    __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0;
    if(g_trace_n > 0) g_trace_n--;
    { Value __v = __stk[--__sp]; gc_pop_cframe(); gc_protect_push(__v); gc_protect_pop(); return __v; }
}

static Value lumyr_func_Dog_speak_1(Value lmloc_self)
{
    Value __stk[3];
    int __sp = 0;
    int __g_d0 = __g_depth; jmp_buf* __g_gj0 = g_err_jmp; int __g_fin0 = __g_fin_n;
    g_trace_push("speak");
    volatile Value* __local_ptrs[1] = { &lmloc_self };
    CFrame __frame;
    __frame.stack = __stk;
    __frame.sp = &__sp;
    __frame.stack_size = 3;
    __frame.local_ptrs = (Value**)__local_ptrs;
    __frame.nlocals = 1;
    gc_push_cframe(&__frame);
    gc_stw_check_fast();
    __stk[__sp++] = lumyr_make_string("Dog barks");
    { int __pcnt = 1; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0;
    if(g_trace_n > 0) g_trace_n--;
    gc_pop_cframe();
    return val_none();
}

static Value lumyr_func_Cat_speak_1(Value lmloc_self)
{
    Value __stk[3];
    int __sp = 0;
    int __g_d0 = __g_depth; jmp_buf* __g_gj0 = g_err_jmp; int __g_fin0 = __g_fin_n;
    g_trace_push("speak");
    volatile Value* __local_ptrs[1] = { &lmloc_self };
    CFrame __frame;
    __frame.stack = __stk;
    __frame.sp = &__sp;
    __frame.stack_size = 3;
    __frame.local_ptrs = (Value**)__local_ptrs;
    __frame.nlocals = 1;
    gc_push_cframe(&__frame);
    gc_stw_check_fast();
    __stk[__sp++] = lumyr_make_string("Cat meows");
    { int __pcnt = 1; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0;
    if(g_trace_n > 0) g_trace_n--;
    gc_pop_cframe();
    return val_none();
}

static Value lumyr_func_make_animal_speak(Value lmloc_animal)
{
    Value __stk[3];
    int __sp = 0;
    int __g_d0 = __g_depth; jmp_buf* __g_gj0 = g_err_jmp; int __g_fin0 = __g_fin_n;
    g_trace_push("make_animal_speak");
    volatile Value* __local_ptrs[1] = { &lmloc_animal };
    CFrame __frame;
    __frame.stack = __stk;
    __frame.sp = &__sp;
    __frame.stack_size = 3;
    __frame.local_ptrs = (Value**)__local_ptrs;
    __frame.nlocals = 1;
    gc_push_cframe(&__frame);
    gc_stw_check_fast();
    __stk[__sp++] = lmloc_animal;
    gc_stw_check_fast();
    {
        int __lmin_argc = 1;
        Value __args[1];
        for (int __k = 0; __k < __lmin_argc; __k++) __args[__k] = __stk[__sp - __lmin_argc + __k];
        __sp -= __lmin_argc;
        __stk[__sp++] = lumyr_func_speak(__args[0]);
    }
    __sp--;
    __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0;
    if(g_trace_n > 0) g_trace_n--;
    gc_pop_cframe();
    return val_none();
}

static Value lum_wrap_0(Value* a, int n, void* __ctx)
{
    Value p0 = (n > 0) ? a[0] : val_none();
    Value __wrap_ret = lumyr_func_speak(p0);
    return __wrap_ret;
}

static RuntimeFunc lum_wrap_0_rf = { (FuncEntry*)lum_wrap_0, 1, 0, NULL, 0 };

static Value lum_wrap_1(Value* a, int n, void* __ctx)
{
    Value p0 = (n > 0) ? a[0] : val_none();
    Value __wrap_ret = lumyr_func_get_name(p0);
    return __wrap_ret;
}

static RuntimeFunc lum_wrap_1_rf = { (FuncEntry*)lum_wrap_1, 1, 0, NULL, 0 };

static Value lum_wrap_2(Value* a, int n, void* __ctx)
{
    Value p0 = (n > 0) ? a[0] : val_none();
    Value __wrap_ret = lumyr_func_speak(p0);
    return __wrap_ret;
}

static RuntimeFunc lum_wrap_2_rf = { (FuncEntry*)lum_wrap_2, 1, 0, NULL, 0 };

static Value lum_wrap_3(Value* a, int n, void* __ctx)
{
    Value p0 = (n > 0) ? a[0] : val_none();
    Value __wrap_ret = lumyr_func_get_name(p0);
    return __wrap_ret;
}

static RuntimeFunc lum_wrap_3_rf = { (FuncEntry*)lum_wrap_3, 1, 0, NULL, 0 };

static Value lum_wrap_4(Value* a, int n, void* __ctx)
{
    Value p0 = (n > 0) ? a[0] : val_none();
    Value __wrap_ret = lumyr_func_speak(p0);
    return __wrap_ret;
}

static RuntimeFunc lum_wrap_4_rf = { (FuncEntry*)lum_wrap_4, 1, 0, NULL, 0 };

static Value lum_wrap_5(Value* a, int n, void* __ctx)
{
    Value p0 = (n > 0) ? a[0] : val_none();
    Value __wrap_ret = lumyr_func_speak(p0);
    return __wrap_ret;
}

static RuntimeFunc lum_wrap_5_rf = { (FuncEntry*)lum_wrap_5, 1, 0, NULL, 0 };

static Value lum_wrap_6(Value* a, int n, void* __ctx)
{
    Value p0 = (n > 0) ? a[0] : val_none();
    Value __wrap_ret = lumyr_func_speak(p0);
    return __wrap_ret;
}

static RuntimeFunc lum_wrap_6_rf = { (FuncEntry*)lum_wrap_6, 1, 0, NULL, 0 };

static Value lum_wrap_7(Value* a, int n, void* __ctx)
{
    Value p0 = (n > 0) ? a[0] : val_none();
    Value __wrap_ret = lumyr_func_speak(p0);
    return __wrap_ret;
}

static RuntimeFunc lum_wrap_7_rf = { (FuncEntry*)lum_wrap_7, 1, 0, NULL, 0 };

static Value lum_wrap_8(Value* a, int n, void* __ctx)
{
    Value p0 = (n > 0) ? a[0] : val_none();
    Value __wrap_ret = lumyr_func_make_animal_speak(p0);
    return __wrap_ret;
}

static RuntimeFunc lum_wrap_8_rf = { (FuncEntry*)lum_wrap_8, 1, 0, NULL, 0 };

static Value (*const lumyr_cfunc_tbl[])(Value*, int, void*) = {
    lum_wrap_0,
    lum_wrap_1,
    lum_wrap_2,
    lum_wrap_3,
    lum_wrap_4,
    lum_wrap_5,
    lum_wrap_6,
    lum_wrap_7,
    lum_wrap_8,
};

int main(void){
    Value __stk[12];
    int __sp = 0;
    int __g_d0 = __g_depth; jmp_buf* __g_gj0 = g_err_jmp; int __g_fin0 = __g_fin_n;
    volatile Value* __local_ptrs[4] = { &lmvar_d, &lmvar_c, &lmvar_animals, &lmvar_i };
    CFrame __frame;
    __frame.stack = __stk;
    __frame.sp = &__sp;
    __frame.stack_size = 12;
    __frame.local_ptrs = (Value**)__local_ptrs;
    __frame.nlocals = 4;
    gc_push_cframe(&__frame);
    gc_stw_check_fast();
    __stk[__sp++] = lumyr_make_string("=== Class Polymorphism Test ===");
    { int __pcnt = 1; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    __stk[__sp++] = lumyr_make_string("--- 1. 多态方法调用 ---");
    { int __pcnt = 1; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    __stk[__sp++] = lumyr_make_string("__mapname__");
    __stk[__sp++] = lumyr_make_string("Dog");
    __stk[__sp++] = lumyr_make_string("__structname__");
    gc_stw_check_fast();
    __stk[__sp++] = lumyr_make_string("Dog");
    __stk[__sp++] = lumyr_make_string("__classname__");
    __stk[__sp++] = lumyr_make_string("Dog");
    __stk[__sp++] = lumyr_make_string("name");
    __stk[__sp++] = lumyr_make_string("Buddy");
    __stk[__sp++] = lumyr_make_string("breed");
    __stk[__sp++] = lumyr_make_string("Golden");
    {
        Value __m = lumyr_map_lit(&__stk[__sp - 10], 5);
        __sp = __sp - 10 + 1;
        __stk[__sp - 1] = __m;
    }
    gc_stw_check_fast();
    { Value __v = __stk[--__sp]; lmvar_d = __v; __stk[__sp++] = __v; }
    __sp--;
    __stk[__sp++] = lumyr_make_string("__mapname__");
    __stk[__sp++] = lumyr_make_string("Cat");
    __stk[__sp++] = lumyr_make_string("__structname__");
    __stk[__sp++] = lumyr_make_string("Cat");
    __stk[__sp++] = lumyr_make_string("__classname__");
    __stk[__sp++] = lumyr_make_string("Cat");
    gc_stw_check_fast();
    __stk[__sp++] = lumyr_make_string("name");
    __stk[__sp++] = lumyr_make_string("Whiskers");
    __stk[__sp++] = lumyr_make_string("color");
    __stk[__sp++] = lumyr_make_string("Gray");
    {
        Value __m = lumyr_map_lit(&__stk[__sp - 10], 5);
        __sp = __sp - 10 + 1;
        __stk[__sp - 1] = __m;
    }
    { Value __v = __stk[--__sp]; lmvar_c = __v; __stk[__sp++] = __v; }
    __sp--;
    __stk[__sp++] = lmvar_d;
    gc_stw_check_fast();
    {
        int __lmin_argc = 1;
        Value __args[1];
        for (int __k = 0; __k < __lmin_argc; __k++) __args[__k] = __stk[__sp - __lmin_argc + __k];
        __sp -= __lmin_argc;
        if(__args[0].type == VAL_MAP && lumyr_map_has(__args[0], lumyr_make_string("__mapname__"))) {
            __args[0] = lumyr_map_shallow_copy(__args[0]);
        }
        __stk[__sp++] = lumyr_func_make_animal_speak(__args[0]);
    }
    __sp--;
    __stk[__sp++] = lmvar_c;
    gc_stw_check_fast();
    {
        int __lmin_argc = 1;
        Value __args[1];
        for (int __k = 0; __k < __lmin_argc; __k++) __args[__k] = __stk[__sp - __lmin_argc + __k];
        __sp -= __lmin_argc;
        if(__args[0].type == VAL_MAP && lumyr_map_has(__args[0], lumyr_make_string("__mapname__"))) {
            __args[0] = lumyr_map_shallow_copy(__args[0]);
        }
        __stk[__sp++] = lumyr_func_make_animal_speak(__args[0]);
    }
    __sp--;
    __stk[__sp++] = lumyr_make_string("\\n--- 2. 数组中的多态 ---");
    { int __pcnt = 1; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    __stk[__sp++] = lmvar_d;
    __stk[__sp++] = lmvar_c;
    {
        Value __arr = val_array(2);
        gc_write_barrier(__stk[__sp - 2 + 0]);
        __arr.v.array->items[0] = __stk[__sp - 2 + 0];
        gc_write_barrier(__stk[__sp - 2 + 1]);
        __arr.v.array->items[1] = __stk[__sp - 2 + 1];
        __sp = __sp - 2 + 1;
        __stk[__sp - 1] = __arr;
    }
    { Value __v = __stk[--__sp]; lmvar_animals = __v; __stk[__sp++] = __v; }
    gc_stw_check_fast();
    __sp--;
    __stk[__sp++] = lumyr_make_int(0);
    { Value __v = __stk[--__sp]; lmvar_i = __v; __stk[__sp++] = __v; }
    __sp--;
L46:;
    __stk[__sp++] = lmvar_i;
    __stk[__sp++] = lmvar_animals;
    gc_stw_check_fast();
    { Value __v = __stk[__sp-1]; __stk[__sp-1] = lumyr_len(__v); }
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumyr_lt(__l, __r); __sp--; }
    if (!lumyr_to_bool(__stk[--__sp])) goto L59;
    __stk[__sp++] = lmvar_animals;
    __stk[__sp++] = lmvar_i;
    { Value __c = __stk[__sp-2], __idx = __stk[__sp-1]; __stk[__sp-2] = lumyr_index_get(__c, __idx); __sp--; }
    gc_stw_check_fast();
    {
        int __lmin_argc = 1;
        Value __args[1];
        for (int __k = 0; __k < __lmin_argc; __k++) __args[__k] = __stk[__sp - __lmin_argc + __k];
        __sp -= __lmin_argc;
        __stk[__sp++] = lumyr_func_speak(__args[0]);
    }
    __sp--;
    { Value* __vp = &lmvar_i; __stk[__sp++] = lumyr_post_inc(__vp); }
    __sp--;
    gc_stw_check_fast();
    goto L46;
L59:;
    __stk[__sp++] = lumyr_make_string("\\n--- 3. 继承的方法 ---");
    { int __pcnt = 1; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    __stk[__sp++] = lumyr_make_string("Dog name:");
    __stk[__sp++] = lmvar_d;
    gc_stw_check_fast();
    {
        int __lmin_argc = 1;
        Value __args[1];
        for (int __k = 0; __k < __lmin_argc; __k++) __args[__k] = __stk[__sp - __lmin_argc + __k];
        __sp -= __lmin_argc;
        __stk[__sp++] = lumyr_func_get_name(__args[0]);
    }
    { int __pcnt = 2; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    __stk[__sp++] = lumyr_make_string("Cat name:");
    __stk[__sp++] = lmvar_c;
    gc_stw_check_fast();
    {
        int __lmin_argc = 1;
        Value __args[1];
        for (int __k = 0; __k < __lmin_argc; __k++) __args[__k] = __stk[__sp - __lmin_argc + __k];
        __sp -= __lmin_argc;
        __stk[__sp++] = lumyr_func_get_name(__args[0]);
    }
    { int __pcnt = 2; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    __stk[__sp++] = lumyr_make_string("\\n=== All Class Polymorphism Tests Passed ===");
    { int __pcnt = 1; if(__pcnt <= 0) __pcnt = 1; int __pbase = __sp - __pcnt;
      for(int __pi = 0; __pi < __pcnt; __pi++) { if(__pi > 0) printf(" "); lumyr_print_inline(__stk[__pbase + __pi]); }
      printf("\n"); __sp -= __pcnt; }
    gc_pop_cframe();
    return 0;
}

