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
#include "lumin_value.h"


__attribute__((weak)) void lumin_interp_scan_captures(const RuntimeFunc* rf, void (*mark)(Value)) { (void)rf; (void)mark; }

static Value lmvar_a = {0};
static Value lmvar_b = {0};
static Value lmvar_c = {0};
static Value lmvar_msg = {0};
static Value lmvar_testint = {0};
static Value lmvar_addmsg = {0};
static Value lmvar_d = {0};
static Value lmvar_e = {0};
static Value lmvar_f = {0};
static Value lmvar_g = {0};
static Value lmvar_j = {0};
static Value lmvar_ch = {0};
static Value lmvar_h = {0};
static Value lmvar_x = {0};


static Value (*const lumin_cfunc_tbl[])(Value*, int, void*) = {
};

int main(void){
    Value __stk[5];
    int __sp = 0;
    int __g_d0 = __g_depth; jmp_buf* __g_gj0 = g_err_jmp; int __g_fin0 = __g_fin_n;
    Value* __local_ptrs[14] = { &lmvar_a, &lmvar_b, &lmvar_c, &lmvar_msg, &lmvar_testint, &lmvar_addmsg, &lmvar_d, &lmvar_e, &lmvar_f, &lmvar_g, &lmvar_j, &lmvar_ch, &lmvar_h, &lmvar_x };
    CFrame __frame;
    __frame.stack = __stk;
    __frame.sp = &__sp;
    __frame.stack_size = 5;
    __frame.local_ptrs = __local_ptrs;
    __frame.nlocals = 14;
    gc_push_cframe(&__frame);
    gc_stw_check_fast();
    __stk[__sp++] = lumin_make_int(30);
    { Value __v = __stk[--__sp]; lmvar_a = __v; __stk[__sp++] = __v; }
    __sp--;
    __stk[__sp++] = lmvar_a;
    lumin_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lmvar_a;
    gc_stw_check_fast();
    __stk[__sp++] = lumin_make_double(2.1459999999999999);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_mul(__l, __r); __sp--; }
    { Value __v = __stk[--__sp]; lmvar_b = __v; __stk[__sp++] = __v; }
    __sp--;
    __stk[__sp++] = lmvar_b;
    lumin_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lumin_make_bool(1);
    gc_stw_check_fast();
    { Value __v = __stk[--__sp]; lmvar_c = __v; __stk[__sp++] = __v; }
    __sp--;
    __stk[__sp++] = lmvar_c;
    lumin_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lumin_make_string("hello compiler");
    { Value __v = __stk[--__sp]; lmvar_msg = __v; __stk[__sp++] = __v; }
    __sp--;
    gc_stw_check_fast();
    __stk[__sp++] = lmvar_msg;
    lumin_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lumin_make_int(1);
    { Value __v = __stk[--__sp]; lmvar_testint = __v; __stk[__sp++] = __v; }
    __sp--;
    __stk[__sp++] = lmvar_testint;
    lumin_print(__stk[__sp-1]);
    gc_stw_check_fast();
    __sp--;
    __stk[__sp++] = lmvar_msg;
    __stk[__sp++] = lmvar_testint;
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    __stk[__sp++] = lmvar_c;
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    { Value __v = __stk[--__sp]; lmvar_addmsg = __v; __stk[__sp++] = __v; }
    __sp--;
    gc_stw_check_fast();
    __stk[__sp++] = lmvar_addmsg;
    lumin_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lmvar_msg;
    __stk[__sp++] = lmvar_b;
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    lumin_print(__stk[__sp-1]);
    __sp--;
    gc_stw_check_fast();
    __stk[__sp++] = lmvar_msg;
    __stk[__sp++] = lmvar_c;
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    lumin_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lumin_make_int(10);
    { Value __v = __stk[--__sp]; lmvar_d = __v; __stk[__sp++] = __v; }
    __sp--;
    gc_stw_check_fast();
    __stk[__sp++] = lmvar_d;
    __stk[__sp++] = lumin_make_int(5);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_gt(__l, __r); __sp--; }
    if (!lumin_to_bool(__stk[--__sp])) goto L63;
    __stk[__sp++] = lumin_make_bool(1);
    lumin_print(__stk[__sp-1]);
    __sp--;
    goto L74;
