#include "lm_value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <ctype.h>

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

// len(x)：数组长度 / 字符串字符数
Value lumin_len(Value v) {
    if(v.type == VAL_ARRAY) return lumin_make_int(v.v.array.len);
    if(v.type == VAL_STRING) return lumin_make_int((long long)strlen(v.v.s));
    runtime_error("len() 参数必须是数组或字符串");
    return lumin_make_int(0);
}

// 下标读：数组元素 / 字符串字符（返回 char）
Value lumin_index_get(Value c, Value idx) {
    long long i = array_index_of(idx);
    if(c.type == VAL_ARRAY) {
        if(i < 0 || i >= c.v.array.len) {
            char buf[128];
            snprintf(buf, sizeof(buf), "数组下标越界: %lld (长度 %d)", i, c.v.array.len);
            runtime_error(buf);
        }
        return c.v.array.items[i];
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
    runtime_error("下标访问的对象不是数组或字符串");
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
        case VAL_STRING: return lumin_make_string("string");
        case VAL_FUNC:   return lumin_make_string("func");
        case VAL_ARRAY:  return lumin_make_string("array");
    }
    return lumin_make_string("unknown");
}

Value lumin_input(void) {
    char buf[4096];
    if(!fgets(buf, sizeof(buf), stdin)) return lumin_make_string("");
    size_t n = strlen(buf);
    while(n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
    return lumin_make_string(buf);
}

// range() 参数转 long long（double 整数值截断，兼容旧行为）
static long long range_to_ll(Value v) {
    if(v.type == VAL_DOUBLE) return (long long)v.v.d;
    if(v.type != VAL_INT) runtime_error("range() 参数必须是整数");
    return v.v.i;
}

// range(n) / range(a,b) / range(a,b,step)：生成等差数列数组
Value lumin_range_n(Value* args, int n) {
    if(n < 1 || n > 3) runtime_error("range() 需要 1 到 3 个参数");
    long long a = 0, b, step = 1;
    if(n == 1) { b = range_to_ll(args[0]); if(b < 0) runtime_error("range() 上界不能为负数"); }
    else if(n == 2) { a = range_to_ll(args[0]); b = range_to_ll(args[1]); }
    else { a = range_to_ll(args[0]); b = range_to_ll(args[1]); step = range_to_ll(args[2]); if(step == 0) runtime_error("range() 步长不能为 0"); }
    // 元素数：正步长 (b-a) 向上；负步长 (a-b) 向上；方向不对 → 空
    long double span = step > 0 ? ((long double)b - a) : ((long double)a - b);
    long long len = 0;
    if(span > 0) len = (long long)((span + (step > 0 ? step : -step) - 1) / (step > 0 ? step : -step));
    if(len < 0) len = 0;
    if(len > 100000000LL) runtime_error("range() 元素数过多");
    Value arr = val_array((int)len);
    for(long long i = 0; i < len; i++) {
        Value item = lumin_make_int(a + i * step);
        arr.v.array.items[i] = val_clone(&item);
    }
    return arr;
}

Value lumin_substr(Value s, Value start, Value n) {
    if(s.type != VAL_STRING) runtime_error("substr() 第一个参数必须是字符串");
    long long slen = (long long)strlen(s.v.s);
    long long i = array_index_of(start);
    long long cnt = array_index_of(n);
    if(i < 0 || i > slen) runtime_error("substr() 起始越界");
    if(cnt < 0) runtime_error("substr() 长度不能为负数");
    if(i + cnt > slen) cnt = slen - i;
    char* out = (char*)malloc(cnt + 1);
    if(!out) { perror("lumin_substr"); exit(EXIT_FAILURE); }
    memcpy(out, s.v.s + i, cnt);
    out[cnt] = '\0';
    Value r = lumin_make_string(out);
    free(out);
    return r;
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
        case VAL_NONE:
            printf("null\n");
            break;
        case VAL_FUNC:
            printf("<func>\n");
            break;
        case VAL_ARRAY:
            printf("<array>\n");
            break;
        default:
            printf("<unknown>\n");
            break;
    }
}

