#include "lm_value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
    char* buf = (char*)malloc(len + 1);
    memcpy(buf, s, len);
    buf[len] = '\0';
    v.v.s = buf;
    return v;
}

Value lumin_make_char(char ch) {
    Value v;
    v.type = VAL_CHAR;
    v.v.c = ch;
    return v;
}

// 获取value的数值，int转double
static double value_as_number(Value x) {
    if(x.type == VAL_INT)
        return (double)x.v.i;
    else if(x.type == VAL_DOUBLE)
        return x.v.d;
    else if(x.type == VAL_CHAR)
        return (double)x.v.c;
    return 0.0;
}

// 判断是否是字符串类型
static int is_string(Value a, Value b) {
    return (a.type == VAL_STRING) || (b.type == VAL_STRING);
}

// 把一个Value转堆字符串（用于字符串拼接、弱比较）
static char* value_to_str(Value v) {
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
        char* out = (char*)malloc(la + lb + 1);
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
static long long array_index_of(Value idx) {
    if(idx.type == VAL_INT) return idx.v.i;
    if(idx.type == VAL_DOUBLE) return (long long)idx.v.d;
    if(idx.type == VAL_CHAR) return (unsigned char)idx.v.c;
    runtime_error("数组下标必须是数值");
    return 0;
}

Value lumin_array_get(Value arr, Value idx) {
    if(arr.type != VAL_ARRAY) runtime_error("下标访问的对象不是数组");
    long long i = array_index_of(idx);
    if(i < 0 || i >= arr.v.array.len) {
        char buf[128];
        snprintf(buf, sizeof(buf), "数组下标越界: %lld (长度 %d)", i, arr.v.array.len);
        runtime_error(buf);
    }
    return arr.v.array.items[i];   // 返回数组持有值的引用（调用方如需长期持有需 clone）
}

// 写回数组元素（深拷贝），返回 val 作为表达式值
Value lumin_array_set(Value arr, Value idx, Value val) {
    if(arr.type != VAL_ARRAY) runtime_error("下标访问的对象不是数组");
    long long i = array_index_of(idx);
    if(i < 0 || i >= arr.v.array.len) {
        char buf[128];
        snprintf(buf, sizeof(buf), "数组下标越界: %lld (长度 %d)", i, arr.v.array.len);
        runtime_error(buf);
    }
    Value* slot = &arr.v.array.items[i];
    val_destroy(slot);
    *slot = val_clone(&val);
    return val;
}

Value lumin_array_len(Value arr) {
    if(arr.type != VAL_ARRAY) runtime_error("len() 参数必须是数组");
    return lumin_make_int(arr.v.array.len);
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
        default:
            runtime_error("(char) cast: unsupported type");
    }
    return lumin_make_char(cv);
}

// (ASCII)v：char ↔ int，0‑255范围校验
Value lumin_cast_ascii(Value v) {
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
        case VAL_STRING:
            runtime_error("(int) cast cannot convert string");
            break;
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
        case VAL_STRING:
            runtime_error("(double) cast cannot convert string");
            break;
        default:
            runtime_error("(double) cast: unsupported type");
    }
    return lumin_make_double(dv);
}

// (bool)v 强转
Value lumin_cast_bool(Value v) {
    _Bool b = lumin_to_bool(v);
    return lumin_make_bool(b);
}

// (string)v 强转
Value lumin_cast_string(Value v) {
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
        case VAL_ARRAY:
            printf("<array>\n");
            break;
        default:
            printf("<unknown>\n");
            break;
    }
}