L63:;
    gc_stw_check_fast();
    __stk[__sp++] = lmvar_d;
    __stk[__sp++] = lumin_make_int(5);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_lt(__l, __r); __sp--; }
    if (!lumin_to_bool(__stk[--__sp])) goto L71;
    __stk[__sp++] = lumin_make_bool(0);
    lumin_print(__stk[__sp-1]);
    __sp--;
    goto L74;
L71:;
    gc_stw_check_fast();
    __stk[__sp++] = lumin_make_int(0);
    lumin_print(__stk[__sp-1]);
    __sp--;
L74:;
    __stk[__sp++] = lumin_make_int(20);
    { Value __v = __stk[--__sp]; lmvar_e = __v; __stk[__sp++] = __v; }
    __sp--;
    __stk[__sp++] = lmvar_e;
    __stk[__sp++] = lumin_make_int(100);
    gc_stw_check_fast();
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_gt(__l, __r); __sp--; }
    if (!lumin_to_bool(__stk[--__sp])) goto L88;
    __stk[__sp++] = lumin_make_bool(1);
    lumin_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lumin_make_int(999);
    { Value __v = __stk[--__sp]; lmvar_e = __v; __stk[__sp++] = __v; }
    __sp--;
    gc_stw_check_fast();
    goto L99;
L88:;
    __stk[__sp++] = lmvar_e;
    __stk[__sp++] = lumin_make_int(50);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_lt(__l, __r); __sp--; }
    if (!lumin_to_bool(__stk[--__sp])) goto L96;
    __stk[__sp++] = lumin_make_bool(0);
    lumin_print(__stk[__sp-1]);
    __sp--;
    gc_stw_check_fast();
    goto L99;
L96:;
    __stk[__sp++] = lumin_make_int(111);
    lumin_print(__stk[__sp-1]);
    __sp--;
L99:;
    __stk[__sp++] = lumin_make_double(20);
    { Value __v = __stk[--__sp]; lmvar_f = __v; __stk[__sp++] = __v; }
    __sp--;
    __stk[__sp++] = lmvar_f;
    gc_stw_check_fast();
    __stk[__sp++] = lumin_make_int(15);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_gt(__l, __r); __sp--; }
    if (!lumin_to_bool(__stk[--__sp])) goto L129;
    __stk[__sp++] = lmvar_f;
    lumin_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lmvar_f;
    __stk[__sp++] = lumin_make_int(5);
    gc_stw_check_fast();
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_gt(__l, __r); __sp--; }
    if (!lumin_to_bool(__stk[--__sp])) goto L117;
    __stk[__sp++] = lumin_make_bool(1);
    lumin_print(__stk[__sp-1]);
    __sp--;
    goto L117;
L117:;
    __stk[__sp++] = lmvar_f;
    __stk[__sp++] = lumin_make_int(50);
    gc_stw_check_fast();
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_gt(__l, __r); __sp--; }
    if (!lumin_to_bool(__stk[--__sp])) goto L125;
    __stk[__sp++] = lumin_make_string("50true");
    lumin_print(__stk[__sp-1]);
    __sp--;
    goto L128;
L125:;
    __stk[__sp++] = lumin_make_string("50false");
    lumin_print(__stk[__sp-1]);
    gc_stw_check_fast();
    __sp--;
L128:;
    goto L129;
L129:;
    __stk[__sp++] = lmvar_f;
    __stk[__sp++] = lumin_make_int(5);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_gt(__l, __r); __sp--; }
    if (!lumin_to_bool(__stk[--__sp])) goto L137;
    __stk[__sp++] = lumin_make_bool(1);
    lumin_print(__stk[__sp-1]);
    gc_stw_check_fast();
    __sp--;
    goto L137;
L137:;
    __stk[__sp++] = lumin_make_int(0);
    { Value __v = __stk[--__sp]; lmvar_g = __v; __stk[__sp++] = __v; }
    __sp--;
L140:;
    __stk[__sp++] = lmvar_g;
    __stk[__sp++] = lumin_make_int(5);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_lt(__l, __r); __sp--; }
    gc_stw_check_fast();
    if (!lumin_to_bool(__stk[--__sp])) goto L155;
    __stk[__sp++] = lumin_make_string("while-");
    __stk[__sp++] = lmvar_g;
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    lumin_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lmvar_g;
    __stk[__sp++] = lumin_make_int(1);
    gc_stw_check_fast();
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    { Value __v = __stk[--__sp]; lmvar_g = __v; __stk[__sp++] = __v; }
    __sp--;
    gc_stw_check_fast();
    goto L140;
