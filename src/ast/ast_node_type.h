//
// Created by kai on 2026/9/5.
//

#ifndef AST_NODE_TYPE_H
#define AST_NODE_TYPE_H
#include "lumyr_types.h"
#include "lumyr_value_type.h"

typedef struct AstNode AstNode;

struct AstNode {
    AstType type;
    ValueType val_type;  // 该节点表达式的类型，语义分析后填充
    int line;            // 源码行号（lexer yylineno，解析时填充；报错定位用）
    union {
        long long inum;           // AST_INT
        double num;               // AST_NUM
        _Bool bval;               // AST_BOOL
        char ch;
        char* sval;               // AST_STRING，字符串字面量，strdup堆分配
        char* varname;            // AST_VAR
        struct {
            BinOp op;
            AstNode* left;
            AstNode* right;
        } bin;
        struct {
            BinOp op;             // OpCode，复用BinOp别名
            AstNode* child;
        } uny;
        struct {
            char* varname;
            AstNode* expr;
        } assign;
        struct {
            AstNode* expr;
        } print;
        struct {
            AstNode* first;
            AstNode* second;
        } seq;
        struct {
            AstNode* cond;        // if条件
            AstNode* then_stmt;   // if分支
            AstNode* elif_chain;  // elseif链：AST_IF链表
            AstNode* else_stmt;   // else分支，可以NULL
        } ifnode;
        struct {
            AstNode* stmts;
        } block;
        struct {
            AstNode* cond;
            AstNode* if_body;
            AstNode* elif_list;   // AST_ELIF* 链表
            AstNode* else_body;
        } if_chain;
        struct {
            AstNode* cond;
            AstNode* body;
            AstNode* next;
        } elif;
        struct {
            AstNode* cond;
            AstNode* body;
        } while_node;
        struct {
            char* name;
            AstNode* args;
        } annotation;
        struct {
            AstNode* init;
            AstNode* cond;
            AstNode* update;
            AstNode* body;
        } for_node;
        struct {
            int cast_type;
            struct AstNode* child;
        } cast;
        struct {
            AstNode* ret_val;
        } ret;
        struct {
            AstNode* cond;
            AstNode* true_expr;
            AstNode* false_expr;
        } ternary;
        struct {
            AstNode* cond;
            AstNode* cases;
        } sw;
        struct {
            AstNode* const_val;
            AstNode* body;
            int is_default;
            AstNode* next;
        } cs;

        struct {
            char* name;
            AstNode* params;   // AST_PARAM链表
            AstNode* body;     // 函数体 block
            AstNode* annotations;  // 注解列表（AST_SEQ 链，每个节点是 AST_ANNOTATION）
        } func_def;

        struct {
            char* name;
            int is_ellipsis; // 1=...args可变参数，只能最后一个
            AstNode* default_val; // 默认值表达式，NULL=无默认值
            AstNode* next;   // 参数链表
        } param;

        struct {
            char* name;
            AstNode* args;   // 实参链表
        } call;

        struct {
            AstNode* callee; // 被调用的表达式（函数值，如 f(1) 的结果）
            AstNode* args;   // 实参链表
        } dyn_call;

        struct {
            AstNode* arr;    // 数组表达式
            AstNode* idx;    // 下标表达式
        } index;

        struct {
            AstNode* arr;    // 数组表达式
            AstNode* idx;    // 下标表达式
            AstNode* value;  // 赋值表达式
        } index_assign;

        struct {
            AstNode* elems;  // AST_SEQ 链：数组元素
        } array_lit;
        struct {
            AstNode* entries;  // AST_SEQ 链：AST_MAP_ENTRY
        } map_lit;
        struct {
            AstNode* key;    // 键表达式（字符串）
            AstNode* value;  // 值表达式
        } map_entry;
        struct {
            AstNode* body;       // try 块
            char* catch_var;     // catch (e) 的变量名（可为 NULL：try-finally 无 catch）
            AstNode* catch_body; // catch 块（可为 NULL）
            AstNode* finally_body; // finally 块（可为 NULL）
        } trynode;
        struct {
            AstNode* expr;       // throw 的值表达式
        } thrownode;
        struct {
            char** names;        // 解构变量名数组
            int count;           // 变量个数
            AstNode* rhs;        // 右值表达式（应为数组）
        } destruct;
        struct {
            AstNode* expr;       // 被展开的表达式（数组或 map）
        } spread;
    } u;
};


#endif //AST_NODE_TYPE_H
