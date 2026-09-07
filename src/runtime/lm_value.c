#include "lm_value.h"
#include "lm_json.h"
#include "gc_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <ctype.h>

// 字典辅助（VAL_MAP）前向声明：lumin_eq 等在定义之前引用
Value lumin_make_int(long long i) {
    Value v;
    v.type = VAL_INT;
    v.v.i = i;
    return v;
}

Value lumin_make_double(double d) {
    Value v;
    v.type = VAL_DOUBLE;
    v.v.d = d;
    return v;
}

Value lumin_make_bool(_Bool b) {
    Value v;
    v.type = VAL_BOOL;
    v.v.b = b;
    return v;
}

Value lumin_make_string(const char* s) {
    Value v;
    v.type = VAL_STRING;
    if(s == NULL)
    {
        v.v.s = NULL;
        return v;
    }
    size_t len = strlen(s);
    v.v.s = (char*)gc_alloc(len + 1, VAL_STRING);
    memcpy(v.v.s, s, len);
    v.v.s[len] = '\0';
    return v;
}

Value lumin_make_char(char ch) {
    Value v;
    v.type = VAL_CHAR;
    v.v.c = ch;
    return v;
}

Value lumin_make_byte(unsigned char b) {
    Value v;
    v.type = VAL_BYTE;
    v.v.i = (long long)(b & 0xFF);
    return v;
}

// 获取value的数值，int转double
double value_as_number(Value x) {
    if(x.type == VAL_INT)
        return (double)x.v.i;
    else if(x.type == VAL_DOUBLE)
        return x.v.d;
    else if(x.type == VAL_CHAR)
        return (double)x.v.c;
    else if(x.type == VAL_BYTE)
        return (double)(x.v.i & 0xFF);
    return 0.0;
}

// 判断是否是字符串类型
static int is_string(Value a, Value b) {
    return (a.type == VAL_STRING) || (b.type == VAL_STRING);
}

// 把一个Value转堆字符串（用于字符串拼接、弱比较）
char* value_to_str(Value v) {
    char buf[256];
    switch(v.type)
    {
        case VAL_INT:
            snprintf(buf, sizeof(buf), "%lld", v.v.i);
            break;
        case VAL_DOUBLE:
            snprintf(buf, sizeof(buf), "%g", v.v.d);
            break;
        case VAL_BOOL:
            strcpy(buf, v.v.b ? "true" : "false");
            break;
        case VAL_STRING:
        {
            size_t l = strlen(v.v.s);
            char* p = (char*)malloc(l+1);
            memcpy(p, v.v.s, l+1);
            return p;
        }
        case VAL_ERROR:
            return strdup(v.v.err.message ? v.v.err.message : "");
        case VAL_BYTE:
            snprintf(buf, sizeof(buf), "%lld", v.v.i & 0xFF);
            break;
        case VAL_CHAR:
        {
            char buf2[2];
            buf2[0] = v.v.c;
            buf2[1] = '\0';
            size_t n = strlen(buf2);
            char* res = (char*)malloc(n+1);
            memcpy(res, buf2, n+1);
            return res;
        }
        case VAL_MAP:
            return lumin_json_stringify(v);
        default:
            strcpy(buf, "");
            break;
    }
    size_t n = strlen(buf);
    char* res = (char*)malloc(n+1);
    memcpy(res, buf, n+1);
    return res;
}

Value lumin_unary_plus(Value v) {
    return v;
}

Value lumin_unary_minus(Value v) {
    if(v.type == VAL_INT) {
        if(v.v.i == LLONG_MIN) return lumin_make_double(-(double)v.v.i);  // 溢出保护
        return lumin_make_int(-v.v.i);
    }
    double num = value_as_number(v);
    return lumin_make_double(-num);
}