L155:;
    __stk[__sp++] = lumin_make_int(0);
    { Value __v = __stk[--__sp]; lmvar_j = __v; __stk[__sp++] = __v; }
    __sp--;
L158:;
    __stk[__sp++] = lmvar_j;
    __stk[__sp++] = lumin_make_int(3);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_lt(__l, __r); __sp--; }
    if (!lumin_to_bool(__stk[--__sp])) goto L173;
    gc_stw_check_fast();
    __stk[__sp++] = lumin_make_string("for-");
    __stk[__sp++] = lmvar_j;
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    lumin_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lmvar_j;
    __stk[__sp++] = lumin_make_int(1);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    gc_stw_check_fast();
    { Value __v = __stk[--__sp]; lmvar_j = __v; __stk[__sp++] = __v; }
    __sp--;
    gc_stw_check_fast();
    goto L158;
L173:;
    __stk[__sp++] = lumin_make_double(0);
    { Value __v = __stk[--__sp]; lmvar_j = __v; __stk[__sp++] = __v; }
    __sp--;
L176:;
    __stk[__sp++] = lmvar_j;
    __stk[__sp++] = lumin_make_int(3);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_lt(__l, __r); __sp--; }
    if (!lumin_to_bool(__stk[--__sp])) goto L191;
    gc_stw_check_fast();
    __stk[__sp++] = lumin_make_string("for-");
    __stk[__sp++] = lmvar_j;
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    lumin_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lmvar_j;
    __stk[__sp++] = lumin_make_double(0.10000000000000001);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    gc_stw_check_fast();
    { Value __v = __stk[--__sp]; lmvar_j = __v; __stk[__sp++] = __v; }
    __sp--;
    gc_stw_check_fast();
    goto L176;
L191:;
    __stk[__sp++] = lumin_make_char('a');
    { Value __v = __stk[--__sp]; lmvar_ch = __v; __stk[__sp++] = __v; }
    __sp--;
L194:;
    __stk[__sp++] = lmvar_ch;
    __stk[__sp++] = lumin_make_char('z');
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_le(__l, __r); __sp--; }
    if (!lumin_to_bool(__stk[--__sp])) goto L209;
    gc_stw_check_fast();
    __stk[__sp++] = lumin_make_string("a-z:");
    __stk[__sp++] = lmvar_ch;
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    lumin_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lmvar_ch;
    __stk[__sp++] = lumin_make_int(1);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    gc_stw_check_fast();
    { Value __v = __stk[--__sp]; lmvar_ch = __v; __stk[__sp++] = __v; }
    __sp--;
    gc_stw_check_fast();
    goto L194;
L209:;
    __stk[__sp++] = lumin_make_char('A');
    { Value __v = __stk[--__sp]; lmvar_ch = __v; __stk[__sp++] = __v; }
    __sp--;
L212:;
    __stk[__sp++] = lmvar_ch;
    __stk[__sp++] = lumin_make_char('Z');
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_le(__l, __r); __sp--; }
    if (!lumin_to_bool(__stk[--__sp])) goto L227;
    gc_stw_check_fast();
    __stk[__sp++] = lumin_make_string("A-Z:");
    __stk[__sp++] = lmvar_ch;
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    lumin_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lmvar_ch;
    __stk[__sp++] = lumin_make_int(1);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    gc_stw_check_fast();
    { Value __v = __stk[--__sp]; lmvar_ch = __v; __stk[__sp++] = __v; }
    __sp--;
    gc_stw_check_fast();
    goto L212;
L227:;
    __stk[__sp++] = lumin_make_string("test-");
    lumin_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lumin_make_int(65);
    lumin_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lumin_make_char('A');
    gc_stw_check_fast();
    { Value __v = __stk[--__sp]; lmvar_ch = __v; __stk[__sp++] = __v; }
    __sp--;
