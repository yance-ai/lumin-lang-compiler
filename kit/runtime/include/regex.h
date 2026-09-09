/*
 * POSIX regex.h 兼容层
 * - Windows：基于 TRE 正则库（纯 C 实现），提供 regcomp/regexec/regfree/regerror
 * - macOS/Linux：直接使用系统自带的 POSIX regex.h
 */
#ifndef _COMPAT_REGEX_H
#define _COMPAT_REGEX_H

#if defined(_WIN32) || defined(__MINGW32__) || defined(WIN32)
#include <tre/tre.h>
/* TRE 使用 tre_ 前缀，这里映射为标准 POSIX 名称 */
#define regcomp    tre_regcomp
#define regexec    tre_regexec
#define regfree    tre_regfree
#define regerror   tre_regerror
#else
/* 非 Windows：跳过本兼容层，直接包含系统 POSIX regex.h */
#include_next <regex.h>
#endif

#endif /* _COMPAT_REGEX_H */
