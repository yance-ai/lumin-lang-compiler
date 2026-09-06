// lm_value.h —— 值系统核心（构造/算术/比较/下标/强转）+ 子模块头聚合
// 职责拆分：字符串 → lm_string.h、数组 → lm_array.h、数学 → lm_math.h、
//          文件 IO → lm_io.h、字典 → lm_map.h
#ifndef LM_VALUE_H
#define LM_VALUE_H

#include "ast/lumin_value_type.h"
#include "ast/lumin_value.h"

// 构造
Value lumin_make_int(long long i);
Value lumin_make_double(double d);
Value lumin_make_bool(_Bool b);
Value lumin_make_string(const char* s);
Value lumin_make_char(char ch);

// 算术
Value lumin_add(Value a, Value b);
Value lumin_sub(Value a, Value b);
Value lumin_mul(Value a, Value b);
Value lumin_div(Value a, Value b);
Value lumin_mod(Value a, Value b);
Value lumin_unary_plus(Value v);
Value lumin_unary_minus(Value v);

// 比较
Value lumin_gt(Value a, Value b);
Value lumin_lt(Value a, Value b);
Value lumin_ge(Value a, Value b);
Value lumin_le(Value a, Value b);
Value lumin_eq(Value a, Value b);
Value lumin_ne(Value a, Value b);

_Bool lumin_to_bool(Value v);
Value lumin_logic_not(Value v);

// ===== 数组 =====
Value lumin_array_get(Value arr, Value idx);
Value lumin_array_set(Value arr, Value idx, Value val);
Value lumin_len(Value v);           // 数组/字符串/字典长度
Value lumin_index_get(Value c, Value idx);  // 数组元素 / 字符串字符 / 字典键

// ===== 内置函数（核心） =====
Value lumin_type(Value v);          // type(x)：类型名字符串
Value lumin_input(void);            // input()：读一行（去换行）

// ===== 错误机制全局（定义在 lm_value.c；动态扩容，无硬上限） =====
extern _Thread_local char* g_err_type;
extern _Thread_local const char** g_trace;
extern _Thread_local int g_trace_n;
extern _Thread_local char* g_err_msg;

void __g_ensure(int need);
void g_trace_push(const char* nm);
void g_err_msg_set(const char* s);
void g_err_type_set(const char* s);

// ===== 错误对象 =====
Value lumin_make_error(const char* type, const char* msg, const char* stack);
char* lumin_build_stack_trace(void);   // malloc，调用方 free

// ===== 强转 =====
Value lumin_cast_int(Value v);
Value lumin_cast_double(Value v);
Value lumin_cast_bool(Value v);
Value lumin_cast_string(Value v);
Value lumin_cast_char(Value v);
Value lumin_cast_ascii(Value v);

int lumin_extract_int(Value v);

void lumin_print(Value v);

// ===== 跨文件共享辅助（拆分后子模块依赖） =====
double value_as_number(Value x);    // 值转数值（数值/布尔/字符）
char*  value_to_str(Value v);       // 值转字符串（malloc，调用方 free）
long long array_index_of(Value idx); // 下标值转 long long
long long range_to_ll(Value v);       // range 参数转 long long

// ===== 子模块头（职责拆分） =====
#include "lm_string.h"
#include "lm_array.h"
#include "lm_math.h"
#include "lm_io.h"
#include "lm_map.h"

#endif //LM_VALUE_H
