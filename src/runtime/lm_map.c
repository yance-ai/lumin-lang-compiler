// lm_map.c —— 字典（哈希表）内置函数，支持任意类型键
#include "lm_map.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

// 键比较：支持 int/double/bool/char/string/byte/自定义type(map)
static int value_key_eq(Value a, Value b) {
    if(a.type != b.type) {
        // int/byte 互通比较
        if((a.type == VAL_INT || a.type == VAL_BYTE) &&
           (b.type == VAL_INT || b.type == VAL_BYTE))
            return a.v.i == b.v.i;
        return 0;
    }
    switch(a.type) {
        case VAL_INT: case VAL_BYTE: return a.v.i == b.v.i;
        case VAL_DOUBLE: return a.v.d == b.v.d;
        case VAL_BOOL: return a.v.b == b.v.b;
        case VAL_CHAR: return a.v.c == b.v.c;
        case VAL_STRING: return strcmp(a.v.s, b.v.s) == 0;
        case VAL_MAP: {
            // 自定义 type 对象：比较 __classname__ + 所有属性
            if(a.v.map->len != b.v.map->len) return 0;
            for(int i = 0; i < a.v.map->len; i++) {
                Value ka = a.v.map->keys[i];
                int j = lumin_map_find(b.v.map, ka);
                if(j < 0) return 0;
                if(!value_key_eq(a.v.map->values[i], b.v.map->values[j])) return 0;
            }
            return 1;
        }
        default: return 0;
    }
}

int lumin_map_find(const ValueMap* m, Value key) {
    for(int i = 0; i < m->len; i++)
        if(value_key_eq(m->keys[i], key)) return i;
    return -1;
}

static void map_reserve(ValueMap* m, int need) {
    if(need <= m->cap) return;
    int ncap = m->cap ? m->cap : 8;
    while(ncap < need) ncap *= 2;
    m->keys = (Value*)realloc(m->keys, sizeof(Value) * ncap);
    m->values = (Value*)realloc(m->values, sizeof(Value) * ncap);
    m->cap = ncap;
}

// val_map() 定义在 ast/lumin_value.c（与 val_array 同层，供 stackframe_test 等链接）

// 下标写：d[k] = v（原地改共享对象；ValueMap 是堆上指针，所有引用共享）

void lumin_map_set(Value* map, Value key, Value val) {
    if(map->type != VAL_MAP)
        runtime_error("字典下标写需要 字典[键]");
    ValueMap* m = map->v.map;
    int i = lumin_map_find(m, key);
    if(i >= 0) {
        val_destroy(&m->values[i]);
        m->values[i] = val_clone(&val);
    } else {
        map_reserve(m, m->len + 1);
        m->keys[m->len] = val_clone(&key);
        m->values[m->len] = val_clone(&val);
        m->len++;
    }
}

// 下标读：d[k]；键不存在 → null

Value lumin_map_get(Value map, Value key) {
    if(map.type != VAL_MAP)
        runtime_error("字典下标读需要 字典[键]");
    ValueMap* m = map.v.map;
    int i = lumin_map_find(m, key);
    if(i < 0) return val_none();
    return m->values[i];
}

int lumin_map_has(Value map, Value key) {
    if(map.type != VAL_MAP) return 0;
    return lumin_map_find(map.v.map, key) >= 0;
}

// del(d, k) → 新字典（去掉该键；键不存在 → 原样拷贝）

Value lumin_map_del(Value map, Value key) {
    if(map.type != VAL_MAP) runtime_error("del() 参数必须是数组或字典");
    ValueMap* src = map.v.map;
    Value r = val_map();
    ValueMap* dst = r.v.map;
    map_reserve(dst, src->len);
    for(int i = 0; i < src->len; i++) {
        if(value_key_eq(src->keys[i], key)) continue;
        dst->keys[dst->len] = val_clone(&src->keys[i]);
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
        r.v.array.items[i] = val_clone(&m->keys[i]);
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

Value lumin_map_lit(Value* kv, int n) {
    Value r = val_map();
    for(int i = 0; i < n; i++) {
        Value k = kv[i * 2];
        Value v = kv[i * 2 + 1];
        lumin_map_set(&r, k, v);
    }
    return r;
}
