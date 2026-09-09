// lm_regex.h —— 正则表达式（POSIX regex 封装）
#ifndef LM_REGEX_H
#define LM_REGEX_H

#include "lm_value.h"

// regex_match(s, pattern)：完整匹配，返回 bool
_Bool lumin_regex_match(const char* s, const char* pattern);

// regex_search(s, pattern)：搜索第一个匹配，返回数组 [match, group1, group2, ...]；无匹配返回空数组
Value lumin_regex_search(const char* s, const char* pattern);

// regex_replace(s, pattern, repl)：替换所有匹配，repl 支持 \1 \2 反向引用；返回新字符串
char* lumin_regex_replace(const char* s, const char* pattern, const char* repl);

#endif // LM_REGEX_H
