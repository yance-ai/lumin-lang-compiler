// lm_map.h —— 字典（哈希表）内置函数
#ifndef LM_MAP_H
#define LM_MAP_H

#include "ast/lumin_value_type.h"

int   lumin_map_find(const ValueMap* m, Value key); // 线性扫描键位置，-1=无
void  lumin_map_set(Value* map, Value key, Value val);      // d[k] = v（原地，传指针，任意类型键）
Value lumin_map_get(Value map, Value key);                  // d[k]；缺键 → null
int   lumin_map_has(Value map, Value key);                  // 键是否存在
Value lumin_map_del(Value map, Value key);                  // 删键，返回新字典
Value lumin_map_keys(Value map);                            // keys(d) → 键数组（任意类型）
Value lumin_map_values(Value map);                          // values(d) → 值数组
Value lumin_map_lit(Value* kv, int n);                      // OPC_MAP_LIT：键值交替构造

// ===== 迭代器（遍历所有键值对，统一链表/红黑树） =====
typedef struct {
    ValueMap* map;
    int bucket_idx;
    MapEntry* entry;           // 当前链表节点
    int in_tree;               // 当前桶是否是红黑树
    MapEntry* tree_stack[256]; // 红黑树中序遍历栈
    int tree_top;
} MapIter;

void map_iter_init(MapIter* it, ValueMap* m);
int  map_iter_next(MapIter* it, Value* key, Value* val);  // 1=有数据, 0=结束
void entry_free(MapEntry* e);  // 释放单个条目（键值销毁+内存释放）

#endif //LM_MAP_H
