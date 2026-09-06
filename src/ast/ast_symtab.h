#ifndef AST_SYMTAB_H
#define AST_SYMTAB_H

#include "lumin_types.h"

#define STATIC_SYM_INITIAL_CAP 128

typedef struct {
    char* name;
    ValueType ty;
} SymStaticEntry;

/* 静态符号表：动态扩容，无硬上限 */
extern SymStaticEntry* static_sym_table;
extern int static_sym_count;
extern int static_sym_cap;

void static_sym_ensure(int need);

void static_sym_reset(void);
int static_sym_put(const char* name, ValueType ty);
int static_sym_get(const char* name, ValueType* out_ty);

#endif
