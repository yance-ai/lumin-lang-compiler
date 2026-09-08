// lm_qs.c —— qs 内置函数（qs 库风格）：查询字符串解析/序列化
// stringify：map → "name=john&age=30"；嵌套 map → "user[name]=john"；
//            数组 → "tags[0]=a&tags[1]=b"；URL 编码特殊字符（%XX，UTF-8 安全）
// parse：    "user[name]=john&tags[0]=a" → {user:{name:"john"}, tags:["a"]}
// 注意：数组空段追加语法 tags[]= 暂不支持（须用显式索引 tags[0]=）
#include "lm_qs.h"
#include "lm_charset.h"
#include "gc_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ===== 简单动态字符串 =====
typedef struct { char* s; int len; int cap; } QSB;
static void qsb_init(QSB* b) { b->cap = 64; b->s = malloc(64); b->len = 0; b->s[0] = 0; }
static void qsb_grow(QSB* b, int need) {
    if(b->len + need + 1 > b->cap) { while(b->cap < b->len + need + 1) b->cap *= 2; b->s = realloc(b->s, b->cap); }
}
static void qsb_ch(QSB* b, char c) { qsb_grow(b, 1); b->s[b->len++] = c; b->s[b->len] = 0; }
static void qsb_str(QSB* b, const char* s) { int n = (int)strlen(s); qsb_grow(b, n); memcpy(b->s + b->len, s, n); b->len += n; b->s[b->len] = 0; }

// ===== URL 编码/解码（UTF-8 字节安全） =====
static void qs_encode(QSB* b, const char* s, int raw_high) {
    for(const unsigned char* p = (const unsigned char*)s; *p; p++) {
        if(isalnum(*p) || *p=='-' || *p=='_' || *p=='.' || *p=='~') qsb_ch(b, (char)*p);
        else if(raw_high && *p >= 0x80) qsb_ch(b, (char)*p);  // 中文等多字节字符原样输出
        else { char h[4]; snprintf(h, 4, "%%%02X", *p); qsb_str(b, h); }
    }
}
static void qs_decode(QSB* b, const char* s) {
    for(const char* p = s; *p; p++) {
        if(*p == '%' && p[1] && p[2]) { int v = 0; sscanf(p + 1, "%2x", &v); qsb_ch(b, (char)v); p += 2; }
        else if(*p == '+') qsb_ch(b, ' ');
        else qsb_ch(b, *p);
    }
}

// ===== stringify（递归） =====
static const char* qs_enc_str(Value enc) {
    if(enc.type == VAL_NONE || (enc.type == VAL_STRING && (!lumin_str_cstr(&enc) || !*lumin_str_cstr(&enc)))) return NULL;
    return enc.type == VAL_STRING ? lumin_str_cstr(&enc) : NULL;
}
static void qs_stringify_rec(QSB* b, const char* key, Value v, Value enc) {
    if(v.type == VAL_MAP) {
        MapIter it; map_iter_init(&it, v.v.map);
        Value k, vv;
        while(map_iter_next(&it, &k, &vv)) {
            char* kstr = value_to_str(k);
            char* sub;
            if(*key) { sub = malloc(strlen(key) + strlen(kstr) + 4); sprintf(sub, "%s[%s]", key, kstr); }
            else     { sub = malloc(strlen(kstr) + 2); sprintf(sub, "%s", kstr); }
            free(kstr);
            qs_stringify_rec(b, sub, vv, enc);
            free(sub);
        }
    } else if(v.type == VAL_ARRAY) {
        for(int i = 0; i < v.v.array->len; i++) {
            char* sub = malloc(strlen(key) + 32);
            sprintf(sub, "%s[%d]", key, i);
            qs_stringify_rec(b, sub, v.v.array->items[i], enc);
            free(sub);
        }
    } else {
        if(b->len) qsb_ch(b, '&');
        const char* e = qs_enc_str(enc);
        if(e) { char* kt = lumin_utf8_to_text(key, enc); qsb_str(b, kt ? kt : key); free(kt); }
        else qsb_str(b, key);
        qsb_ch(b, '=');
        char* sv = value_to_str(v);
        if(e) { char* vt = lumin_utf8_to_text(sv, enc); qs_encode(b, vt ? vt : sv, 0); free(vt); }
        else qs_encode(b, sv, 1);
        free(sv);
    }
}
char* lumin_qs_stringify_enc(Value v, Value enc) {
    QSB b; qsb_init(&b);
    qs_stringify_rec(&b, "", v, enc);
    return b.s;
}
char* lumin_qs_stringify(Value v) {
    return lumin_qs_stringify_enc(v, val_none());
}