// toupper/tolower：ASCII 大小写转换（非 ASCII 保持）
static Value str_case(Value s, int upper)
{
    if(s.type != VAL_STRING) runtime_error("参数必须是字符串");
    char* out = (char*)malloc(strlen(s.v.s) + 1);
    if(!out) { perror("str_case"); exit(EXIT_FAILURE); }
    const unsigned char* p = (const unsigned char*)s.v.s;
    char* q = out;
    while(*p) {
        if(upper && *p >= 'a' && *p <= 'z') *q = *p - 'a' + 'A';
        else if(!upper && *p >= 'A' && *p <= 'Z') *q = *p - 'A' + 'a';
        else *q = (char)*p;
        p++; q++;
    }
    *q = '\0';
    Value r = lumin_make_string(out);
    free(out);
    return r;
}

Value lumin_toupper(Value s) { return str_case(s, 1); }
Value lumin_tolower(Value s) { return str_case(s, 0); }

// split(s, sep)：按分隔符拆成字符串数组
Value lumin_split(Value s, Value sep)
{
    if(s.type != VAL_STRING || sep.type != VAL_STRING)
        runtime_error("split() 参数必须是字符串");
    if(sep.v.s[0] == '\0') runtime_error("split() 分隔符不能为空");
    const char* p = s.v.s;
    const char* sp = sep.v.s;
    size_t splen = strlen(sp);
    int count = 1;
    for(const char* t = p; (t = strstr(t, sp)) != NULL; t += splen) count++;
    Value arr = val_array(count);
    int idx = 0;
    const char* start = p;
    const char* hit = strstr(start, sp);
    while(hit) {
        size_t len = (size_t)(hit - start);
        char* piece = (char*)malloc(len + 1);
        memcpy(piece, start, len);
        piece[len] = '\0';
        Value item = lumin_make_string(piece);
        free(piece);
        arr.v.array.items[idx++] = val_clone(&item);
        start = hit + splen;
        hit = strstr(start, sp);
    }
    Value item = lumin_make_string(start);
    arr.v.array.items[idx++] = val_clone(&item);
    return arr;
}

// del(arr, idx)：返回删除第 idx 个元素后的新数组（值语义，原数组不变）
Value lumin_del(Value arr, Value idx)
{
    if(arr.type != VAL_ARRAY) runtime_error("del() 第一个参数必须是数组");
    if(idx.type != VAL_INT) runtime_error("del() 下标必须是整数");
    long long i = idx.v.i;
    int n = arr.v.array.len;
    if(i < 0 || i >= n) { char b[96]; snprintf(b, sizeof b, "del() 下标 %lld 越界（长度 %d）", i, n); runtime_error(b); }
    Value r = val_array(n - 1);
    for(int k = 0; k < n; k++) {
        if(k == (int)i) continue;
        int dst = (k < (int)i) ? k : k - 1;
        r.v.array.items[dst] = val_clone(&arr.v.array.items[k]);
    }
    return r;
}

// insert(arr, idx, val)：返回在第 idx 个位置插入 val 后的新数组（idx 允许 0..n）
Value lumin_insert(Value arr, Value idx, Value val)
{
    if(arr.type != VAL_ARRAY) runtime_error("insert() 第一个参数必须是数组");
    if(idx.type != VAL_INT) runtime_error("insert() 下标必须是整数");
    long long i = idx.v.i;
    int n = arr.v.array.len;
    if(i < 0 || i > n) { char b[96]; snprintf(b, sizeof b, "insert() 下标 %lld 越界（允许 0..%d）", i, n); runtime_error(b); }
    Value r = val_array(n + 1);
    for(int k = 0; k < (int)i; k++) r.v.array.items[k] = val_clone(&arr.v.array.items[k]);
    r.v.array.items[(int)i] = val_clone(&val);
    for(int k = (int)i; k < n; k++) r.v.array.items[k + 1] = val_clone(&arr.v.array.items[k]);
    return r;
}

// floor/ceil：向下/向上取整，返回 int
static Value num_round(Value x, int up)
{
    if(x.type != VAL_INT && x.type != VAL_DOUBLE && x.type != VAL_CHAR)
        runtime_error("参数必须是数字");
    double d = value_as_number(x);
    double r = up ? ceil(d) : floor(d);
    if(r > 9.2e18 || r < -9.2e18)
        runtime_error("取整结果超出整数范围");
    return lumin_make_int((long long)r);
}
Value lumin_floor(Value x) { return num_round(x, 0); }
Value lumin_ceil(Value x)  { return num_round(x, 1); }

