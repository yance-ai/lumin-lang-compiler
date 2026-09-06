// lm_map.h —— 字典（哈希表）内置函数
#ifndef LM_MAP_H
#define LM_MAP_H

#include "lm_value.h"

int   lumin_map_find(const ValueMap* m, const char* key);  // 线性扫描键位置，-1=无
void  lumin_map_set(Value* map, Value key, Value val);      // d["k"] = v（原地，传指针）
Value lumin_map_get(Value map, Value key);                  // d["k"]；缺键 → null
int   lumin_map_has(Value map, const char* key);            // 键是否存在
Value lumin_map_del(Value map, const char* key);            // 删键，返回新字典
Value lumin_map_keys(Value map);                            // keys(d) → 字符串数组
Value lumin_map_values(Value map);                          // values(d) → 值数组
Value lumin_map_lit(Value* kv, int n);                      // OPC_MAP_LIT：键值交替构造

#endif //LM_MAP_H