Value lumin_add(Value a, Value b) {
    // 与解释器 ast_interp.c 语义对齐：
    // 1) 任一操作数为 string 或 bool → 字符串拼接（bool 转 "true"/"false"）
    // 2) int+int → int
    // 3) 其它（含 char 参与）→ double（char 按数值提升）
    if(is_string(a,b) || a.type == VAL_BOOL || b.type == VAL_BOOL)
    {
        char* sa = value_to_str(a);
        char* sb = value_to_str(b);
        size_t la = strlen(sa);
        size_t lb = strlen(sb);
        char* out = (char*)gc_alloc(la + lb + 1, VAL_STRING);
        memcpy(out, sa, la);
        memcpy(out+la, sb, lb);
        out[la+lb] = '\0';
        free(sa);
        free(sb);
        Value res;
        res.type = VAL_STRING;
        res.v.s = out;
        return res;
    }
    if(a.type == VAL_INT && b.type == VAL_INT)
    {
        return lumin_make_int(a.v.i + b.v.i);
    }
    // 算术加法
    double na = value_as_number(a);
    double nb = value_as_number(b);
    return lumin_make_double(na + nb);
}

Value lumin_sub(Value a, Value b) {
    if(a.type == VAL_INT && b.type == VAL_INT)
    {
        return lumin_make_int(a.v.i - b.v.i);
    }
    double na = value_as_number(a);
    double nb = value_as_number(b);
    return lumin_make_double(na - nb);
}

Value lumin_mul(Value a, Value b) {
    if(a.type == VAL_INT && b.type == VAL_INT)
    {
        return lumin_make_int(a.v.i * b.v.i);
    }
    double na = value_as_number(a);
    double nb = value_as_number(b);
    return lumin_make_double(na * nb);
}

Value lumin_div(Value a, Value b) {
    double na = value_as_number(a);
    double nb = value_as_number(b);
    return lumin_make_double(na / nb);
}

// % 取模：int%int → int（C 语义，负数与 C 一致）；任一 double → fmod
Value lumin_mod(Value a, Value b) {
    if(a.type == VAL_INT && b.type == VAL_INT) {
        if(b.v.i == 0) return lumin_make_double(0.0 / 0.0);  // 除零得 NaN，避免 UB
        return lumin_make_int(a.v.i % b.v.i);
    }
    double na = value_as_number(a);
    double nb = value_as_number(b);
    return lumin_make_double(fmod(na, nb));
}

// ! 逻辑非：返回 bool
Value lumin_logic_not(Value v) {
    return lumin_make_bool(!lumin_to_bool(v));
}

// ---------------- 数组 ----------------

// 下标必须是数值；越界运行时错误
long long array_index_of(Value idx) {
    if(idx.type == VAL_INT) return idx.v.i;
    if(idx.type == VAL_DOUBLE) return (long long)idx.v.d;
    if(idx.type == VAL_CHAR) return (unsigned char)idx.v.c;
    runtime_error("数组下标必须是数值");
    return 0;
}

Value lumin_array_get(Value arr, Value idx) {
    if(arr.type != VAL_ARRAY) runtime_error("下标访问的对象不是数组");
    long long i = array_index_of(idx);
    if(i < 0 || i >= arr.v.array->len) {
        char buf[128];
        snprintf(buf, sizeof(buf), "数组下标越界: %lld (长度 %d)", i, arr.v.array->len);
        runtime_error(buf);
    }
    return arr.v.array->items[i];   // 返回数组持有值的引用（调用方如需长期持有需 clone）
}

// len(x)：数组长度 / 字符串字符数
Value lumin_len(Value v) {
    if(v.type == VAL_ARRAY) return lumin_make_int(v.v.array->len);
    if(v.type == VAL_STRING) return lumin_make_int((long long)strlen(v.v.s));
    if(v.type == VAL_MAP) return lumin_make_int(v.v.map->len);
    runtime_error("len() 参数必须是数组、字符串或字典");
    return lumin_make_int(0);
}

