// lm_math.h —— 数学内置函数
#ifndef LM_MATH_H
#define LM_MATH_H

#include "lm_value.h"

Value lumyr_floor(Value x);  // floor(x)：向下取整，返回 int
Value lumyr_ceil(Value x);   // ceil(x)：向上取整，返回 int
Value lumyr_abs(Value x);    // abs(x)：绝对值（int/double）
Value lumyr_sqrt(Value x);   // sqrt(x)：平方根（double），负数报错
Value lumyr_max(Value* args, int n);  // max(a, b, ...)：变参最大值
Value lumyr_min(Value* args, int n);  // min(a, b, ...)：变参最小值

#endif //LM_MATH_H
