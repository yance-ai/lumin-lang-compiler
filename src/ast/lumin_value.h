#ifndef LUMIN_VALUE_H
#define LUMIN_VALUE_H

#include <stdio.h>
#include <stdlib.h>
#include "lumin_value_type.h"

// runtime错误抛出
void runtime_error(const char* msg);

// try/catch 全局错误状态：当前错误跳转点（NULL=无 try，直接退出）与错误消息
// 错误机制全部动态化（g_err_msg/g_err_type/g_trace/__g_* 按需扩容，无硬上限）
#include <setjmp.h>
extern _Thread_local jmp_buf* g_err_jmp;
extern _Thread_local char* g_err_msg;
extern _Thread_local char* g_err_type;
extern _Thread_local const char** g_trace;
extern _Thread_local int g_trace_n;
extern _Thread_local jmp_buf* __g_jbs;
extern _Thread_local jmp_buf** __g_prev;
extern _Thread_local int __g_depth;
extern _Thread_local int* __g_sp0;
extern _Thread_local int* __g_tgt;
extern _Thread_local int* __g_tn;
extern _Thread_local int* __g_fn;
extern _Thread_local int* __g_fin_act;
extern _Thread_local int* __g_fin_dep;
extern _Thread_local int* __g_fin_tgt;
extern _Thread_local int __g_fin_n;
extern _Thread_local Value __g_pend_val;

void __g_ensure(int need);
void g_trace_push(const char* nm);
void g_err_msg_set(const char* s);
void g_err_type_set(const char* s);

// ---------------- 值构造API ----------------
Value val_none(void);
Value val_int(long long v);
Value val_double(double v);
Value val_bool(_Bool v);
Value val_char(char v);
Value val_string(const char* s);
// ❗ 删除这一行：Value val_func(AstNode* func_ast);
Value val_array(int len);
Value val_map(void);

// ---------------- 内存管理 ----------------
void val_destroy(Value* v);
Value val_clone(const Value* src);

// ---------------- debug工具 ----------------
const char* val_typename(ValueType t);
void val_print(const Value* v);

// ---------------- 自增自减运算 ----------------
Value lumin_post_inc(Value* v);
Value lumin_pre_inc(Value* v);
Value lumin_post_dec(Value* v);
Value lumin_pre_dec(Value* v);

#endif
