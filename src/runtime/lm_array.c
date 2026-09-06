// lm_array.c —— 数组操作内置函数
#include "lm_array.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

// sort_cmp 排序方向（lumin_sort 设置后调用 qsort）
static _Thread_local int g_sort_numeric = 0;   /* TLS：多线程 sort 互不干扰 */



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

Value lumin_del(Value arr, Value idx)
{
    if(arr.type == VAL_MAP) {
        if(idx.type != VAL_STRING) runtime_error("del() 字典键必须是字符串");
        return lumin_map_del(arr, idx.v.s);
    }
    if(arr.type != VAL_ARRAY) runtime_error("del() 第一个参数必须是数组或字典");
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

// add(arr, val)：追加元素，返回新数组（arr.add(x) 方法链 / add(arr,x) 内置）
Value lumin_array_add(Value arr, Value val)
{
    if(arr.type != VAL_ARRAY) runtime_error("add() 第一个参数必须是数组");
    int n = arr.v.array.len;
    Value r = val_array(n + 1);
    for(int k = 0; k < n; k++) r.v.array.items[k] = val_clone(&arr.v.array.items[k]);
    r.v.array.items[n] = val_clone(&val);
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

// indexOf(arr, x)：首个相等元素下标，-1 未找到（== 类型敏感语义）
Value lumin_index_of(Value arr, Value x)
{
    if(arr.type != VAL_ARRAY) runtime_error("indexOf() 第一个参数必须是数组");
    for(int i = 0; i < arr.v.array.len; i++) {
        Value eq = lumin_eq(arr.v.array.items[i], x);
        if(lumin_to_bool(eq)) return lumin_make_int(i);
    }
    return lumin_make_int(-1);
}

// arr_get(arr, i)：安全取（越界/非数组 → null，不抛错）
Value lumin_array_get_safe(Value arr, Value idx)
{
    if(arr.type != VAL_ARRAY) return val_none();
    if(idx.type != VAL_INT) return val_none();
    long long i = idx.v.i;
    if(i < 0 || i >= arr.v.array.len) return val_none();
    return arr.v.array.items[i];
}

// set(arr, i, v)：原地改（与 a[i]=v 一致），返回数组本身支持链式
Value lumin_array_set_method(Value arr, Value idx, Value val)
{
    if(arr.type != VAL_ARRAY) runtime_error("set() 第一个参数必须是数组");
    if(idx.type != VAL_INT) runtime_error("set() 下标必须是整数");
    long long i = idx.v.i;
    if(i < 0 || i >= arr.v.array.len) {
        char b[96]; snprintf(b, sizeof b, "set() 下标 %lld 越界（长度 %d）", i, arr.v.array.len);
        runtime_error(b);
    }
    Value* slot = &arr.v.array.items[i];
    val_destroy(slot);
    *slot = val_clone(&val);
    return arr;
}

// first(arr) / last(arr)：首/尾元素（空数组 → null）
Value lumin_array_first(Value arr)
{
    if(arr.type != VAL_ARRAY || arr.v.array.len == 0) return val_none();
    return arr.v.array.items[0];
}
Value lumin_array_last(Value arr)
{
    if(arr.type != VAL_ARRAY || arr.v.array.len == 0) return val_none();
    return arr.v.array.items[arr.v.array.len - 1];
}

// floor/ceil：向下/向上取整，返回 int

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
