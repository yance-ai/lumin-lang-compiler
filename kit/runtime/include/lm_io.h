//
// lm_io.h —— 文件 IO 内置函数
//

#ifndef LUMIN_LANG_COMPILER_LM_IO_H
#define LUMIN_LANG_COMPILER_LM_IO_H

#include "lm_value.h"

Value lumin_read_file(Value* args, int n);    // read_file(path) → 文件内容字符串
Value lumin_write_file(Value* args, int n);   // write_file(path, content) → 覆盖写入
Value lumin_file_exists(Value* args, int n);  // file_exists(path) → bool

#endif //LUMIN_LANG_COMPILER_LM_IO_H
