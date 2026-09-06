#ifndef AST_SYMTAB_H
#define AST_SYMTAB_H

#include "lumin_types.h"

#define STATIC_SYM_MAX 128

typedef struct {
    char* name;
    ValueType ty;
} SymStaticEntry;

extern SymStaticEntry static_sym_table[STATIC_SYM_MAX];
extern int static_sym_count;

void static_sym_reset(void);
int static_sym_put(const char* name, ValueType ty);
int static_sym_get(const char* name, ValueType* out_ty);

#endif