// abs：绝对值（保持原类型）
Value lumin_abs(Value x)
{
    if(x.type == VAL_INT) return lumin_make_int(x.v.i < 0 ? -x.v.i : x.v.i);
    if(x.type == VAL_DOUBLE) return lumin_make_double(fabs(x.v.d));
    if(x.type == VAL_CHAR) return lumin_make_char((char)(x.v.c < 0 ? -x.v.c : x.v.c));
    runtime_error("abs() 参数必须是数字");
    return val_none();
}

// sqrt：平方根（返回 double）
Value lumin_sqrt(Value x)
{
    if(x.type != VAL_INT && x.type != VAL_DOUBLE && x.type != VAL_CHAR)
        runtime_error("sqrt() 参数必须是数字");
    double d = value_as_number(x);
    if(d < 0) runtime_error("sqrt() 不能对负数开方");
    return lumin_make_double(sqrt(d));
}

// max/min：变参极值（复用比较语义：数字/字符串混合均可）
static Value extremum(Value* args, int n, int want_max)
{
    if(n < 1) runtime_error("需要至少 1 个参数");
    Value best = args[0];
    for(int i = 1; i < n; i++) {
        Value c = want_max ? lumin_gt(args[i], best) : lumin_lt(args[i], best);
        if(c.v.b) best = args[i];
    }
    return best;
}
Value lumin_max(Value* args, int n) { return extremum(args, n, 1); }
Value lumin_min(Value* args, int n) { return extremum(args, n, 0); }

// join：字符串数组按分隔符拼接（非字符串元素 value_to_str 转换）
Value lumin_join(Value arr, Value sep)
{
    if(arr.type != VAL_ARRAY) runtime_error("join() 第一个参数必须是数组");
    if(sep.type != VAL_STRING) runtime_error("join() 分隔符必须是字符串");
    size_t total = 1;
    for(int i = 0; i < arr.v.array.len; i++) {
        char* t = value_to_str(arr.v.array.items[i]);
        total += strlen(t);
        if(i < arr.v.array.len - 1) total += strlen(sep.v.s);
        free(t);
    }
    char* out = (char*)malloc(total);
    if(!out) { perror("join"); exit(EXIT_FAILURE); }
    out[0] = '\0';
    for(int i = 0; i < arr.v.array.len; i++) {
        if(i > 0) strcat(out, sep.v.s);
        char* t = value_to_str(arr.v.array.items[i]);
        strcat(out, t);
        free(t);
    }
    Value r = lumin_make_string(out);
    free(out);
    return r;
}

// contains：字符串子串 / 数组元素相等
Value lumin_contains(Value hay, Value needle)
{
    if(hay.type == VAL_STRING) {
        if(needle.type != VAL_STRING) runtime_error("contains() 字符串查找需要字符串参数");
        return lumin_make_bool(strstr(hay.v.s, needle.v.s) != NULL);
    }
    if(hay.type == VAL_ARRAY) {
        for(int i = 0; i < hay.v.array.len; i++) {
            Value eq = lumin_eq(hay.v.array.items[i], needle);
            if(eq.v.b) return lumin_make_bool(1);
        }
        return lumin_make_bool(0);
    }
    runtime_error("contains() 第一个参数必须是字符串或数组");
    return val_none();
}

// repeat(s, n)：字符串重复 n 次
Value lumin_repeat(Value s, Value n)
{
    if(s.type != VAL_STRING) runtime_error("repeat() 第一个参数必须是字符串");
    if(n.type != VAL_INT) runtime_error("repeat() 次数必须是整数");
    long long k = n.v.i;
    if(k < 0) runtime_error("repeat() 次数不能为负数");
    size_t len = strlen(s.v.s);
    if(k > 0 && len > (size_t)((1ULL << 40) / k)) runtime_error("repeat() 结果过大");
    size_t total = len * (size_t)k;
    char* out = (char*)malloc(total + 1);
    if(!out) { perror("repeat"); exit(EXIT_FAILURE); }
    for(long long i = 0; i < k; i++) memcpy(out + len * (size_t)i, s.v.s, len);
    out[total] = '\0';
    Value r = lumin_make_string(out);
    free(out);
    return r;
}

