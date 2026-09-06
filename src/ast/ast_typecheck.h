
#ifndef AST_TYPECHECK_H
#define AST_TYPECHECK_H
#include "ast_node_type.h"

int ast_typecheck(AstNode* node);
int typecheck_expr(AstNode* node);


#endif //AST_TYPECHECK_H
