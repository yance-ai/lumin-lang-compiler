// lm_string.h —— 字符串操作内置函数
#ifndef LM_STRING_H
#define LM_STRING_H

#include "lm_value.h"

Value lumyr_substr(Value s, Value start, Value n);   // substr(s, i, n)
Value lumyr_toupper(Value s);                        // toupper(s)：ASCII 大写
Value lumyr_tolower(Value s);                        // tolower(s)：ASCII 小写
Value lumyr_split(Value s, Value sep);               // split(s, sep)
Value lumyr_join(Value arr, Value sep);              // join(arr, sep)
Value lumyr_contains(Value hay, Value needle);       // contains(s/arr/map, x)
Value lumyr_repeat(Value s, Value n);                // repeat(s, n)
Value lumyr_replace(Value s, Value from, Value to);  // replace(s, from, to)
Value lumyr_format(Value* args, int n);              // format(fmt, args...)
Value lumyr_strip(Value s);                          // strip(s)
Value lumyr_startswith(Value s, Value prefix);       // startswith(s, prefix)
Value lumyr_endswith(Value s, Value suffix);         // endswith(s, suffix)

#endif //LM_STRING_H