L236:;
    __stk[__sp++] = lmvar_ch;
    __stk[__sp++] = lumin_make_char('z');
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_le(__l, __r); __sp--; }
    if (!lumin_to_bool(__stk[--__sp])) goto L277;
    __stk[__sp++] = lumin_make_string("A-z:");
    __stk[__sp++] = lmvar_ch;
    gc_stw_check_fast();
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    __stk[__sp++] = lumin_make_string("(int)");
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    __stk[__sp++] = lmvar_ch;
    { Value __v = __stk[__sp-1]; __stk[__sp-1] = lumin_cast_int(__v); }
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    __stk[__sp++] = lmvar_ch;
    { Value __v = __stk[__sp-1]; __stk[__sp-1] = lumin_cast_ascii(__v); }
    gc_stw_check_fast();
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    lumin_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lumin_make_string("A-z2:");
    __stk[__sp++] = lmvar_ch;
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    __stk[__sp++] = lumin_make_string("(int)");
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    gc_stw_check_fast();
    __stk[__sp++] = lmvar_ch;
    { Value __v = __stk[__sp-1]; __stk[__sp-1] = lumin_cast_int(__v); }
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    __stk[__sp++] = lumin_make_string("ASCII-");
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    __stk[__sp++] = lmvar_ch;
    { Value __v = __stk[__sp-1]; __stk[__sp-1] = lumin_cast_ascii(__v); }
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    gc_stw_check_fast();
    lumin_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lumin_make_string("ASCII=>");
    __stk[__sp++] = lmvar_ch;
    { Value __v = __stk[__sp-1]; __stk[__sp-1] = lumin_cast_ascii(__v); }
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    lumin_print(__stk[__sp-1]);
    __sp--;
    gc_stw_check_fast();
    { Value* __vp = &lmvar_ch; __stk[__sp++] = lumin_post_inc(__vp); }
    __sp--;
    gc_stw_check_fast();
    goto L236;
L277:;
    __stk[__sp++] = lumin_make_char('z');
    { Value __v = __stk[--__sp]; lmvar_ch = __v; __stk[__sp++] = __v; }
    __sp--;
L280:;
    __stk[__sp++] = lmvar_ch;
    __stk[__sp++] = lumin_make_int(90);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_ge(__l, __r); __sp--; }
    if (!lumin_to_bool(__stk[--__sp])) goto L303;
    gc_stw_check_fast();
    __stk[__sp++] = lumin_make_string("z-A:");
    __stk[__sp++] = lmvar_ch;
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    __stk[__sp++] = lmvar_ch;
    { Value __v = __stk[__sp-1]; __stk[__sp-1] = lumin_cast_int(__v); }
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    __stk[__sp++] = lmvar_ch;
    __stk[__sp++] = lumin_make_int(100);
    gc_stw_check_fast();
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_gt(__l, __r); __sp--; }
    if (!lumin_to_bool(__stk[--__sp])) goto L296;
    __stk[__sp++] = lumin_make_bool(1);
    goto L297;
L296:;
    __stk[__sp++] = lumin_make_bool(0);
L297:;
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    lumin_print(__stk[__sp-1]);
    __sp--;
    gc_stw_check_fast();
    { Value* __vp = &lmvar_ch; __stk[__sp++] = lumin_post_dec(__vp); }
    __sp--;
    gc_stw_check_fast();
    goto L280;
L303:;
    __stk[__sp++] = lumin_make_char('Z');
    { Value __v = __stk[--__sp]; lmvar_h = __v; __stk[__sp++] = __v; }
    __sp--;
    __stk[__sp++] = lumin_make_string("Z:");
    __stk[__sp++] = lmvar_h;
    { Value __v = __stk[__sp-1]; __stk[__sp-1] = lumin_cast_int(__v); }
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    gc_stw_check_fast();
    lumin_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lumin_make_string("Z>95:");
    __stk[__sp++] = lmvar_h;
    __stk[__sp++] = lumin_make_int(95);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_gt(__l, __r); __sp--; }
    if (!lumin_to_bool(__stk[--__sp])) goto L319;
    __stk[__sp++] = lumin_make_char('Z');
    gc_stw_check_fast();
    goto L320;
L319:;
    __stk[__sp++] = lumin_make_int(65);
L320:;
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    lumin_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lumin_make_string("Z>100:");
    __stk[__sp++] = lmvar_h;
    __stk[__sp++] = lumin_make_int(100);
    gc_stw_check_fast();
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_gt(__l, __r); __sp--; }
    if (!lumin_to_bool(__stk[--__sp])) goto L330;
    __stk[__sp++] = lumin_make_string("yes");
    goto L331;
L330:;
    __stk[__sp++] = lumin_make_string("no");
L331:;
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    lumin_print(__stk[__sp-1]);
    __sp--;
    gc_stw_check_fast();
    __stk[__sp++] = lumin_make_string("Z==95:");
    __stk[__sp++] = lmvar_h;
    __stk[__sp++] = lumin_make_int(90);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_eq(__l, __r); __sp--; }
    if (!lumin_to_bool(__stk[--__sp])) goto L341;
    __stk[__sp++] = lumin_make_string("yes");
    goto L342;
