#ifndef LUMYR_MACRO_H
#define LUMYR_MACRO_H

#include "ast/ast_node_type.h"

// 宏表管理：注册、查找、展开
// 宏展开发生在语法分析阶段，将宏调用替换为展开后的 AST 节点

// 注册宏（参数名列表和 body AST）
void macro_register(const char* name, AstNode* params, AstNode* body);

// 查找宏，未找到返回 NULL
AstNode* macro_lookup(const char* name);

// 展开宏：将 body 中的参数名替换为实际参数（深拷贝），返回展开后的 AST 节点
// args 是 AST_SEQ 链表，每个节点是实际参数表达式
AstNode* macro_expand(AstNode* macro_def, AstNode* args);

// 检查名称是否是已注册的宏
int macro_is_defined(const char* name);

#endif
