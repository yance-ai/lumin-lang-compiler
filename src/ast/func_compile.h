#ifndef FUNC_COMPILE_H
#define FUNC_COMPILE_H
#include "lumin_value_type.h"
#include "ast_node_type.h"

// 将AST_FUNC_DEF节点编译生成RuntimeFunc（不持有AstNode，内部提取信息生成IR/解释器句柄）
RuntimeFunc* compile_func_from_ast(AstNode* func_def_ast);

// 销毁RuntimeFunc（不碰AST）
void runtime_func_destroy(RuntimeFunc* f);

// ---- 解释器 payload 访问（AST_CALL 参数绑定用）----
// capture_count == -1 标记解释器 payload 模式
_Bool interp_func_is_payload(const RuntimeFunc* rf);
int interp_func_param_cnt(const RuntimeFunc* rf);
_Bool interp_func_has_variadic(const RuntimeFunc* rf);
// 参数名（普通参数在前，可变参数最后），索引越界返回 NULL
const char* interp_func_param_name(const RuntimeFunc* rf, int idx);

// ---- 解释器 entry 需要知道自己对应的 RuntimeFunc（payload 在 captures 里）----
// 函数调用前设置当前被调函数，entry 入口读取；返回旧值用于调用后恢复
RuntimeFunc* interp_set_current_rf(RuntimeFunc* rf);
RuntimeFunc* interp_current_rf(void);

#endif