L341:;
    __stk[__sp++] = lumin_make_string("no");
L342:;
    gc_stw_check_fast();
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_add(__l, __r); __sp--; }
    lumin_print(__stk[__sp-1]);
    __sp--;
    __stk[__sp++] = lumin_make_string("111");
    { Value __v = __stk[--__sp]; lmvar_a = __v; __stk[__sp++] = __v; }
    __sp--;
    __stk[__sp++] = lmvar_a;
    __stk[__sp] = __stk[__sp-1]; __sp++;
    gc_stw_check_fast();
    __stk[__sp++] = lumin_make_int(1);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_eq(__l, __r); __sp--; }
    if (!lumin_to_bool(__stk[--__sp])) goto L358;
    __sp--;
    __stk[__sp++] = lumin_make_string("case 1 执行");
    lumin_print(__stk[__sp-1]);
    __sp--;
    goto L396;
L358:;
    gc_stw_check_fast();
    __stk[__sp] = __stk[__sp-1]; __sp++;
    __stk[__sp++] = lumin_make_double(2.1000000000000001);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_eq(__l, __r); __sp--; }
    if (!lumin_to_bool(__stk[--__sp])) goto L373;
    __sp--;
    __stk[__sp++] = lumin_make_string("case 2.1 命中！");
    lumin_print(__stk[__sp-1]);
    __sp--;
    gc_stw_check_fast();
    __stk[__sp++] = lumin_make_int(100);
    { Value __v = __stk[--__sp]; lmvar_x = __v; __stk[__sp++] = __v; }
    __sp--;
    __stk[__sp++] = lmvar_x;
    lumin_print(__stk[__sp-1]);
    __sp--;
    goto L396;
L373:;
    __stk[__sp] = __stk[__sp-1]; __sp++;
    gc_stw_check_fast();
    __stk[__sp++] = lumin_make_string("111");
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_eq(__l, __r); __sp--; }
    if (!lumin_to_bool(__stk[--__sp])) goto L382;
    __sp--;
    __stk[__sp++] = lumin_make_string("case3");
    lumin_print(__stk[__sp-1]);
    __sp--;
    gc_pop_cframe();
    return 0;
L382:;
    gc_stw_check_fast();
    __stk[__sp] = __stk[__sp-1]; __sp++;
    __stk[__sp++] = lumin_make_char('z');
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_eq(__l, __r); __sp--; }
    if (!lumin_to_bool(__stk[--__sp])) goto L391;
    __sp--;
    __stk[__sp++] = lumin_make_string("casez");
    lumin_print(__stk[__sp-1]);
    __sp--;
    gc_stw_check_fast();
    gc_pop_cframe();
    return 0;
L391:;
    __sp--;
    __stk[__sp++] = lumin_make_string("走到default分支");
    lumin_print(__stk[__sp-1]);
    __sp--;
    goto L396;
L396:;
    __stk[__sp++] = lumin_make_string("switch结束，继续往下跑");
    lumin_print(__stk[__sp-1]);
    gc_stw_check_fast();
    __sp--;
    __stk[__sp++] = lumin_make_int(4);
    { Value __v = __stk[--__sp]; lmvar_b = __v; __stk[__sp++] = __v; }
    __sp--;
    __stk[__sp++] = lmvar_b;
    __stk[__sp] = __stk[__sp-1]; __sp++;
    __stk[__sp++] = lumin_make_int(10);
    { Value __l = __stk[__sp-2], __r = __stk[__sp-1]; __stk[__sp-2] = lumin_eq(__l, __r); __sp--; }
    gc_stw_check_fast();
    if (!lumin_to_bool(__stk[--__sp])) goto L412;
    __sp--;
    __stk[__sp++] = lumin_make_string("不会跑");
    lumin_print(__stk[__sp-1]);
    __sp--;
    goto L417;
L412:;
    __sp--;
    __stk[__sp++] = lumin_make_string("b走default");
    gc_stw_check_fast();
    lumin_print(__stk[__sp-1]);
    __sp--;
    goto L417;
L417:;
    __stk[__sp++] = lumin_make_string("程序末尾");
    lumin_print(__stk[__sp-1]);
    __sp--;
    gc_pop_cframe();
    return 0;
}

