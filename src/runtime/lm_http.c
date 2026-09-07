// lm_http.c —— HTTP 客户端（libcurl 实现；requests.get/post/... 内置的运行时支撑）
#include "runtime/lm_http.h"
#include "runtime/lm_map.h"
#include <curl/curl.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>

// 动态字节缓冲（响应体/查询串拼接；无硬上限，按需翻倍）
typedef struct {
    char* data;
    size_t len;
    size_t cap;
} Buf;

static void buf_append(Buf* b, const char* s, size_t n)
{
    if(b->len + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 4096;
        while(nc < b->len + n + 1) nc *= 2;
        char* nd = (char*)realloc(b->data, nc);
        if(!nd) { free(b->data); b->data = NULL; b->len = 0; b->cap = 0; runtime_error("http: 响应缓冲内存不足"); return; }
        b->data = nd;
        b->cap = nc;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

// 响应体收集回调
static size_t body_cb(char* ptr, size_t size, size_t nmemb, void* ud)
{
    Buf* b = (Buf*)ud;
    buf_append(b, ptr, size * nmemb);
    return size * nmemb;
}

// 响应头收集回调：解析 "Name: value\r\n"，键值进响应头 map（重复键后者覆盖）
static size_t hdr_cb(char* ptr, size_t size, size_t nmemb, void* ud)
{
    Value* hm = (Value*)ud;
    size_t n = size * nmemb;
    char* line = (char*)malloc(n + 1);
    if(!line) return 0;
    memcpy(line, ptr, n);
    line[n] = '\0';
    char* colon = strchr(line, ':');
    if(colon) {
        *colon = '\0';
        char* k = line;
        char* v = colon + 1;
        while(*k == ' ' || *k == '\t') k++;
        char* ke = k + strlen(k);
        while(ke > k && (ke[-1] == ' ' || ke[-1] == '\t' || ke[-1] == '\r' || ke[-1] == '\n')) *--ke = '\0';
        while(*v == ' ' || *v == '\t') v++;
        char* ve = v + strlen(v);
        while(ve > v && (ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '\r' || ve[-1] == '\n')) *--ve = '\0';
        if(k[0] && v[0])
            lumin_map_set(hm, lumin_make_string(k), lumin_make_string(v));
    }
    free(line);
    return n;
}

static pthread_once_t g_curl_once = PTHREAD_ONCE_INIT;
static void curl_global_init_once(void) { curl_global_init(CURL_GLOBAL_DEFAULT); }

Value lumin_http_request(const char* method, Value url, Value params, Value config)
{
    pthread_once(&g_curl_once, curl_global_init_once);

    if(url.type != VAL_STRING)
        runtime_error("requests: url 必须是字符串");
    if(params.type != VAL_NONE && params.type != VAL_STRING && params.type != VAL_MAP)
        runtime_error("requests: 参数 params 必须是字符串查询串或字典");
    if(config.type != VAL_NONE && config.type != VAL_MAP)
        runtime_error("requests: 配置 config 必须是字典");

    CURL* h = curl_easy_init();
    if(!h) runtime_error("requests: curl 初始化失败");
    struct curl_slist* hdrs = NULL;

    // 1. URL + 查询串
    Buf full_url = {0};
    buf_append(&full_url, url.v.s, strlen(url.v.s));
    Buf qs = {0};
    if(params.type == VAL_MAP) {
        for(int i = 0; i < params.v.map->len; i++) {
            if(i > 0) buf_append(&qs, "&", 1);
            char* pkstr = value_to_str(params.v.map->keys[i]);
            char* ek = curl_easy_escape(h, pkstr, 0);
            free(pkstr);
            char* sv = value_to_str(params.v.map->values[i]);
            char* ev = curl_easy_escape(h, sv, 0);
            free(sv);
            buf_append(&qs, ek, strlen(ek));
            buf_append(&qs, "=", 1);
            buf_append(&qs, ev, strlen(ev));
            curl_free(ek);
            curl_free(ev);
        }
        if(qs.len > 0) {
            if(strchr(full_url.data, '?') == NULL) buf_append(&full_url, "?", 1);
            else buf_append(&full_url, "&", 1);
            buf_append(&full_url, qs.data, qs.len);
        }
    } else if(params.type == VAL_STRING && params.v.s && params.v.s[0]) {
        if(strchr(full_url.data, '?') == NULL) buf_append(&full_url, "?", 1);
        else buf_append(&full_url, "&", 1);
        buf_append(&full_url, params.v.s, strlen(params.v.s));
    }
    free(qs.data);

    // 2. config：请求头 / 请求体 / 超时
    long timeout_s = 30;
    Value body = val_none();
    if(config.type == VAL_MAP) {
        if(lumin_map_has(config, lumin_make_string("headers"))) {
            Value hv = lumin_map_get(config, lumin_make_string("headers"));
            if(hv.type != VAL_MAP) { curl_easy_cleanup(h); free(full_url.data); runtime_error("requests: config.headers 必须是字典"); }
            for(int i = 0; i < hv.v.map->len; i++) {
                char* sv = value_to_str(hv.v.map->values[i]);
                char* hkstr = value_to_str(hv.v.map->keys[i]);
                size_t klen = strlen(hkstr), vlen = strlen(sv);
                char* entry = (char*)malloc(klen + vlen + 3);
                memcpy(entry, hkstr, klen);
                free(hkstr);
                entry[klen] = ':'; entry[klen + 1] = ' ';
                memcpy(entry + klen + 2, sv, vlen + 1);
                hdrs = curl_slist_append(hdrs, entry);
                free(entry);
                free(sv);
            }
        }
        if(lumin_map_has(config, lumin_make_string("body"))) {
            body = lumin_map_get(config, lumin_make_string("body"));
            if(body.type != VAL_STRING) { curl_easy_cleanup(h); free(full_url.data); if(hdrs) curl_slist_free_all(hdrs); runtime_error("requests: config.body 必须是字符串"); }
        }
        if(lumin_map_has(config, lumin_make_string("timeout"))) {
            Value tv = lumin_map_get(config, lumin_make_string("timeout"));
            if(tv.type == VAL_INT) timeout_s = tv.v.i;
        }
    }

    // 3. 请求体
    if(body.type == VAL_STRING) {
        curl_easy_setopt(h, CURLOPT_POSTFIELDS, body.v.s);
        curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)strlen(body.v.s));
    }

    // 4. 执行
    Buf resp_body = {0};
    Value resp_headers = val_map();
    curl_easy_setopt(h, CURLOPT_URL, full_url.data);
    curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, timeout_s);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, body_cb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &resp_body);
    curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, hdr_cb);
    curl_easy_setopt(h, CURLOPT_HEADERDATA, &resp_headers);
    if(strcmp(method, "HEAD") == 0) curl_easy_setopt(h, CURLOPT_NOBODY, 1L);

    CURLcode rc = curl_easy_perform(h);
    long code = 0;
    if(rc == CURLE_OK) curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);

    if(hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(h);
    free(full_url.data);

    if(rc != CURLE_OK) {
        free(resp_body.data);
        char msg[256];
        snprintf(msg, sizeof(msg), "requests.%s: %s", method, curl_easy_strerror(rc));
        runtime_error(msg);
    }

    Value r = val_map();
    lumin_map_set(&r, lumin_make_string("status"), lumin_make_int(code));
    lumin_map_set(&r, lumin_make_string("body"), lumin_make_string(resp_body.data ? resp_body.data : ""));
    lumin_map_set(&r, lumin_make_string("headers"), resp_headers);
    free(resp_body.data);
    return r;
}
