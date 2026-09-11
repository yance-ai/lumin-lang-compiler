#ifndef AST_TYPES_H
#define AST_TYPES_H

#include "lumyr_value_type.h"

// type 声明（提前声明对象属性）与枚举声明
// type Person { name: string, age: int }   → 形状表（属性名 + 期望类型）
// enum Color { RED, GREEN }                → 全局 map {RED:"RED", GREEN:"GREEN"}
//
// 类型表为编译期全局注册表：yacc 声明时注册，typecheck 校验构造调用，
// ir_compile 把 Person(...) 构造展开为 map 字面量（属性按序 + 期望类型强转）

typedef struct {
    char* name;          // 类型名
    char** props;        // 属性名（按声明序）
    ValueType* ptypes;   // 属性期望类型（VAL_INT/VAL_STRING/...，VAL_NONE=未标注）
    int nprops;
    char** generic_params;  // 泛型参数名（NULL=非泛型类型）
    int generic_param_count; // 泛型参数数量
} TypeDef;

// 注册 / 查找（返回下标，-1 未找到）
int type_register(const char* name, char** props, ValueType* ptypes, int nprops, char** generic_params, int generic_param_count);
int type_lookup(const char* name);
TypeDef* type_get(int idx);

// 属性类型名（string/int/double/bool/char/ascii/byte）→ ValueType；未知返回 VAL_NONE
ValueType type_name_to_valtype(const char* tname);
char* valtype_to_name(ValueType vt);
ValueType castkind_to_valtype(int ck);

/* ===== 接口/trait 系统 ===== */
// interface Printable { func to_string(): string }
// 接口表为编译期全局注册表：yacc 声明时注册，typecheck 校验类型是否实现接口

typedef struct {
    char* name;           // 方法名
    char* return_type;    // 返回类型名（NULL=无返回值/void）
} InterfaceMethod;

typedef struct {
    char* name;              // 接口名
    InterfaceMethod* methods; // 方法签名列表
    int nmethods;
} InterfaceDef;

// 注册 / 查找（返回下标，-1 未找到）
int interface_register(const char* name, void* methods);
int interface_lookup(const char* name);
InterfaceDef* interface_get(int idx);

// 检查类型是否实现了接口（鸭子类型：检查类型是否有接口要求的所有方法）
int type_implements_interface(const char* type_name, const char* interface_name);

#endif //AST_TYPES_H
