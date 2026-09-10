//
// lm_io.h —— 文件 IO 内置函数
//

#ifndef LUMYR_LANG_COMPILER_LM_IO_H
#define LUMYR_LANG_COMPILER_LM_IO_H

#include "lm_value.h"

Value lumyr_read_file(Value* args, int n);    // read_file(path) → 文件内容字符串
Value lumyr_write_file(Value* args, int n);   // write_file(path, content) → 覆盖写入
Value lumyr_file_exists(Value* args, int n);  // file_exists(path) → bool

#endif //LUMYR_LANG_COMPILER_LM_IO_H
