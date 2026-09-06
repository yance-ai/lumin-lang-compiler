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
Value lumin_floor(Value x);  // floor(x)：向下取整，返回 int
Value lumin_ceil(Value x);   // ceil(x)：向上取整，返回 int
Value lumin_abs(Value x);    // abs(x)：绝对值（int/double）
Value lumin_sqrt(Value x);   // sqrt(x)：平方根（double），负数报错
Value lumin_max(Value* args, int n);  // max(a, b, ...)：变参最大值（≥1）
Value lumin_min(Value* args, int n);  // min(a, b, ...)：变参最小值（≥1）
Value lumin_join(Value arr, Value sep);  // join(arr, sep)：字符串数组拼接（非字符串元素自动转字符串）
Value lumin_contains(Value hay, Value needle);  // contains(s/arr/map, x)：字符串子串 / 数组元素 / 字典键包含

// 字典（VAL_MAP）
void lumin_map_set(Value* map, Value key, Value val);    // d["k"] = v（原地，传指针）
Value lumin_map_get(Value map, Value key);              // d["k"]；缺键 → null
int   lumin_map_has(Value map, const char* key);        // 键是否存在
Value lumin_map_del(Value map, const char* key);        // 删键，返回新字典
Value lumin_map_keys(Value map);                        // keys(d) → 字符串数组
Value lumin_map_values(Value map);                      // values(d) → 值数组
Value lumin_map_lit(Value* kv, int n);                  // OPC_MAP_LIT：键值交替构造
Value lumin_repeat(Value s, Value n);  // repeat(s, n)：字符串重复 n 次
Value lumin_replace(Value s, Value from, Value to);  // replace(s, from, to)：替换所有出现
Value lumin_sum(Value arr);  // sum(arr)：数字数组求和（全 int 返回 int，否则 double）
Value lumin_range_n(Value* args, int n);  // range(n) / range(a,b) / range(a,b,step)
Value lumin_format(Value* args, int n);   // format(fmt, args...)：{} 占位替换
Value lumin_sort(Value arr);   // sort(arr)：升序（全数字或全字符串），返回新数组
Value lumin_reverse(Value arr);  // reverse(arr)：反转，返回新数组
Value lumin_strip(Value s);  // strip(s)：去首尾空白
Value lumin_startswith(Value s, Value prefix);  // startswith(s, prefix)
Value lumin_endswith(Value s, Value suffix);    // endswith(s, suffix)
Value lumin_avg(Value arr);  // avg(arr)：数字数组平均值（double），空数组报错

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