// ===== parse =====
static _Bool seg_is_num(const char* s) {
    if(!*s) return 0;
    for(const char* p = s; *p; p++) if(!isdigit((unsigned char)*p)) return 0;
    return 1;
}
// 数组按索引写入（自动扩容；中间空槽补 null）
static void qs_arr_set_grow(Value* arr, int idx, Value v) {
    if(arr->type != VAL_ARRAY) { *arr = val_array(idx + 1); }
    else if(idx >= arr->v.array->len) {
        Value nv = val_array(idx + 1);
        /* val_clone：旧数组可能被 map_set 替换时 val_destroy，必须深拷贝 */
        for(int k = 0; k < arr->v.array->len; k++) {
            Value __cv = val_clone(&arr->v.array->items[k]);
            gc_write_barrier(__cv);
            nv.v.array->items[k] = __cv;
        }
        *arr = nv;
    }
    {
        Value __cv = val_clone(&v);
        gc_write_barrier(__cv);
        arr->v.array->items[idx] = __cv;
    }
}
static Value qs_child_get(Value container, const char* seg) {
    if(seg_is_num(seg)) {
        if(container.type == VAL_ARRAY) {
            int idx = atoi(seg);
            if(idx >= 0 && idx < container.v.array->len) return val_clone(&container.v.array->items[idx]);
        }
    } else if(container.type == VAL_MAP) {
        Value k = lumin_make_string((char*)seg);
        Value r = lumin_map_get(container, k);
        /* 容器深拷贝：qs_child_set 替换父键时 lumin_map_set 会 val_destroy 旧值，
           共享引用会 use-after-free */
        if(r.type == VAL_MAP || r.type == VAL_ARRAY) return val_clone(&r);
        return r;
    }
    return val_none();
}
static void qs_child_set(Value* container, const char* seg, Value child) {
    if(seg_is_num(seg)) qs_arr_set_grow(container, atoi(seg), child);
    else lumin_map_set(container, lumin_make_string((char*)seg), child);
}
static void qs_set_path(Value* container, char** segs, int i, int nseg, Value v) {
    if(i == nseg - 1) {  // 叶子
        if(seg_is_num(segs[i])) qs_arr_set_grow(container, atoi(segs[i]), v);
        else lumin_map_set(container, lumin_make_string(segs[i]), v);
        return;
    }
    const char* seg = segs[i];
    Value child = qs_child_get(*container, seg);
    if(child.type != VAL_MAP && child.type != VAL_ARRAY) {
        _Bool next_arr = seg_is_num(segs[i + 1]);
        child = next_arr ? val_array(0) : val_map();
    }
    qs_set_path(&child, segs, i + 1, nseg, v);
    qs_child_set(container, seg, child);
}
Value lumin_qs_parse_enc(const char* s, Value enc) {
    Value root = val_map();
    if(!s || !*s) return root;
    char* dup = strdup(s);
    char* save = NULL;
    for(char* pair = strtok_r(dup, "&", &save); pair; pair = strtok_r(NULL, "&", &save)) {
        char* eq = strchr(pair, '=');
        char* key = pair;
        char* val = "";
        if(eq) { *eq = 0; val = eq + 1; }
        // 拆段：base[seg1][seg2]
        char* segs[64]; int nseg = 0;
        char* p = strchr(key, '[');
        if(p) {
            *p = 0;
            segs[nseg++] = key;
            while(p) {
                char* q = strchr(p + 1, ']');
                if(!q) break;
                *q = 0;
                segs[nseg++] = p + 1;
                p = strchr(q + 1, '[');
            }
        } else {
            segs[nseg++] = key;
        }
        if(nseg == 0) { free(dup); return root; }
        // 每段 URL 解码（段指针指向 dup 副本，解码到新缓冲）
        char* dec_segs[64];
        for(int i = 0; i < nseg; i++) {
            QSB d; qsb_init(&d); qs_decode(&d, segs[i]);
            if(qs_enc_str(enc)) {
                char* u = lumin_text_to_utf8(d.s, d.len, enc);
                if(u) { free(d.s); d.s = u; d.len = (int)strlen(u); }
            }
            dec_segs[i] = d.s;
        }
        QSB vd; qsb_init(&vd); qs_decode(&vd, val);
        if(qs_enc_str(enc)) {
            char* u = lumin_text_to_utf8(vd.s, vd.len, enc);
            if(u) { free(vd.s); vd.s = u; vd.len = (int)strlen(u); }
        }
        Value v = lumin_make_string(vd.s);
        qs_set_path(&root, dec_segs, 0, nseg, v);
        for(int i = 0; i < nseg; i++) free(dec_segs[i]);
        free(vd.s);
    }
    free(dup);
    return root;
}

Value lumin_qs_parse(const char* s) {
    return lumin_qs_parse_enc(s, val_none());
}
