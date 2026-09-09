// lm_regex.c —— 正则表达式（POSIX regex.h 封装）
#include "lm_regex.h"
#ifdef _WIN32
#include "regex.h"
#else
#include <regex.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_GROUPS 32

// PCRE 风格转义 → POSIX ERE 等价物
static char* pcre_to_posix(const char* pat) {
    size_t cap = strlen(pat) * 3 + 16;
    char* out = malloc(cap);
    if(!out) return strdup(pat);
    char* p = out;
    for(const char* q = pat; *q; q++) {
        if(*q == '\\' && q[1]) {
            switch(q[1]) {
                case 's': memcpy(p, "[[:space:]]", 11); p += 11; q++; continue;
                case 'S': memcpy(p, "[^[:space:]]", 12); p += 12; q++; continue;
                case 'd': memcpy(p, "[0-9]", 5); p += 5; q++; continue;
                case 'D': memcpy(p, "[^0-9]", 6); p += 6; q++; continue;
                case 'w': memcpy(p, "[a-zA-Z0-9_]", 13); p += 13; q++; continue;
                case 'W': memcpy(p, "[^a-zA-Z0-9_]", 14); p += 14; q++; continue;
                default: *p++ = *q; break;
            }
        } else {
            *p++ = *q;
        }
    }
    *p = 0;
    return out;
}

static int regex_compile(regex_t* re, const char* pattern) {
    char* conv = pcre_to_posix(pattern);
    int rc = regcomp(re, conv, REG_EXTENDED);
    free(conv);
    if(rc != 0) {
        char errbuf[256];
        regerror(rc, re, errbuf, sizeof(errbuf));
        runtime_error(errbuf);
        return -1;
    }
    return 0;
}

_Bool lumin_regex_match(const char* s, const char* pattern) {
    if(!s || !pattern) return 0;
    regex_t re;
    if(regex_compile(&re, pattern) != 0) return 0;
    regmatch_t m[1];
    int rc = regexec(&re, s, 1, m, 0);
    regfree(&re);
    // 完整匹配：匹配从 0 开始且到字符串末尾
    if(rc == 0 && m[0].rm_so == 0 && (size_t)m[0].rm_eo == strlen(s)) return 1;
    return 0;
}

Value lumin_regex_search(const char* s, const char* pattern) {
    Value arr = val_array(0);
    if(!s || !pattern) return arr;
    regex_t re;
    if(regex_compile(&re, pattern) != 0) return arr;
    regmatch_t m[MAX_GROUPS];
    int rc = regexec(&re, s, MAX_GROUPS, m, 0);
    if(rc == 0) {
        for(int i = 0; i < MAX_GROUPS; i++) {
            if(m[i].rm_so == -1) break;
            int len = m[i].rm_eo - m[i].rm_so;
            char* sub = malloc(len + 1);
            memcpy(sub, s + m[i].rm_so, len);
            sub[len] = 0;
            lumin_array_add(&arr, lumin_make_string(sub));
            free(sub);
        }
    }
    regfree(&re);
    return arr;
}

char* lumin_regex_replace(const char* s, const char* pattern, const char* repl) {
    if(!s || !pattern || !repl) return strdup(s ? s : "");
    regex_t re;
    if(regex_compile(&re, pattern) != 0) return strdup(s);
    size_t cap = strlen(s) * 2 + 64;
    char* out = malloc(cap);
    if(!out) { regfree(&re); return NULL; }
    out[0] = 0;
    size_t outlen = 0;
    const char* p = s;
    regmatch_t m[MAX_GROUPS];
    while(*p) {
        int rc = regexec(&re, p, MAX_GROUPS, m, 0);
        if(rc != 0) {
            // 无匹配，追加剩余
            size_t rem = strlen(p);
            if(outlen + rem + 1 > cap) { cap = (outlen + rem + 1) * 2; out = realloc(out, cap); }
            memcpy(out + outlen, p, rem);
            outlen += rem;
            out[outlen] = 0;
            break;
        }
        // 追加匹配前的文本
        size_t pre = m[0].rm_so;
        if(outlen + pre + 1 > cap) { cap = (outlen + pre + 1) * 2; out = realloc(out, cap); }
        memcpy(out + outlen, p, pre);
        outlen += pre;
        out[outlen] = 0;
        // 处理替换串中的 \1 \2 反向引用
        for(const char* r = repl; *r; r++) {
            if(*r == '\\' && r[1] >= '0' && r[1] <= '9') {
                int gi = r[1] - '0';
                if(gi < MAX_GROUPS && m[gi].rm_so != -1) {
                    int glen = m[gi].rm_eo - m[gi].rm_so;
                    if(outlen + glen + 1 > cap) { cap = (outlen + glen + 1) * 2; out = realloc(out, cap); }
                    memcpy(out + outlen, p + m[gi].rm_so, glen);
                    outlen += glen;
                    out[outlen] = 0;
                }
                r++;
            } else {
                if(outlen + 2 > cap) { cap *= 2; out = realloc(out, cap); }
                out[outlen++] = *r;
                out[outlen] = 0;
            }
        }
        // 跳过匹配
        p += m[0].rm_eo;
        if(m[0].rm_eo == 0) { // 空匹配，避免死循环
            if(outlen + 2 > cap) { cap *= 2; out = realloc(out, cap); }
            out[outlen++] = *p;
            out[outlen] = 0;
            if(*p) p++;
            else break;
        }
    }
    regfree(&re);
    return out;
}
