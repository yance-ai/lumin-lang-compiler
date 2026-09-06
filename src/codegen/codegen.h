#ifndef CODEGEN_H
#define CODEGEN_H

#include "ast/ast.h"

void codegen_generate(AstNode* root, const char* out_c_path);

#endif
