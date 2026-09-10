#ifndef LM_JSON_H
#define LM_JSON_H

// json / stringify 内置函数：JSON 解析与序列化
// json(s)      → 解析 JSON 文本为 lm 值（map/array/string/int/double/bool/none）
// stringify(v) → lm 值序列化为 JSON 文本（malloc，调用方 free）
Value lumyr_json_parse_enc(const char* s, Value enc);
Value lumyr_json_parse(const char* s);
char* lumyr_json_stringify_enc(Value v, Value enc);
char* lumyr_json_stringify(Value v);

#endif //LM_JSON_H
