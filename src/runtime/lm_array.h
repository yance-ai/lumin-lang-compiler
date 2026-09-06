// lm_array.h —— 数组操作内置函数
#ifndef LM_ARRAY_H
#define LM_ARRAY_H

#include "lm_value.h"

Value lumin_range_n(Value* args, int n);  // range(n) / range(a,b) / range(a,b,step)
Value lumin_del(Value arr, Value idx);    // del(arr, idx)：返回删除后的新数组/字典
Value lumin_insert(Value arr, Value idx, Value val);  // insert(arr, idx, val)
Value lumin_sum(Value arr);               // sum(arr)：数字数组求和
Value lumin_avg(Value arr);               // avg(arr)：平均值
Value lumin_sort(Value arr);              // sort(arr)：升序，返回新数组
Value lumin_reverse(Value arr);           // reverse(arr)：反转，返回新数组

#endif //LM_ARRAY_H

Value lumin_array_add(Value arr, Value val);  // 追加元素，返回新数组
