// Created by kai on 2026/9/4.
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
Value lumin_len(Value v);           // 数组/字符串长度
Value lumin_index_get(Value c, Value idx);  // 数组元素 / 字符串字符

// ===== 内置函数 =====
Value lumin_type(Value v);          // type(x)：类型名字符串
Value lumin_input(void);            // input()：读一行（去换行）
Value lumin_range(Value n);         // range(n)：[0..n-1]
Value lumin_substr(Value s, Value start, Value n);  // substr(s, i, n)
Value lumin_toupper(Value s);  // toupper(s)：ASCII 大写
Value lumin_tolower(Value s);  // tolower(s)：ASCII 小写
Value lumin_split(Value s, Value sep);  // split(s, sep)：按分隔符拆数组
Value lumin_del(Value arr, Value idx);  // del(arr, idx)：返回删除后的新数组
Value lumin_insert(Value arr, Value idx, Value val);  // insert(arr, idx, val)：返回插入后的新数组

// ===== 强转新API =====
Value lumin_cast_int(Value v);
Value lumin_cast_double(Value v);
Value lumin_cast_bool(Value v);
Value lumin_cast_string(Value v);
Value lumin_cast_char(Value v);
Value lumin_cast_ascii(Value v);

int lumin_extract_int(Value v);

void lumin_print(Value v);



#endif //LM_VALUE_H