// 下标读：数组元素 / 字符串字符（返回 char） / 字典键
Value lumin_index_get(Value c, Value idx) {
    if(c.type == VAL_MAP) {
        return lumin_map_get(c, idx);
    }
    if(c.type == VAL_ERROR) {
        if(idx.type != VAL_STRING) runtime_error("错误对象下标必须是字符串键");
        if(strcmp(idx.v.s, "type") == 0) return lumin_make_string(c.v.err.type ? c.v.err.type : "");
        if(strcmp(idx.v.s, "message") == 0) return lumin_make_string(c.v.err.message ? c.v.err.message : "");
        if(strcmp(idx.v.s, "stack") == 0) return lumin_make_string(c.v.err.stack ? c.v.err.stack : "");
        runtime_error("错误对象只有 type/message/stack 三个字段");
        return val_none();
    }
    long long i = array_index_of(idx);
    if(c.type == VAL_ARRAY) {
        if(i < 0 || i >= c.v.array->len) {
            char buf[128];
            snprintf(buf, sizeof(buf), "数组下标越界: %lld (长度 %d)", i, c.v.array->len);
            runtime_error(buf);
        }
        return c.v.array->items[i];
    }
    if(c.type == VAL_STRING) {
        long long n = (long long)strlen(c.v.s);
        if(i < 0 || i >= n) {
            char buf[128];
            snprintf(buf, sizeof(buf), "字符串下标越界: %lld (长度 %lld)", i, n);
            runtime_error(buf);
        }
        return lumin_make_char(c.v.s[i]);
    }
    runtime_error("下标访问的对象不是数组、字符串或字典");
    return val_none();
}

// ---------------- 内置函数 ----------------

Value lumin_type(Value v) {
    switch(v.type) {
        case VAL_NONE:   return lumin_make_string("none");
        case VAL_INT:    return lumin_make_string("int");
        case VAL_DOUBLE: return lumin_make_string("double");
        case VAL_BOOL:   return lumin_make_string("bool");
        case VAL_CHAR:   return lumin_make_string("char");
        case VAL_BYTE:   return lumin_make_string("byte");
        case VAL_STRING: return lumin_make_string("string");
        case VAL_FUNC:   return lumin_make_string("func");
        case VAL_ARRAY:  return lumin_make_string("array");
        case VAL_MAP:    return lumin_make_string("map");
        case VAL_ERROR:  return lumin_make_string("error");
    }
    return lumin_make_string("unknown");
}

