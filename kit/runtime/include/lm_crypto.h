// lm_crypto.h —— 编码/哈希：URL、Base64、MD5
#ifndef LM_CRYPTO_H
#define LM_CRYPTO_H

#include "lm_value.h"

// URL 编码（%XX，空格→%20；高字节原样）
char* lumyr_url_encode(const char* s);
// URL 解码（%XX → 字节，+ → 空格）
char* lumyr_url_decode(const char* s);

// Base64 编码（字符串 → base64 文本，malloc）
char* lumyr_base64_encode(const char* s, int len);
// Base64 解码（base64 文本 → 原字符串，malloc；返回长度通过 outlen）
char* lumyr_base64_decode(const char* s, int* outlen);

// MD5（字符串 → 32 位十六进制小写，malloc）
char* lumyr_md5_hex(const char* s, int len);

#endif // LM_CRYPTO_H
