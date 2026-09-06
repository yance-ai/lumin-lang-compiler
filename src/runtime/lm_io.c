// lm_io.c —— 文件 IO 内置函数
#include "lm_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>



Value lumin_read_file(Value* args, int n) {
    if(n < 1 || args[0].type != VAL_STRING)
        runtime_error("read_file() 参数必须是文件路径字符串");
    const char* path = args[0].v.s;
    FILE* f = fopen(path, "rb");
    if(!f) {
        char buf[512];
        snprintf(buf, sizeof buf, "无法打开文件（读取）: %s", path);
        runtime_error(buf);
    }
    if(fseek(f, 0, SEEK_END) != 0) { fclose(f); runtime_error("无法定位文件末尾"); }
    long sz = ftell(f);
    if(sz < 0) { fclose(f); runtime_error("无法获取文件大小"); }
    rewind(f);
    char* buf = (char*)malloc((size_t)sz + 1);
    if(!buf) { fclose(f); perror("read_file"); exit(EXIT_FAILURE); }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);
    Value r = lumin_make_string(buf);
    free(buf);
    return r;
}

// write_file(path, content) → 覆盖写入（value_to_str 转字符串）；失败 → runtime_error

Value lumin_write_file(Value* args, int n) {
    if(n < 2 || args[0].type != VAL_STRING)
        runtime_error("write_file() 需要 (路径, 内容) 两个参数");
    const char* path = args[0].v.s;
    char* text = value_to_str(args[1]);
    FILE* f = fopen(path, "wb");
    if(!f) {
        char buf[512];
        snprintf(buf, sizeof buf, "无法打开文件（写入）: %s", path);
        free(text);
        runtime_error(buf);
    }
    size_t len = strlen(text);
    size_t wr = fwrite(text, 1, len, f);
    fclose(f);
    free(text);
    if(wr != len) runtime_error("写入文件不完整");
    return val_none();
}

// file_exists(path) → bool

Value lumin_file_exists(Value* args, int n) {
    if(n < 1 || args[0].type != VAL_STRING)
        runtime_error("file_exists() 参数必须是文件路径字符串");
    FILE* f = fopen(args[0].v.s, "rb");
    if(f) { fclose(f); return val_bool(1); }
    return val_bool(0);
}

// ---------------- 字典（VAL_MAP） ----------------
