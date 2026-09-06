#ifndef AST_RUNTIME_SYM_H
#define AST_RUNTIME_SYM_H

#include "lumin_value.h"

#define SYM_MAX 64


extern char* sym_names[SYM_MAX];
extern Value sym_vals[SYM_MAX];
extern int sym_cnt;

void sym_set(const char* n, Value v);
Value sym_get(const char* n);
Value* sym_get_ptr(const char* n);
_Bool sym_has(const char* n);
double val_to_num(Value v);
int value_equal(Value a, Value b);
char* lumin_concat(const char* s1, const char* s2);


#endif
