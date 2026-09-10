#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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


__attribute__((weak)) void lumyr_interp_scan_captures(const RuntimeFunc* rf, void (*mark)(Value)) { (void)rf; (void)mark; }

static Value lmvar_i = {0};
static Value lmvar_r = {0};

static Value lumyr_func_counter(Value);
static Value lumyr_func_choose(Value);
static Value lumyr_func_early(Value);
static Value lumyr_func_add2(Value);
static Value lumyr_func_combo(Value);
static Value lum_wrap_0(Value*, int, void*);
static Value lum_wrap_1(Value*, int, void*);
static Value lum_wrap_2(Value*, int, void*);
static Value lum_wrap_3(Value*, int, void*);
static Value lum_wrap_4(Value*, int, void*);
static RuntimeFunc lum_wrap_0_rf;
static RuntimeFunc lum_wrap_1_rf;
static RuntimeFunc lum_wrap_2_rf;
static RuntimeFunc lum_wrap_3_rf;
static RuntimeFunc lum_wrap_4_rf;

static Value lumyr_func_counter(Value lmloc_x)
{
    Value __stk[4];
    int __sp = 0;
    int __g_d0 = __g_depth; jmp_buf* __g_gj0 = g_err_jmp; int __g_fin0 = __g_fin_n;
    g_trace_push("counter");
    Value* __local_ptrs[1] = { &lmloc_x };
    CFrame __frame;
    __frame.stack = __stk;
    __frame.sp = &__sp;
    __frame.stack_size = 4;
    __frame.local_ptrs = __local_ptrs;
    __frame.nlocals = 1;
    gc_push_cframe(&__frame);
    gc_stw_check_fast();
    __stk[__sp++] = lmloc_x;
    __stk[__sp++] = lumyr_make_int(1);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumyr_add(__l, __r); __sp--; }
    { Value __v = __stk[--__sp]; lmloc_x = __v; __stk[__sp++] = __v; }
    __sp--;
    __stk[__sp++] = lmloc_x;
    __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0;
    if(g_trace_n > 0) g_trace_n--;
    { Value __v = __stk[--__sp]; gc_protect_push(__v); gc_pop_cframe(); gc_protect_pop(); return __v; }
}

static Value lumyr_func_choose(Value lmloc_v)
{
    Value __stk[4];
    int __sp = 0;
    int __g_d0 = __g_depth; jmp_buf* __g_gj0 = g_err_jmp; int __g_fin0 = __g_fin_n;
    g_trace_push("choose");
    Value* __local_ptrs[1] = { &lmloc_v };
    CFrame __frame;
    __frame.stack = __stk;
    __frame.sp = &__sp;
    __frame.stack_size = 4;
    __frame.local_ptrs = __local_ptrs;
    __frame.nlocals = 1;
    gc_push_cframe(&__frame);
    gc_stw_check_fast();
    __stk[__sp++] = lmloc_v;
    __stk[__sp++] = lumyr_make_int(0);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumyr_gt(__l, __r); __sp--; }
    if (!lumyr_to_bool(__stk[--__sp])) goto L8;
    __stk[__sp++] = lmloc_v;
    __stk[__sp++] = lumyr_make_int(10);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumyr_mul(__l, __r); __sp--; }
    gc_stw_check_fast();
    __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0;
    if(g_trace_n > 0) g_trace_n--;
    { Value __v = __stk[--__sp]; gc_protect_push(__v); gc_pop_cframe(); gc_protect_pop(); return __v; }
L8:;
    __stk[__sp++] = lumyr_make_int(0);
    __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0;
    if(g_trace_n > 0) g_trace_n--;
    { Value __v = __stk[--__sp]; gc_protect_push(__v); gc_pop_cframe(); gc_protect_pop(); return __v; }
}

