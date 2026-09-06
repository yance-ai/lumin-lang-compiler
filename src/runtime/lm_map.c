// lm_map.c —— 字典（哈希表）内置函数
#include "lm_map.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>



int lumin_map_find(const ValueMap* m, const char* key) {
    for(int i = 0; i < m->len; i++)
        if(strcmp(m->keys[i], key) == 0) return i;
    return -1;
}

static void map_reserve(ValueMap* m, int need) {
    if(need <= m->cap) return;
    int ncap = m->cap ? m->cap : 8;
    while(ncap < need) ncap *= 2;
    m->keys = (char**)realloc(m->keys, sizeof(char*) * ncap);
    m->values = (Value*)realloc(m->values, sizeof(Value) * ncap);
    m->cap = ncap;
}

// val_map() 定义在 ast/lumin_value.c（与 val_array 同层，供 stackframe_test 等链接）

// 下标写：d["k"] = v（原地改共享对象；ValueMap 是堆上指针，所有引用共享）

void lumin_map_set(Value* map, Value key, Value val) {
    if(map->type != VAL_MAP || key.type != VAL_STRING)
        runtime_error("字典下标写需要 字典[字符串键]");
    ValueMap* m = map->v.map;
    int i = lumin_map_find(m, key.v.s);
    if(i >= 0) {
        val_destroy(&m->values[i]);
        m->values[i] = val_clone(&val);
    } else {
        map_reserve(m, m->len + 1);
        m->keys[m->len] = strdup(key.v.s);
        m->values[m->len] = val_clone(&val);
        m->len++;
    }
}

// 下标读：d["k"]；键不存在 → null

Value lumin_map_get(Value map, Value key) {
    if(map.type != VAL_MAP || key.type != VAL_STRING)
        runtime_error("字典下标读需要 字典[字符串键]");
    ValueMap* m = map.v.map;
    int i = lumin_map_find(m, key.v.s);
    if(i < 0) return val_none();
    return m->values[i];
}

int lumin_map_has(Value map, const char* key) {
    if(map.type != VAL_MAP) return 0;
    return lumin_map_find(map.v.map, key) >= 0;
}

// del(d, "k") → 新字典（去掉该键；键不存在 → 原样拷贝）

Value lumin_map_del(Value map, const char* key) {
    if(map.type != VAL_MAP) runtime_error("del() 参数必须是数组或字典");
    ValueMap* src = map.v.map;
    Value r = val_map();
    ValueMap* dst = r.v.map;
    map_reserve(dst, src->len);
    for(int i = 0; i < src->len; i++) {
        if(strcmp(src->keys[i], key) == 0) continue;
        dst->keys[dst->len] = strdup(src->keys[i]);
        dst->values[dst->len] = val_clone(&src->values[i]);
        dst->len++;
    }
    return r;
}

Value lumin_map_keys(Value map) {
    if(map.type != VAL_MAP) runtime_error("keys() 参数必须是字典");
    ValueMap* m = map.v.map;
    Value r = val_array(m->len);
    for(int i = 0; i < m->len; i++)
        r.v.array.items[i] = lumin_make_string(m->keys[i]);
    return r;
}

Value lumin_map_values(Value map) {
    if(map.type != VAL_MAP) runtime_error("values() 参数必须是字典");
    ValueMap* m = map.v.map;
    Value r = val_array(m->len);
    for(int i = 0; i < m->len; i++)
        r.v.array.items[i] = val_clone(&m->values[i]);
    return r;
}

// OPC_MAP_LIT：栈上 2n 个值（键、值交替）构造字典
// 注意：lumin_map_set 返回 val（下标写表达式值），此处必须原地改 r

Value lumin_map_lit(Value* kv, int n) {
    Value r = val_map();
    for(int i = 0; i < n; i++) {
        Value k = kv[i * 2];
        Value v = kv[i * 2 + 1];
        if(k.type != VAL_STRING) runtime_error("字典字面量的键必须是字符串");
        lumin_map_set(&r, k, v);
    }
    return r;
}
