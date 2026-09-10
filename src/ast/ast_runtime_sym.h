#ifndef AST_RUNTIME_SYM_H
#define AST_RUNTIME_SYM_H

#include "lumyr_value.h"

#define SYM_INITIAL_CAP 64

/* 运行时符号表（全局变量/函数）：动态扩容，无硬上限 */
extern char** sym_names;
extern Value* sym_vals;
extern int sym_cnt;
extern int sym_cap;

void sym_ensure(int need);
void sym_set(const char* n, Value v);
Value sym_get(const char* n);
Value* sym_get_ptr(const char* n);
_Bool sym_has(const char* n);
double val_to_num(Value v);
int value_equal(Value a, Value b);
char* lumyr_concat(const char* s1, const char* s2);


#endif
