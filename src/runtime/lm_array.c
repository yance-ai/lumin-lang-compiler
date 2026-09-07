// lm_array.c —— 数组操作内置函数
#include "lm_array.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <limits.h>

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
    /* 无人为上限：与 C 一致，数组长度受 int 类型与内存共同约束
       （INT_MAX 仅为类型上限，防止 (int) 截断；实际先受 malloc 失败约束） */
    if(len > INT_MAX) runtime_error("range() 元素数超出数组长度上限（INT_MAX）");
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
        lumin_check_classname_ro(arr, idx, "删除");
        return lumin_map_del(arr, idx);
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
    if(arr.type == VAL_MAP) {
        return lumin_map_get(arr, idx);
    }
    if(arr.type != VAL_ARRAY) return val_none();
    if(idx.type != VAL_INT) return val_none();
    long long i = idx.v.i;
    if(i < 0 || i >= arr.v.array.len) return val_none();
    return arr.v.array.items[i];
}

// set(arr, i, v)：原地改（与 a[i]=v 一致），返回数组本身支持链式
Value lumin_array_set_method(Value arr, Value idx, Value val)
{
    if(arr.type == VAL_MAP) {
        lumin_check_classname_ro(arr, idx, "赋值");
        lumin_map_set(&arr, idx, val);
        return arr;
    }
    if(arr.type != VAL_ARRAY) runtime_error("set() 第一个参数必须是数组或字典");
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
    if(arr.type == VAL_MAP) {
        if(arr.v.map->len == 0) return val_none();
        MapIter it; map_iter_init(&it, arr.v.map);
        Value k, vv; map_iter_next(&it, &k, &vv);
        return val_clone(&vv);
    }
    if(arr.type != VAL_ARRAY || arr.v.array.len == 0) return val_none();
    return arr.v.array.items[0];
}
Value lumin_array_last(Value arr)
{
    if(arr.type == VAL_MAP) {
        if(arr.v.map->len == 0) return val_none();
        MapIter it; map_iter_init(&it, arr.v.map);
        Value k, vv, lastv;
        while(map_iter_next(&it, &k, &vv)) lastv = vv;
        return val_clone(&lastv);
    }
    if(arr.type != VAL_ARRAY || arr.v.array.len == 0) return val_none();
    return arr.v.array.items[arr.v.array.len - 1];
}

// add 的 map 路径：m.add(k, v) 设键值，返回 m（链式）
Value lumin_map_add(Value m, Value k, Value v)
{
    if(m.type != VAL_MAP) runtime_error("add() 第一个参数必须是数组或字典");
    lumin_check_classname_ro(m, k, "赋值");
    lumin_map_set(&m, k, v);
    return m;
}

// clear 容器：数组值语义 → 返回新空数组（与 add/remove 一致）；
// 字典引用语义 → 原地清空，返回自身（链式）
// 只读 __classname__ 不被清理：type 构造对象 clear 后类名属性保留
Value lumin_array_clear(Value v)
{
    if(v.type == VAL_MAP) {
        ValueMap* m = v.v.map;
        Value cnv = val_none();
        if(lumin_map_has(v, lumin_make_string("__classname__")))
            cnv = lumin_map_get(v, lumin_make_string("__classname__"));
        // 清空所有桶
        for(int bi = 0; bi < m->cap; bi++) {
            if(m->tree[bi]) {
                MapEntry* stk[256]; int top = 0;
                MapEntry* cur = m->buckets[bi];
                while(cur || top > 0) {
                    while(cur) { stk[top++] = cur; cur = cur->left; }
                    cur = stk[--top];
                    MapEntry* r = cur->right;
                    entry_free(cur);
                    cur = r;
                }
            } else {
                MapEntry* e = m->buckets[bi];
                while(e) { MapEntry* nx = e->next; entry_free(e); e = nx; }
            }
            m->buckets[bi] = NULL;
            m->tree[bi] = 0;
        }
        m->len = 0;
        if(cnv.type != VAL_NONE) { lumin_map_set(&v, lumin_make_string("__classname__"), cnv); val_destroy(&cnv); }
        return v;
    }
    if(v.type == VAL_ARRAY) return val_array(0);
    runtime_error("clear() 参数必须是数组或字典");
    return v;
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

// ===== 数组/字典扁平化 =====
// depth：展开层数（1=展开一层，2=两层，负数=无限）；字典 → 值数组再展开
typedef struct { Value* items; int len; int cap; } FlatBuf;
static void flat_push(FlatBuf* b, Value v) {
    if(b->len >= b->cap) { b->cap = b->cap ? b->cap * 2 : 16; b->items = realloc(b->items, b->cap * sizeof(Value)); }
    b->items[b->len++] = val_clone(&v);
}
static void flat_rec(Value v, int depth, FlatBuf* b) {
    if(v.type == VAL_ARRAY) {
        if(depth > 0) {
            for(int i = 0; i < v.v.array.len; i++) flat_rec(v.v.array.items[i], depth - 1, b);
        } else {
            /* depth 耗尽：数组元素逐个 push（元素若是数组保持原样） */
            for(int i = 0; i < v.v.array.len; i++) flat_push(b, v.v.array.items[i]);
        }
    } else if(v.type == VAL_MAP && depth > 0) {
        /* 字典：按值展开（flat 的字典语义 = 值数组的扁平化） */
        Value vals = lumin_map_values(v);
        for(int i = 0; i < vals.v.array.len; i++) flat_rec(vals.v.array.items[i], depth - 1, b);
    } else {
        flat_push(b, v);
    }
}
Value lumin_array_flat(Value v, int depth) {
    if(depth < 0) depth = 2147483647;  /* 负数 → 无限展开 */
    FlatBuf b = {0};
    if(v.type == VAL_ARRAY && depth == 0) {
        /* 深度 0：浅拷贝（顶层元素逐个复制，不递归） */
        for(int i = 0; i < v.v.array.len; i++) flat_push(&b, v.v.array.items[i]);
    } else if(v.type == VAL_MAP && depth == 0) {
        /* 深度 0：字典 → 值数组（不递归） */
        Value vals = lumin_map_values(v);
        for(int i = 0; i < vals.v.array.len; i++) flat_push(&b, vals.v.array.items[i]);
    } else {
        flat_rec(v, depth, &b);
    }
    Value r = val_array(b.len);
    for(int i = 0; i < b.len; i++) r.v.array.items[i] = b.items[i];
    free(b.items);
    return r;
}

// ===== addAll：数组追加 / 字典合并 =====
Value lumin_array_addall(Value a, Value b) {
    if(a.type == VAL_ARRAY && b.type == VAL_ARRAY) {
        int n = a.v.array.len + b.v.array.len;
        Value r = val_array(n);
        for(int i = 0; i < a.v.array.len; i++) r.v.array.items[i] = val_clone(&a.v.array.items[i]);
        for(int i = 0; i < b.v.array.len; i++) r.v.array.items[a.v.array.len + i] = val_clone(&b.v.array.items[i]);
        return r;
    }
    if(a.type == VAL_MAP && b.type == VAL_MAP) {
        /* 引用语义：原地合并，返回 a（与 set/clear 一致） */
        MapIter it; map_iter_init(&it, b.v.map);
        Value k, vv;
        while(map_iter_next(&it, &k, &vv))
            lumin_map_set(&a, k, vv);
        return a;
    }
    runtime_error("addAll() 参数类型不匹配：数组+数组 或 字典+字典");
    return a;
}