Value lumin_input(void) {
    /* 动态读取整行：初始 64 字节，按需翻倍，无长度上限 */
    size_t cap = 64, n = 0;
    char* buf = (char*)malloc(cap);
    if(!buf) { fprintf(stderr, "input: 内存不足\n"); exit(EXIT_FAILURE); }
    for(;;) {
        if(!fgets(buf + n, (int)(cap - n), stdin)) {
            if(n == 0) { free(buf); return lumin_make_string(""); }
            break;
        }
        n = strlen(buf);
        if(n > 0 && buf[n - 1] == '\n') break;
        if(n < cap - 1) break;                 /* 正常读满前退出（EOF 无换行） */
        size_t nc = cap * 2;
        char* nb = (char*)realloc(buf, nc);
        if(!nb) { fprintf(stderr, "input: 内存不足\n"); exit(EXIT_FAILURE); }
        buf = nb; cap = nc;
    }
    while(n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
    Value r = lumin_make_string(buf);
    free(buf);
    return r;
}

// range() 参数转 long long（double 整数值截断，兼容旧行为）
long long range_to_ll(Value v) {
    if(v.type == VAL_DOUBLE) return (long long)v.v.d;
    if(v.type != VAL_INT) runtime_error("range() 参数必须是整数");
    return v.v.i;
}

// range(n) / range(a,b) / range(a,b,step)：生成等差数列数组
// 只读属性检查：type 构造对象的 __classname__ 不可写/删（map 写路径统一拦截）
void lumin_check_classname_ro(Value arr, Value idx, const char* op)
{
    if(arr.type == VAL_MAP && idx.type == VAL_STRING && strcmp(idx.v.s, "__classname__") == 0) {
        char b[96];
        snprintf(b, sizeof b, "只读属性 __classname__ 不能%s", op);
        runtime_error(b);
    }
}

Value lumin_array_set(Value arr, Value idx, Value val) {
    if(arr.type == VAL_MAP) { lumin_check_classname_ro(arr, idx, "赋值"); lumin_map_set(&arr, idx, val); return val; }
    if(arr.type != VAL_ARRAY) runtime_error("下标访问的对象不是数组");
    long long i = array_index_of(idx);
    if(i < 0 || i >= arr.v.array->len) {
        char buf[128];
        snprintf(buf, sizeof(buf), "数组下标越界: %lld (长度 %d)", i, arr.v.array->len);
        runtime_error(buf);
    }
    Value* slot = &arr.v.array->items[i];
    *slot = val;
    return val;
}

// > 弱类型：任意一方为字符串 → 字典序strcmp；否则数值比较
Value lumin_gt(Value a, Value b) {
    if (is_string(a,b)) {
        char *sa = value_to_str(a);
        char *sb = value_to_str(b);
        int r = strcmp(sa, sb);
        free(sa);
        free(sb);
        return lumin_make_bool(r > 0);
    }
    double na = value_as_number(a);
    double nb = value_as_number(b);
    return lumin_make_bool(na > nb);
}

Value lumin_lt(Value a, Value b) {
    if (is_string(a,b)) {
        char *sa = value_to_str(a);
        char *sb = value_to_str(b);
        int r = strcmp(sa, sb);
        free(sa);
        free(sb);
        return lumin_make_bool(r < 0);
    }
    double na = value_as_number(a);
    double nb = value_as_number(b);
    return lumin_make_bool(na < nb);
}

Value lumin_ge(Value a, Value b) {
    if (is_string(a,b)) {
        char *sa = value_to_str(a);
        char *sb = value_to_str(b);
        int r = strcmp(sa, sb);
        free(sa);
        free(sb);
        return lumin_make_bool(r >= 0);
    }
    double na = value_as_number(a);
    double nb = value_as_number(b);
    return lumin_make_bool(na >= nb);
}

Value lumin_le(Value a, Value b) {
    if (is_string(a,b)) {
        char *sa = value_to_str(a);
        char *sb = value_to_str(b);
        int r = strcmp(sa, sb);
        free(sa);
        free(sb);
        return lumin_make_bool(r <= 0);
    }
    double na = value_as_number(a);
    double nb = value_as_number(b);
    return lumin_make_bool(na <= nb);
}

// == 弱相等：一边字符串，全部转字符串比较；两边字符串strcmp；其余数值比较
Value lumin_eq(Value a, Value b) {
    if (a.type == VAL_STRING && b.type == VAL_STRING) {
        if (a.v.s == NULL && b.v.s == NULL) return lumin_make_bool(1);
        if (a.v.s == NULL || b.v.s == NULL) return lumin_make_bool(0);
        return lumin_make_bool(strcmp(a.v.s, b.v.s) == 0);
    }
    if (is_string(a,b)) {
        char *sa = value_to_str(a);
        char *sb = value_to_str(b);
        int eq = (strcmp(sa, sb) == 0);
        free(sa);
        free(sb);
        return lumin_make_bool(eq);
    }
    if(a.type == VAL_ERROR || b.type == VAL_ERROR) {
        if(a.type == VAL_ERROR && b.type == VAL_ERROR) {
            int tm = strcmp(a.v.err.type ? a.v.err.type : "", b.v.err.type ? b.v.err.type : "");
            int mm = strcmp(a.v.err.message ? a.v.err.message : "", b.v.err.message ? b.v.err.message : "");
            return lumin_make_bool(tm == 0 && mm == 0);
        }
        const char* am = (a.type == VAL_ERROR) ? a.v.err.message : (a.type == VAL_STRING ? a.v.s : NULL);
        const char* bm = (b.type == VAL_ERROR) ? b.v.err.message : (b.type == VAL_STRING ? b.v.s : NULL);
        if(a.type == VAL_ERROR && b.type == VAL_MAP && lumin_map_has(b, lumin_make_string("message"))) {
            Value mv = lumin_map_get(b, lumin_make_string("message"));
            if(mv.type != VAL_STRING) return lumin_make_bool(0);
            const char* tm = NULL;
            if(a.v.err.type) {
                if(lumin_map_has(b, lumin_make_string("type"))) {
                    Value tv = lumin_map_get(b, lumin_make_string("type"));
                    if(tv.type == VAL_STRING) tm = tv.v.s;
                }
                if(tm && strcmp(tm, a.v.err.type) != 0) return lumin_make_bool(0);
            }
            return lumin_make_bool(strcmp(a.v.err.message, mv.v.s) == 0);
        }
        if(!am || !bm) return lumin_make_bool(0);
        return lumin_make_bool(strcmp(am, bm) == 0);
    }
    if(a.type == VAL_MAP || b.type == VAL_MAP) {
        if(a.type != VAL_MAP || b.type != VAL_MAP) return lumin_make_bool(0);
        if(a.v.map->len != b.v.map->len) return lumin_make_bool(0);
        MapIter it; map_iter_init(&it, a.v.map);
        Value k, vv;
        while(map_iter_next(&it, &k, &vv)) {
            if(!lumin_map_has(b, k)) return lumin_make_bool(0);
            Value bv = lumin_map_get(b, k);
            Value eq = lumin_eq(vv, bv);
            if(!eq.v.b) return lumin_make_bool(0);
        }
        return lumin_make_bool(1);
    }
    double na = value_as_number(a);
    double nb = value_as_number(b);
    return lumin_make_bool(na == nb);
}

Value lumin_ne(Value a, Value b) {
    Value eq = lumin_eq(a,b);
    return lumin_make_bool(!eq.v.b);
}

_Bool lumin_to_bool(Value v) {
    switch(v.type)
    {
        case VAL_INT:    return v.v.i != 0;
        case VAL_DOUBLE: return v.v.d != 0.0;
        case VAL_BOOL:   return v.v.b;
        case VAL_CHAR:   return (unsigned char)v.v.c != 0;
        case VAL_BYTE:   return (v.v.i & 0xFF) != 0;
        default: return 0;
    }
}

// (char)v 强转，C风格静默截断
Value lumin_cast_char(Value v) {
    char cv = 0;
    switch(v.type)
    {
        case VAL_INT:
            cv = (char)v.v.i;
            break;
        case VAL_DOUBLE:
            cv = (char)(long long)v.v.d;
            break;
        case VAL_BOOL:
            cv = v.v.b ? 1 : 0;
            break;
        case VAL_CHAR:
            cv = v.v.c;
            break;
        case VAL_STRING:
            if(v.v.s == NULL || v.v.s[0] == '\0'){
                cv = '\0';
            }else{
                cv = v.v.s[0];
            }
            break;
case VAL_ARRAY: {
    Value r = val_array(v.v.array->len);
    for(int i = 0; i < v.v.array->len; i++)
        r.v.array->items[i] = lumin_cast_char(v.v.array->items[i]);
    return r;
}
case VAL_MAP: {
    Value r = val_map();
    MapIter it; map_iter_init(&it, v.v.map);
        Value __k, __v;
        while(map_iter_next(&it, &__k, &__v))
            lumin_map_set(&r, __k, lumin_cast_char(__v));
    return r;
}
        default:
            runtime_error("(char) cast: unsupported type");
    }
    return lumin_make_char(cv);
}

Value lumin_cast_byte(Value v) {
    unsigned long long bv = 0;
    switch(v.type)
    {
        case VAL_INT:
            bv = (unsigned long long)v.v.i & 0xFFULL;
            break;
        case VAL_DOUBLE:
            bv = ((unsigned long long)(long long)v.v.d) & 0xFFULL;
            break;
        case VAL_BOOL:
            bv = v.v.b ? 1 : 0;
            break;
        case VAL_CHAR:
            bv = (unsigned char)v.v.c;
            break;
        case VAL_BYTE:
            bv = (unsigned long long)(v.v.i & 0xFF);
            break;
        case VAL_STRING:
            bv = (unsigned long long)atoll(v.v.s) & 0xFFULL;
            break;
        case VAL_NONE:
            bv = 0;
            break;
case VAL_ARRAY: {
    Value r = val_array(v.v.array->len);
    for(int i = 0; i < v.v.array->len; i++)
        r.v.array->items[i] = lumin_cast_byte(v.v.array->items[i]);
    return r;
}
case VAL_MAP: {
    Value r = val_map();
    MapIter it; map_iter_init(&it, v.v.map);
        Value __k, __v;
        while(map_iter_next(&it, &__k, &__v))
            lumin_map_set(&r, __k, lumin_cast_byte(__v));
    return r;
}
        default:
            runtime_error("(byte) cast: unsupported type");
            return val_none();
    }
    return lumin_make_byte((unsigned char)bv);
}

// (ASCII)v：char ↔ int，0‑255范围校验
Value lumin_cast_ascii(Value v) {
    if(v.type == VAL_ARRAY) {
        Value r = val_array(v.v.array->len);
        for(int i = 0; i < v.v.array->len; i++)
            r.v.array->items[i] = lumin_cast_ascii(v.v.array->items[i]);
        return r;
    }
    if(v.type == VAL_MAP) {
        Value r = val_map();
        MapIter it; map_iter_init(&it, v.v.map);
        Value __k, __v;
        while(map_iter_next(&it, &__k, &__v))
            lumin_map_set(&r, __k, lumin_cast_ascii(__v));
        return r;
    }
    if(v.type == VAL_CHAR)
    {
        // char → int编码
        return lumin_make_int((unsigned char)v.v.c);
    }
    else if(v.type == VAL_INT)
    {
        long long x = v.v.i;
        if(x < 0 || x > 255)
        {
            runtime_error("(ASCII) value out of range 0~255");
        }
        return lumin_make_char((char)(unsigned char)x);
    }
    else
    {
        runtime_error("(ASCII) cast only accept char / integer");
    }
    return lumin_make_int(0);
}

// (int)v 强转
Value lumin_cast_int(Value v) {
    long long iv = 0;
    switch(v.type)
    {
        case VAL_INT:
            iv = v.v.i;
            break;
        case VAL_DOUBLE:
            iv = (long long)v.v.d;
            break;
        case VAL_BOOL:
            iv = v.v.b ? 1 : 0;
            break;
        case VAL_CHAR:
            iv = (unsigned char)v.v.c;
            break;
        case VAL_BYTE:
            iv = v.v.i & 0xFF;
            break;
        case VAL_STRING: {
            const char* t = v.v.s;
            while(*t && isspace((unsigned char)*t)) t++;
            char* end = NULL;
            long long r = strtoll(t, &end, 10);
            if(end == t) runtime_error("(int) cast: 字符串无法转为整数");
            while(*end && isspace((unsigned char)*end)) end++;
            if(*end == '.') {
                double d = strtod(t, &end);
                while(*end && isspace((unsigned char)*end)) end++;
                if(*end != '\0') runtime_error("(int) cast: 字符串无法转为整数");
                iv = (long long)d;
            } else if(*end != '\0') {
                runtime_error("(int) cast: 字符串无法转为整数");
            } else {
                iv = r;
            }
            break;
        }
case VAL_ARRAY: {
    Value r = val_array(v.v.array->len);
    for(int i = 0; i < v.v.array->len; i++)
        r.v.array->items[i] = lumin_cast_int(v.v.array->items[i]);
    return r;
}
case VAL_MAP: {
    Value r = val_map();
    MapIter it; map_iter_init(&it, v.v.map);
        Value __k, __v;
        while(map_iter_next(&it, &__k, &__v))
            lumin_map_set(&r, __k, lumin_cast_int(__v));
    return r;
}
        default:
            runtime_error("(int) cast: unsupported type");
    }
    return lumin_make_int(iv);
}

// (double)v 强转
Value lumin_cast_double(Value v) {
    double dv = 0.0;
    switch(v.type)
    {
        case VAL_INT:
            dv = (double)v.v.i;
            break;
        case VAL_DOUBLE:
            dv = v.v.d;
            break;
        case VAL_BOOL:
            dv = v.v.b ? 1.0 : 0.0;
            break;
        case VAL_CHAR:
            dv = (double)(unsigned char)v.v.c;
            break;
        case VAL_BYTE:
            dv = (double)(v.v.i & 0xFF);
            break;
        case VAL_STRING: {
            const char* t = v.v.s;
            while(*t && isspace((unsigned char)*t)) t++;
            char* end = NULL;
            double d = strtod(t, &end);
            if(end == t) runtime_error("(double) cast: 字符串无法转为数字");
            while(*end && isspace((unsigned char)*end)) end++;
            if(*end != '\0') runtime_error("(double) cast: 字符串无法转为数字");
            dv = d;
            break;
        }
case VAL_ARRAY: {
    Value r = val_array(v.v.array->len);
    for(int i = 0; i < v.v.array->len; i++)
        r.v.array->items[i] = lumin_cast_double(v.v.array->items[i]);
    return r;
}
case VAL_MAP: {
    Value r = val_map();
    MapIter it; map_iter_init(&it, v.v.map);
        Value __k, __v;
        while(map_iter_next(&it, &__k, &__v))
            lumin_map_set(&r, __k, lumin_cast_double(__v));
    return r;
}
        default:
            runtime_error("(double) cast: unsupported type");
    }
    return lumin_make_double(dv);
}

// (bool)v 强转
Value lumin_cast_bool(Value v) {
    if(v.type == VAL_ARRAY) {
        Value r = val_array(v.v.array->len);
        for(int i = 0; i < v.v.array->len; i++)
            r.v.array->items[i] = lumin_cast_bool(v.v.array->items[i]);
        return r;
    }
    if(v.type == VAL_MAP) {
        Value r = val_map();
        MapIter it; map_iter_init(&it, v.v.map);
        Value __k, __v;
        while(map_iter_next(&it, &__k, &__v))
            lumin_map_set(&r, __k, lumin_cast_bool(__v));
        return r;
    }
    _Bool b = lumin_to_bool(v);
    return lumin_make_bool(b);
}

// (string)v 强转
Value lumin_cast_string(Value v) {
    if(v.type == VAL_ARRAY) {
        Value r = val_array(v.v.array->len);
        for(int i = 0; i < v.v.array->len; i++)
            r.v.array->items[i] = lumin_cast_string(v.v.array->items[i]);
        return r;
    }
    if(v.type == VAL_MAP) {
        Value r = val_map();
        MapIter it; map_iter_init(&it, v.v.map);
        Value __k, __v;
        while(map_iter_next(&it, &__k, &__v))
            lumin_map_set(&r, __k, lumin_cast_string(__v));
        return r;
    }
    char *s = value_to_str(v);
    Value res;
    res.type = VAL_STRING;
    res.v.s = s;
    return res;
}

int lumin_extract_int(Value v) {
    Value iv = lumin_cast_int(v);
    if (iv.type == VAL_INT) {
        return iv.v.i;
    }
    return 0;
}

void lumin_print(Value v) {
    switch(v.type)
    {
        case VAL_INT:
            printf("%lld\n", v.v.i);
            break;
        case VAL_DOUBLE:
            printf("%g\n", v.v.d);
            break;
        case VAL_BOOL:
            printf("%s\n", v.v.b ? "true" : "false");
            break;
        case VAL_STRING:
            printf("%s\n", v.v.s ? v.v.s : "(null)");
            break;
        case VAL_CHAR:
            printf("%c\n", v.v.c);
            break;
        case VAL_BYTE:
            printf("%lld\n", v.v.i & 0xFF);
            break;
        case VAL_NONE:
            printf("null\n");
            break;
        case VAL_FUNC:
            printf("<func>\n");
            break;
        case VAL_ARRAY:
            printf("<array>\n");
            break;
        case VAL_MAP: {
            char* js = lumin_json_stringify(v);   /* 字典按 JSON 序列化打印 */
            printf("%s\n", js);
            free(js);
            break;
        }
        default:
            printf("<unknown>\n");
            break;
    }
}

// toupper/tolower：ASCII 大小写转换（非 ASCII 保持）

// ===== 固定宽度整数强转（返回 VAL_INT，C 风格截断） =====
static long long value_to_ll(Value v) {
    switch(v.type) {
        case VAL_INT: case VAL_BYTE: return v.v.i;
        case VAL_DOUBLE: return (long long)v.v.d;
        case VAL_BOOL: return v.v.b ? 1 : 0;
        case VAL_CHAR: return (long long)(unsigned char)v.v.c;
        case VAL_STRING: return atoll(v.v.s ? v.v.s : "0");
        case VAL_NONE: return 0;
        default: runtime_error("整数强转: 不支持的类型"); return 0;
    }
}
static unsigned long long value_to_ull(Value v) {
    switch(v.type) {
        case VAL_INT: case VAL_BYTE: return (unsigned long long)v.v.i;
        case VAL_DOUBLE: return (unsigned long long)v.v.d;
        case VAL_BOOL: return v.v.b ? 1ULL : 0ULL;
        case VAL_CHAR: return (unsigned long long)(unsigned char)v.v.c;
        case VAL_STRING: return strtoull(v.v.s ? v.v.s : "0", NULL, 10);
        case VAL_NONE: return 0;
        default: runtime_error("整数强转: 不支持的类型"); return 0;
    }
}
static Value cast_int_width(Value v, int bits, int is_signed) {
    if(v.type == VAL_ARRAY) {
        Value r = val_array(v.v.array->len);
        for(int i = 0; i < v.v.array->len; i++)
            r.v.array->items[i] = cast_int_width(v.v.array->items[i], bits, is_signed);
        return r;
    }
    if(v.type == VAL_MAP) {
        Value r = val_map();
        MapIter it; map_iter_init(&it, v.v.map);
        Value __k, __v;
        while(map_iter_next(&it, &__k, &__v)) {
            lumin_map_set(&r, __k,
                          cast_int_width(__v, bits, is_signed));
        }
        return r;
    }
    long long ll = value_to_ll(v);
    unsigned long long ull = value_to_ull(v);
    switch(bits) {
        case 8:  return lumin_make_int(is_signed ? (long long)(int8_t)ll : (long long)(uint8_t)ull);
        case 16: return lumin_make_int(is_signed ? (long long)(int16_t)ll : (long long)(uint16_t)ull);
        case 32: return lumin_make_int(is_signed ? (long long)(int32_t)ll : (long long)(uint32_t)ull);
        case 64: return lumin_make_int(is_signed ? (long long)(int64_t)ll : (long long)(uint64_t)ull);
    }
    return lumin_make_int(ll);
}
Value lumin_cast_int8(Value v)  { return cast_int_width(v, 8, 1); }
Value lumin_cast_int16(Value v) { return cast_int_width(v, 16, 1); }
Value lumin_cast_int32(Value v) { return cast_int_width(v, 32, 1); }
Value lumin_cast_int64(Value v) { return cast_int_width(v, 64, 1); }
Value lumin_cast_uint8(Value v)  { return cast_int_width(v, 8, 0); }
Value lumin_cast_uint16(Value v) { return cast_int_width(v, 16, 0); }
Value lumin_cast_uint32(Value v) { return cast_int_width(v, 32, 0); }
Value lumin_cast_uint64(Value v) { return cast_int_width(v, 64, 0); }
Value lumin_cast_long(Value v)     { return lumin_cast_int(v); }  // long → 64 位
Value lumin_cast_longlong(Value v) { return lumin_cast_int(v); }  // long long → 64 位
// float：32 位单精度截断（运行时仍存 double）
static Value cast_float_rec(Value v) {
    if(v.type == VAL_ARRAY) {
        Value r = val_array(v.v.array->len);
        for(int i = 0; i < v.v.array->len; i++)
            r.v.array->items[i] = cast_float_rec(v.v.array->items[i]);
        return r;
    }
    if(v.type == VAL_MAP) {
        Value r = val_map();
        MapIter it; map_iter_init(&it, v.v.map);
        Value __k, __v;
        while(map_iter_next(&it, &__k, &__v)) {
            lumin_map_set(&r, __k,
                          cast_float_rec(__v));
        }
        return r;
    }
    double d;
    switch(v.type) {
        case VAL_DOUBLE: d = v.v.d; break;
        case VAL_INT: case VAL_BYTE: d = (double)v.v.i; break;
        case VAL_BOOL: d = v.v.b ? 1.0 : 0.0; break;
        case VAL_CHAR: d = (double)(unsigned char)v.v.c; break;
        case VAL_STRING: d = atof(v.v.s ? v.v.s : "0"); break;
        case VAL_NONE: d = 0.0; break;
        default: runtime_error("(float) 强转: 不支持的类型"); return val_none();
    }
    return lumin_make_double((double)(float)d);
}
Value lumin_cast_float(Value v) { return cast_float_rec(v); }