static Value lumyr_func_early(Value lmloc_x)
{
    Value __stk[4];
    int __sp = 0;
    int __g_d0 = __g_depth; jmp_buf* __g_gj0 = g_err_jmp; int __g_fin0 = __g_fin_n;
    g_trace_push("early");
    Value lmloc_j = val_none();
    Value* __local_ptrs[2] = { &lmloc_x, &lmloc_j };
    CFrame __frame;
    __frame.stack = __stk;
    __frame.sp = &__sp;
    __frame.stack_size = 4;
    __frame.local_ptrs = __local_ptrs;
    __frame.nlocals = 2;
    gc_push_cframe(&__frame);
    gc_stw_check_fast();
    __stk[__sp++] = lumyr_make_int(0);
    { Value __v = __stk[--__sp]; lmloc_j = __v; __stk[__sp++] = __v; }
    __sp--;
    __stk[__sp++] = lumyr_make_int(0);
    { Value __v = __stk[--__sp]; lmloc_j = __v; __stk[__sp++] = __v; }
    __sp--;
L6:;
    __stk[__sp++] = lmloc_j;
    gc_stw_check_fast();
    __stk[__sp++] = lumyr_make_int(10);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumyr_lt(__l, __r); __sp--; }
    if (!lumyr_to_bool(__stk[--__sp])) goto L24;
    __stk[__sp++] = lmloc_j;
    __stk[__sp++] = lmloc_x;
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumyr_eq(__l, __r); __sp--; }
    if (!lumyr_to_bool(__stk[--__sp])) goto L18;
    __stk[__sp++] = lmloc_j;
    gc_stw_check_fast();
    __stk[__sp++] = lumyr_make_int(100);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumyr_mul(__l, __r); __sp--; }
    __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0;
    if(g_trace_n > 0) g_trace_n--;
    { Value __v = __stk[--__sp]; gc_protect_push(__v); gc_pop_cframe(); gc_protect_pop(); return __v; }
L18:;
    __stk[__sp++] = lmloc_j;
    __stk[__sp++] = lumyr_make_int(1);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumyr_add(__l, __r); __sp--; }
    { Value __v = __stk[--__sp]; lmloc_j = __v; __stk[__sp++] = __v; }
    __sp--;
    gc_stw_check_fast();
    goto L6;
L24:;
    __stk[__sp++] = lumyr_make_int(-1);
    __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0;
    if(g_trace_n > 0) g_trace_n--;
    { Value __v = __stk[--__sp]; gc_protect_push(__v); gc_pop_cframe(); gc_protect_pop(); return __v; }
}

static Value lumyr_func_add2(Value lmloc_a)
{
    Value __stk[4];
    int __sp = 0;
    int __g_d0 = __g_depth; jmp_buf* __g_gj0 = g_err_jmp; int __g_fin0 = __g_fin_n;
    g_trace_push("add2");
    Value* __local_ptrs[1] = { &lmloc_a };
    CFrame __frame;
    __frame.stack = __stk;
    __frame.sp = &__sp;
    __frame.stack_size = 4;
    __frame.local_ptrs = __local_ptrs;
    __frame.nlocals = 1;
    gc_push_cframe(&__frame);
    gc_stw_check_fast();
    __stk[__sp++] = lmloc_a;
    __stk[__sp++] = lumyr_make_int(2);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumyr_add(__l, __r); __sp--; }
    __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0;
    if(g_trace_n > 0) g_trace_n--;
    { Value __v = __stk[--__sp]; gc_protect_push(__v); gc_pop_cframe(); gc_protect_pop(); return __v; }
}

static Value lumyr_func_combo(Value lmloc_n)
{
    Value __stk[4];
    int __sp = 0;
    int __g_d0 = __g_depth; jmp_buf* __g_gj0 = g_err_jmp; int __g_fin0 = __g_fin_n;
    g_trace_push("combo");
    Value lmloc_k = val_none();
    Value* __local_ptrs[2] = { &lmloc_n, &lmloc_k };
    CFrame __frame;
    __frame.stack = __stk;
    __frame.sp = &__sp;
    __frame.stack_size = 4;
    __frame.local_ptrs = __local_ptrs;
    __frame.nlocals = 2;
    gc_push_cframe(&__frame);
    gc_stw_check_fast();
    __stk[__sp++] = lumyr_make_int(0);
    { Value __v = __stk[--__sp]; lmloc_k = __v; __stk[__sp++] = __v; }
    __sp--;
L3:;
    __stk[__sp++] = lmloc_k;
    __stk[__sp++] = lumyr_make_int(100);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumyr_lt(__l, __r); __sp--; }
    if (!lumyr_to_bool(__stk[--__sp])) goto L23;
    gc_stw_check_fast();
    __stk[__sp++] = lmloc_k;
    __stk[__sp++] = lumyr_make_int(1);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumyr_add(__l, __r); __sp--; }
    { Value __v = __stk[--__sp]; lmloc_k = __v; __stk[__sp++] = __v; }
    __sp--;
    __stk[__sp++] = lmloc_k;
    __stk[__sp++] = lumyr_make_int(2);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumyr_eq(__l, __r); __sp--; }
    gc_stw_check_fast();
    if (!lumyr_to_bool(__stk[--__sp])) goto L17;
    gc_stw_check_fast();
    goto L3;
