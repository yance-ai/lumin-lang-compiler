#ifndef LUMIN_IR_VM_H
#define LUMIN_IR_VM_H

#include "bytecode.h"
#include "lumin_value_type.h"

// 顶层入口：建顶层栈帧执行 main 字节码
Value vm_run_main(BytecodeFunc* main_fn);

// FuncEntry 签名入口（注册到 RuntimeFunc.entry，由 OP_CALL 调用）
Value vm_func_entry(int arg_cnt, const Value* args, EvalCtx* ctx, StackFrame* frame);

#endif // LUMIN_IR_VM_H
