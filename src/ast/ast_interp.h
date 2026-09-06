//
// Created by kai on 2026/9/5.
//

#ifndef AST_INTERP_H
#define AST_INTERP_H
#include "lumin_value_type.h"
#include "ast_node_type.h"

Value make_int(long long i);
Value make_double(double d);
Value make_bool(_Bool b);
Value make_string(const char* s);
Value make_char(char ch);
Value make_nil(void);
_Bool value_to_bool(Value v);

// 顶层入口：内部构造 EvalCtx + 顶层栈帧，执行后销毁
Value ast_eval(AstNode* node);

// 核心求值：frame 一路透传。
// 函数调用时由调用方新建 callee 帧作为 frame，执行完销毁。
Value ast_eval_ctx(AstNode* node, EvalCtx* ctx, StackFrame* frame);


#endif //AST_INTERP_H