L17:;
    __stk[__sp++] = lmloc_k;
    __stk[__sp++] = lmloc_n;
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumyr_gt(__l, __r); __sp--; }
    if (!lumyr_to_bool(__stk[--__sp])) goto L22;
    goto L23;
L22:;
    gc_stw_check_fast();
    goto L3;
L23:;
    __stk[__sp++] = lmloc_k;
    __g_depth = __g_d0; g_err_jmp = __g_gj0; __g_fin_n = __g_fin0;
    if(g_trace_n > 0) g_trace_n--;
    { Value __v = __stk[--__sp]; gc_protect_push(__v); gc_pop_cframe(); gc_protect_pop(); return __v; }
}

static Value lum_wrap_0(Value* a, int n, void* __ctx)
{
    Value p0 = (n > 0) ? a[0] : val_none();
    return lumyr_func_counter(p0);
}

static RuntimeFunc lum_wrap_0_rf = { (FuncEntry*)lum_wrap_0, 1, 0, NULL, 0 };

static Value lum_wrap_1(Value* a, int n, void* __ctx)
{
    Value p0 = (n > 0) ? a[0] : val_none();
    return lumyr_func_choose(p0);
}

static RuntimeFunc lum_wrap_1_rf = { (FuncEntry*)lum_wrap_1, 1, 0, NULL, 0 };

static Value lum_wrap_2(Value* a, int n, void* __ctx)
{
    Value p0 = (n > 0) ? a[0] : val_none();
    return lumyr_func_early(p0);
}

static RuntimeFunc lum_wrap_2_rf = { (FuncEntry*)lum_wrap_2, 1, 0, NULL, 0 };

static Value lum_wrap_3(Value* a, int n, void* __ctx)
{
    Value p0 = (n > 0) ? a[0] : val_none();
    return lumyr_func_add2(p0);
}

static RuntimeFunc lum_wrap_3_rf = { (FuncEntry*)lum_wrap_3, 1, 0, NULL, 0 };

static Value lum_wrap_4(Value* a, int n, void* __ctx)
{
    Value p0 = (n > 0) ? a[0] : val_none();
    return lumyr_func_combo(p0);
}

static RuntimeFunc lum_wrap_4_rf = { (FuncEntry*)lum_wrap_4, 1, 0, NULL, 0 };

static Value (*const lumyr_cfunc_tbl[])(Value*, int, void*) = {
    lum_wrap_0,
    lum_wrap_1,
    lum_wrap_2,
    lum_wrap_3,
    lum_wrap_4,
};

int main(void){
    Value __stk[4];
    int __sp = 0;
    int __g_d0 = __g_depth; jmp_buf* __g_gj0 = g_err_jmp; int __g_fin0 = __g_fin_n;
    Value* __local_ptrs[2] = { &lmvar_i, &lmvar_r };
    CFrame __frame;
    __frame.stack = __stk;
    __frame.sp = &__sp;
    __frame.stack_size = 4;
    __frame.local_ptrs = __local_ptrs;
    __frame.nlocals = 2;
    gc_push_cframe(&__frame);
    gc_stw_check_fast();
    __stk[__sp++] = lumyr_make_int(0);
    { Value __v = __stk[--__sp]; lmvar_i = __v; __stk[__sp++] = __v; }
    __sp--;
L3:;
    __stk[__sp++] = lmvar_i;
    __stk[__sp++] = lumyr_make_int(3);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumyr_lt(__l, __r); __sp--; }
    if (!lumyr_to_bool(__stk[--__sp])) goto L15;
    gc_stw_check_fast();
    __stk[__sp++] = lmvar_i;
    lumyr_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lmvar_i;
    gc_stw_check_fast();
    {
        int __lmin_argc = 1;
        Value __args[1];
        for (int __k = 0; __k < __lmin_argc; __k++) __args[__k] = __stk[__sp - __lmin_argc + __k];
        __sp -= __lmin_argc;
        __stk[__sp++] = lumyr_func_counter(__args[0]);
    }
    { Value __v = __stk[--__sp]; lmvar_i = __v; __stk[__sp++] = __v; }
    __sp--;
    gc_stw_check_fast();
    goto L3;
