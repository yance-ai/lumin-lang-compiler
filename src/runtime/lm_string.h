// lm_string.h —— 字符串操作内置函数
#ifndef LM_STRING_H
#define LM_STRING_H

#include "lm_value.h"

Value lumin_substr(Value s, Value start, Value n);   // substr(s, i, n)
Value lumin_toupper(Value s);                        // toupper(s)：ASCII 大写
Value lumin_tolower(Value s);                        // tolower(s)：ASCII 小写
Value lumin_split(Value s, Value sep);               // split(s, sep)
Value lumin_join(Value arr, Value sep);              // join(arr, sep)
Value lumin_contains(Value hay, Value needle);       // contains(s/arr/map, x)
Value lumin_repeat(Value s, Value n);                // repeat(s, n)
Value lumin_replace(Value s, Value from, Value to);  // replace(s, from, to)
Value lumin_format(Value* args, int n);              // format(fmt, args...)
Value lumin_strip(Value s);                          // strip(s)
Value lumin_startswith(Value s, Value prefix);       // startswith(s, prefix)
Value lumin_endswith(Value s, Value suffix);         // endswith(s, suffix)

#endif //LM_STRING_H
