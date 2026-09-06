#ifndef LUMIN_TYPES_H
#define LUMIN_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include "lumin_value.h"

// AST节点类型
typedef enum {
    AST_INT,
    AST_NUM,
    AST_BOOL,
    AST_CHAR,
    AST_STRING,
    AST_NONE,   // null 字面量
    AST_FUNCREF,  // 函数名引用（函数作为值）
    AST_VAR,
    AST_BINOP,
    AST_UNARY,
    AST_ASSIGN,
    AST_PRINT,
    AST_SEQ,
    AST_IF,
    AST_BLOCK,
    AST_IF_CHAIN,
    AST_ELIF,
    AST_WHILE,
    AST_FOR,
    AST_CAST,
    AST_TERNARY,
    AST_SWITCH,
    AST_CASE,
    AST_BREAK,
    AST_CONTINUE,
    AST_RETURN,
    AST_FUNC_DEF,      // 函数定义 func f(a,...args){}
    AST_PARAM,         // 形参节点（普通 / ...可变）
    AST_CALL,          // 函数调用 f(1,2,3)
    AST_DYN_CALL,      // 动态调用链 f(1)(2)：callee 是表达式（函数值）
    AST_INDEX,         // 数组下标读 a[i]
    AST_INDEX_ASSIGN,  // 数组下标写 a[i] = v
    AST_ARRAY_LIT,     // 数组字面量 [1,2,3]
    AST_MAP_LIT,       // 字典字面量 {"k": v, ...}
    AST_MAP_ENTRY,     // 字典字面量的一项（键表达式 + 值表达式）
} AstType;

// 二元运算符
typedef enum {
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_GT,
    OP_LT,
    OP_GE,
    OP_LE,
    OP_EQ,
    OP_NE,
    OP_LOGIC_AND,
    OP_LOGIC_OR,
    OP_LOGIC_NOT,
    OP_PRE_INC,
    OP_POST_INC,
    OP_PRE_DEC,
    OP_POST_DEC,
    OP_UNARY_PLUS,
    OP_UNARY_MINUS,
} BinOp;

// 辅助：ValueType → C源码字符串
static inline const char* valtype_to_cstr(ValueType t)
{
    switch(t){
        case VAL_INT:      return "long long";
        case VAL_DOUBLE:   return "double";
        case VAL_BOOL:     return "_Bool";
        case VAL_STRING:   return "char*";
        case VAL_ARRAY:    return "void*";
        case VAL_CHAR:     return "char";
        default:           return "double";
    }
}

#endif
