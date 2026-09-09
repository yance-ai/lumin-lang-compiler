// lm_math.c —— 数学内置函数
#include "lm_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>



static Value num_round(Value x, int up)
{
    if(x.type != VAL_INT && x.type != VAL_DOUBLE && x.type != VAL_CHAR)
        runtime_error("参数必须是数字");
    double d = value_as_number(x);
    double r = up ? ceil(d) : floor(d);
    if(r > 9.2e18 || r < -9.2e18)
        runtime_error("取整结果超出整数范围");
    return lumin_make_int((long long)r);
}

Value lumin_floor(Value x) { return num_round(x, 0); }

Value lumin_ceil(Value x)  { return num_round(x, 1); }

// abs：绝对值（保持原类型）

Value lumin_abs(Value x)
{
    if(x.type == VAL_INT) return lumin_make_int(x.v.i < 0 ? -x.v.i : x.v.i);
    if(x.type == VAL_DOUBLE) return lumin_make_double(fabs(x.v.d));
    if(x.type == VAL_CHAR) return lumin_make_char((char)(x.v.c < 0 ? -x.v.c : x.v.c));
    runtime_error("abs() 参数必须是数字");
    return val_none();
}

// sqrt：平方根（返回 double）

Value lumin_sqrt(Value x)
{
    if(x.type != VAL_INT && x.type != VAL_DOUBLE && x.type != VAL_CHAR)
        runtime_error("sqrt() 参数必须是数字");
    double d = value_as_number(x);
    if(d < 0) runtime_error("sqrt() 不能对负数开方");
    return lumin_make_double(sqrt(d));
}

// max/min：变参极值（复用比较语义：数字/字符串混合均可）

static Value extremum(Value* args, int n, int want_max)
{
    if(n < 1) runtime_error("需要至少 1 个参数");
    Value best = args[0];
    for(int i = 1; i < n; i++) {
        Value c = want_max ? lumin_gt(args[i], best) : lumin_lt(args[i], best);
        if(c.v.b) best = args[i];
    }
    return best;
}

Value lumin_max(Value* args, int n) { return extremum(args, n, 1); }

Value lumin_min(Value* args, int n) { return extremum(args, n, 0); }

// join：字符串数组按分隔符拼接（非字符串元素 value_to_str 转换）
