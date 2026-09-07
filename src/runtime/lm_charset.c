// lm_charset.c —— 字符编码支持（iconv 封装 + bytes/str 双向）
#include "lm_charset.h"
#include <iconv.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* lumin_charset_norm(const char* enc) {
    if(!enc || !*enc) return "UTF-8";
    char buf[32]; int n = 0;
    for(const char* p = enc; *p && n < 31; p++) {
        if(*p == '-' || *p == '_' || *p == ' ') continue;
        buf[n++] = (*p >= 'a' && *p <= 'z') ? (char)(*p - 32) : *p;
    }
    buf[n] = 0;
    if(!strcmp(buf, "UTF8") || !strcmp(buf, "UTF") || !strcmp(buf, "U8")) return "UTF-8";
    if(!strcmp(buf, "GBK") || !strcmp(buf, "GB2312") || !strcmp(buf, "CP936") || !strcmp(buf, "MS936")) return "GBK";
    if(!strcmp(buf, "GB18030")) return "GB18030";
    if(!strcmp(buf, "BIG5") || !strcmp(buf, "BIG5HKSCS") || !strcmp(buf, "CP950")) return "BIG5";
    if(!strcmp(buf, "LATIN1") || !strcmp(buf, "ISO88591") || !strcmp(buf, "CP1252") || !strcmp(buf, "WINDOWS1252")) return "ISO-8859-1";
    if(!strcmp(buf, "ASCII") || !strcmp(buf, "USASCII") || !strcmp(buf, "ANSI")) return "ASCII";
    if(!strcmp(buf, "SHIFTJIS") || !strcmp(buf, "SJIS") || !strcmp(buf, "CP932")) return "SHIFT_JIS";
    if(!strcmp(buf, "EUCJP") || !strcmp(buf, "EUCJP")) return "EUC-JP";
    return NULL;
}

const char* lumin_charset_from_value(Value enc) {
    if(enc.type == VAL_NONE || (enc.type == VAL_STRING && (!enc.v.s || !*enc.v.s))) return "UTF-8";
    if(enc.type == VAL_STRING) {
        const char* r = lumin_charset_norm(enc.v.s);
        if(r) return r;
    }
    runtime_error("不支持的字符编码（支持 utf-8/gbk/gb18030/big5/latin1/ascii/shift_jis 等）");
    return "UTF-8";
}

char* lumin_charset_convert(const char* from, const char* to,
                            const char* in, size_t inlen, size_t* outlen) {
    iconv_t cd = iconv_open(to, from);
    if(cd == (iconv_t)-1) { *outlen = 0; return NULL; }
    size_t cap = inlen * 4 + 16;
    char* out = malloc(cap);
    if(!out) { iconv_close(cd); *outlen = 0; return NULL; }
    char* ip = (char*)in;
    char* op = out;
    size_t ileft = inlen;
    size_t oleft = cap;
    while(ileft > 0) {
        size_t r = iconv(cd, &ip, &ileft, &op, &oleft);
        if(r == (size_t)-1) {
            if(errno == E2BIG) {
                size_t used = cap - oleft;
                cap *= 2;
                out = realloc(out, cap);
                op = out + used;
                oleft = cap - used;
            } else {
                free(out);
                iconv_close(cd);
                *outlen = 0;
                return NULL;
            }
        }
    }
    /* 刷新内部状态（多字节序列尾） */
    iconv(cd, NULL, NULL, &op, &oleft);
    *outlen = cap - oleft;
    out[*outlen] = 0;
    iconv_close(cd);
    return out;
}

Value lumin_to_bytes(Value s, Value enc) {
    const char* e = lumin_charset_from_value(enc);
    if(s.type != VAL_STRING) runtime_error("bytes() 第一个参数必须是字符串");
    const char* in = s.v.s ? s.v.s : "";
    size_t inlen = strlen(in);
    char* buf;
    size_t len;
    if(!strcmp(e, "UTF-8")) { buf = strdup(in); len = inlen; }
    else {
        buf = lumin_charset_convert("UTF-8", e, in, inlen, &len);
        if(!buf) runtime_error("字符编码转换失败（iconv）");
    }
    if(len > 0x7FFFFFFF) { free(buf); runtime_error("字节数过大"); }
    Value arr = val_array((int)len);
    for(size_t i = 0; i < len; i++) arr.v.array.items[i] = lumin_make_byte((unsigned char)buf[i]);
    free(buf);
    return arr;
}

Value lumin_from_bytes(Value arr, Value enc) {
    const char* e = lumin_charset_from_value(enc);
    if(arr.type != VAL_ARRAY) runtime_error("str() 第一个参数必须是字节数组");
    int n = arr.v.array.len;
    char* tmp = malloc((size_t)n + 1);
    if(!tmp) runtime_error("内存不足");
    for(int i = 0; i < n; i++) tmp[i] = (char)lumin_extract_int(arr.v.array.items[i]);
    tmp[n] = 0;
    Value r;
    if(!strcmp(e, "UTF-8")) {
        r = lumin_make_string(tmp);
        free(tmp);
        return r;
    }
    size_t olen;
    char* out = lumin_charset_convert(e, "UTF-8", tmp, (size_t)n, &olen);
    free(tmp);
    if(!out) runtime_error("字符编码转换失败（iconv）");
    r = lumin_make_string(out);
    free(out);
    return r;
}

char* lumin_text_to_utf8(const char* s, size_t len, Value enc) {
    const char* e = lumin_charset_from_value(enc);
    if(!strcmp(e, "UTF-8")) { char* d = malloc(len + 1); memcpy(d, s, len); d[len] = 0; return d; }
    size_t olen;
    return lumin_charset_convert(e, "UTF-8", s, len, &olen);
}

char* lumin_utf8_to_text(const char* s, Value enc) {
    const char* e = lumin_charset_from_value(enc);
    if(!strcmp(e, "UTF-8")) return strdup(s);
    size_t olen;
    return lumin_charset_convert("UTF-8", e, s, strlen(s), &olen);
}
