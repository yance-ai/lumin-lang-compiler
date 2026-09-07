#ifndef LM_QS_H
#define LM_QS_H

// qs 内置函数（qs 库风格）：查询字符串解析/序列化
// qs(map/array)    → 序列化为查询字符串（嵌套 user[name]=john、数组 tags[0]=a）
// qs(string)       → 解析查询字符串为嵌套 map/数组
#include "lm_value.h"

char* lumin_qs_stringify_enc(Value v, Value enc);  // enc：UTF-8/GBK/...（空=默认 UTF-8）；malloc，调用方 free
char* lumin_qs_stringify(Value v);
Value lumin_qs_parse_enc(const char* s, Value enc);
Value lumin_qs_parse(const char* s);

#endif //LM_QS_H
