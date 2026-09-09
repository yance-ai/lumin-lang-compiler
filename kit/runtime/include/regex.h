/*
 * POSIX regex.h 兼容层（Windows）
 * 基于 TRE 正则库（纯 C 实现），提供 regcomp/regexec/regfree/regerror
 */
#ifndef _COMPAT_REGEX_H
#define _COMPAT_REGEX_H

#include <tre/tre.h>

/* TRE 使用 tre_ 前缀，这里映射为标准 POSIX 名称 */
#define regcomp    tre_regcomp
#define regexec    tre_regexec
#define regfree    tre_regfree
#define regerror   tre_regerror

#endif /* _COMPAT_REGEX_H */
