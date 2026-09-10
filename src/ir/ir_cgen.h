#ifndef LUMYR_IR_CGEN_H
#define LUMYR_IR_CGEN_H

#include "bytecode.h"

// 从 IR（字节码）生成 C 文件（函数 + main），语义与解释器一致
void ir_cgen_file(const char* out_c_path, BytecodeFunc* main_fn);

#endif // LUMYR_IR_CGEN_H
