#ifndef STACKFRAME_H
#define STACKFRAME_H

#include "lumin_value_type.h"

// 新建栈帧：parent 为调用者栈帧，可为 NULL（顶层帧）
StackFrame* stackframe_new(StackFrame* parent);

// 销毁栈帧：释放帧内持有的字符串/数组等资源
// 注意：VAL_FUNC 为引用语义（函数对象生命周期由注册方管理），帧内不销毁
void stackframe_destroy(StackFrame* f);

// 查找变量：从当前帧向上沿 parent 链查找，返回 Value*；找不到返回 NULL
Value* stackframe_get(StackFrame* f, const char* name);

// 赋值语义：沿链查找，找到则原地更新；找不到则在当前帧新建
// 返回最终存储位置的指针
Value* stackframe_set(StackFrame* f, const char* name, Value v);

// 绑定语义（参数绑定用）：只在当前帧查找/创建，不向上查找，遮蔽父帧同名变量
Value* stackframe_bind(StackFrame* f, const char* name, Value v);

#endif //STACKFRAME_H