// replace(s, from, to)：替换所有 from 为 to（from 空串报错）
Value lumin_replace(Value s, Value from, Value to)
{
    if(s.type != VAL_STRING || from.type != VAL_STRING || to.type != VAL_STRING)
        runtime_error("replace() 三个参数都必须是字符串");
    if(from.v.s[0] == '\0') runtime_error("replace() 被替换串不能为空");
    const char* p = s.v.s;
    const char* f = from.v.s;
    const char* t = to.v.s;
    size_t flen = strlen(f), tlen = strlen(t), slen = strlen(p);
    int count = 0;
    for(const char* q = p; (q = strstr(q, f)) != NULL; q += flen) count++;
    if(count == 0) return lumin_make_string(p);  // 无匹配，原样返回
    size_t outlen = slen + (size_t)count * (tlen > flen ? tlen - flen : 0);
    char* out = (char*)malloc(outlen + 1);
    if(!out) { perror("replace"); exit(EXIT_FAILURE); }
    char* w = out;
    const char* start = p;
    const char* hit = strstr(start, f);
    while(hit) {
        size_t pre = (size_t)(hit - start);
        memcpy(w, start, pre); w += pre;
        memcpy(w, t, tlen); w += tlen;
        start = hit + flen;
        hit = strstr(start, f);
    }
    size_t rest = strlen(start);
    memcpy(w, start, rest); w += rest;
    *w = '\0';
    Value r = lumin_make_string(out);
    free(out);
    return r;
}

// sum/avg：数字数组聚合（只允许 int/double 元素）
static double array_sum_d(Value arr, long long* isum, int* all_int)
{
    double dsum = 0;
    *isum = 0; *all_int = 1;
    for(int i = 0; i < arr.v.array.len; i++) {
        Value v = arr.v.array.items[i];
        if(v.type != VAL_INT && v.type != VAL_DOUBLE)
            runtime_error("sum()/avg() 数组元素必须是数字");
        if(v.type == VAL_DOUBLE) *all_int = 0;
        dsum += value_as_number(v);
        if(v.type == VAL_INT) *isum += v.v.i;
    }
    return dsum;
}
Value lumin_sum(Value arr)
{
    if(arr.type != VAL_ARRAY) runtime_error("sum() 参数必须是数组");
    long long isum; int all_int;
    double dsum = array_sum_d(arr, &isum, &all_int);
    if(all_int) return lumin_make_int(isum);
    return lumin_make_double(dsum);
}
Value lumin_avg(Value arr)
{
    if(arr.type != VAL_ARRAY) runtime_error("avg() 参数必须是数组");
    if(arr.v.array.len == 0) runtime_error("avg() 不能对空数组求平均");
    long long isum; int all_int;
    double dsum = array_sum_d(arr, &isum, &all_int);
    return lumin_make_double(dsum / arr.v.array.len);
}

// format(fmt, args...)：{} 占位依次替换（{{ 和 }} 转义字面花括号）
Value lumin_format(Value* args, int n) {
    if(n < 1 || args[0].type != VAL_STRING) runtime_error("format() 第一个参数必须是格式串");
    const char* fmt = args[0].v.s;
    int nargs = n - 1;
    int placeholders = 0;
    const char* scan = fmt;
    while(*scan) {
        if(scan[0] == '{' && scan[1] == '{') { scan += 2; continue; }
        if(scan[0] == '}' && scan[1] == '}') { scan += 2; continue; }
        if(scan[0] == '{' && scan[1] == '}') { placeholders++; scan += 2; continue; }
        if(scan[0] == '{') runtime_error("format() 格式串含未配对的 '{'");
        if(scan[0] == '}') runtime_error("format() 格式串含未配对的 '}'");
        scan++;
    }
    if(placeholders != nargs) {
        char b[128];
        snprintf(b, sizeof b, "format() 占位符 %d 个（给了 %d 个实参）", placeholders, nargs);
        runtime_error(b);
    }
    size_t cap = strlen(fmt) + 64;
    for(int i = 0; i < nargs; i++) {
        char* t = value_to_str(args[i + 1]);
        cap += strlen(t);
        free(t);
    }
    char* out = (char*)malloc(cap + 1);
    if(!out) { perror("format"); exit(EXIT_FAILURE); }
    size_t w = 0;
    int ai = 0;
    const char* p = fmt;
    while(*p) {
        if(p[0] == '{' && p[1] == '{') { out[w++] = '{'; p += 2; continue; }
        if(p[0] == '}' && p[1] == '}') { out[w++] = '}'; p += 2; continue; }
        if(p[0] == '{' && p[1] == '}') {
            char* t = value_to_str(args[ai + 1]);
            size_t tl = strlen(t);
            memcpy(out + w, t, tl); w += tl;
            free(t);
            ai++;
            p += 2;
            continue;
        }
        out[w++] = *p++;
    }
    out[w] = '\0';
    Value r = lumin_make_string(out);
    free(out);
    return r;
}