L15:;
    __stk[__sp++] = lumyr_make_int(2);
    gc_stw_check_fast();
    {
        int __lmin_argc = 1;
        Value __args[1];
        for (int __k = 0; __k < __lmin_argc; __k++) __args[__k] = __stk[__sp - __lmin_argc + __k];
        __sp -= __lmin_argc;
        __stk[__sp++] = lumyr_func_choose(__args[0]);
    }
    __stk[__sp++] = lumyr_make_int(0);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumyr_gt(__l, __r); __sp--; }
    if (!lumyr_to_bool(__stk[--__sp])) goto L22;
    __stk[__sp++] = lumyr_make_string("pos");
    goto L23;
L22:;
    __stk[__sp++] = lumyr_make_string("neg");
L23:;
    { Value __v = __stk[--__sp]; lmvar_r = __v; __stk[__sp++] = __v; }
    gc_stw_check_fast();
    __sp--;
    __stk[__sp++] = lmvar_r;
    lumyr_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lumyr_make_int(3);
    gc_stw_check_fast();
    {
        int __lmin_argc = 1;
        Value __args[1];
        for (int __k = 0; __k < __lmin_argc; __k++) __args[__k] = __stk[__sp - __lmin_argc + __k];
        __sp -= __lmin_argc;
        __stk[__sp++] = lumyr_func_early(__args[0]);
    }
    lumyr_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lumyr_make_int(20);
    gc_stw_check_fast();
    {
        int __lmin_argc = 1;
        Value __args[1];
        for (int __k = 0; __k < __lmin_argc; __k++) __args[__k] = __stk[__sp - __lmin_argc + __k];
        __sp -= __lmin_argc;
        __stk[__sp++] = lumyr_func_early(__args[0]);
    }
    lumyr_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lumyr_make_int(1);
    gc_stw_check_fast();
    {
        int __lmin_argc = 1;
        Value __args[1];
        for (int __k = 0; __k < __lmin_argc; __k++) __args[__k] = __stk[__sp - __lmin_argc + __k];
        __sp -= __lmin_argc;
        __stk[__sp++] = lumyr_func_add2(__args[0]);
    }
    gc_stw_check_fast();
    {
        int __lmin_argc = 1;
        Value __args[1];
        for (int __k = 0; __k < __lmin_argc; __k++) __args[__k] = __stk[__sp - __lmin_argc + __k];
        __sp -= __lmin_argc;
        __stk[__sp++] = lumyr_func_add2(__args[0]);
    }
    gc_stw_check_fast();
    {
        int __lmin_argc = 1;
        Value __args[1];
        for (int __k = 0; __k < __lmin_argc; __k++) __args[__k] = __stk[__sp - __lmin_argc + __k];
        __sp -= __lmin_argc;
        __stk[__sp++] = lumyr_func_add2(__args[0]);
    }
    lumyr_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lumyr_make_int(5);
    gc_stw_check_fast();
    {
        int __lmin_argc = 1;
        Value __args[1];
        for (int __k = 0; __k < __lmin_argc; __k++) __args[__k] = __stk[__sp - __lmin_argc + __k];
        __sp -= __lmin_argc;
        __stk[__sp++] = lumyr_func_combo(__args[0]);
    }
    lumyr_print(__stk[__sp-1]);
    __sp--;
    gc_pop_cframe();
    return 0;
}