// sort：升序（全数字按数值 / 全字符串按字典序），混合类型报错
static int g_sort_numeric = 1;
static int sort_cmp(const void* pa, const void* pb)
{
    const Value* a = (const Value*)pa;
    const Value* b = (const Value*)pb;
    if(g_sort_numeric) {
        double x = value_as_number(*a), y = value_as_number(*b);
        return (x > y) - (x < y);
    }
    return strcmp(a->v.s, b->v.s);
}
Value lumin_sort(Value arr) {
    if(arr.type != VAL_ARRAY) runtime_error("sort() 参数必须是数组");
    int n = arr.v.array.len;
    int all_num = 1, all_str = 1;
    for(int i = 0; i < n; i++) {
        Value v = arr.v.array.items[i];
        if(v.type != VAL_INT && v.type != VAL_DOUBLE) all_num = 0;
        if(v.type != VAL_STRING) all_str = 0;
    }
    if(!all_num && !all_str) runtime_error("sort() 数组元素须全为数字或全为字符串");
    int numeric = all_num;
    Value r = val_array(n);
    for(int i = 0; i < n; i++) r.v.array.items[i] = val_clone(&arr.v.array.items[i]);
    if(n > 1) {
        g_sort_numeric = numeric;
        qsort(r.v.array.items, (size_t)n, sizeof(Value), sort_cmp);
    }
    return r;
}

// reverse：反转（任意类型）
Value lumin_reverse(Value arr) {
    if(arr.type != VAL_ARRAY) runtime_error("reverse() 参数必须是数组");
    int n = arr.v.array.len;
    Value r = val_array(n);
    for(int i = 0; i < n; i++) r.v.array.items[i] = val_clone(&arr.v.array.items[n - 1 - i]);
    return r;
}

// strip：去首尾空白（空格/tab/换行/回车/垂直制表/换页）
Value lumin_strip(Value s)
{
    if(s.type != VAL_STRING) runtime_error("strip() 参数必须是字符串");
    const char* p = s.v.s;
    while(*p && isspace((unsigned char)*p)) p++;
    size_t len = strlen(p);
    while(len > 0 && isspace((unsigned char)p[len - 1])) len--;
    char* out = (char*)malloc(len + 1);
    if(!out) { perror("strip"); exit(EXIT_FAILURE); }
    memcpy(out, p, len);
    out[len] = '\0';
    Value r = lumin_make_string(out);
    free(out);
    return r;
}

// startswith / endswith：前缀/后缀判断
Value lumin_startswith(Value s, Value prefix)
{
    if(s.type != VAL_STRING || prefix.type != VAL_STRING)
        runtime_error("startswith() 两个参数都必须是字符串");
    size_t sl = strlen(s.v.s), pl = strlen(prefix.v.s);
    return lumin_make_bool(pl <= sl && strncmp(s.v.s, prefix.v.s, pl) == 0);
}
Value lumin_endswith(Value s, Value suffix)
{
    if(s.type != VAL_STRING || suffix.type != VAL_STRING)
        runtime_error("endswith() 两个参数都必须是字符串");
    size_t sl = strlen(s.v.s), fl = strlen(suffix.v.s);
    return lumin_make_bool(fl <= sl && strcmp(s.v.s + sl - fl, suffix.v.s) == 0);
}


// ---------------- 文件 IO 内置（read "path" / write "path" value / file_exists） ----------------

// read_file(path) → 文件全部内容（字符串）；失败 → runtime_error
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
