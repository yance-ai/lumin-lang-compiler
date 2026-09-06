#include "ast_symtab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SymStaticEntry* static_sym_table = NULL;
int static_sym_count = 0;
int static_sym_cap = 0;

/* 容量不足时翻倍扩容 */
void static_sym_ensure(int need)
{
    if(need <= static_sym_cap) return;
    int newcap = static_sym_cap > 0 ? static_sym_cap : STATIC_SYM_INITIAL_CAP;
    while(newcap < need) newcap *= 2;
    SymStaticEntry* nt = (SymStaticEntry*)realloc(static_sym_table, (size_t)newcap * sizeof(SymStaticEntry));
    if(!nt) { fprintf(stderr, "静态符号表溢出（内存不足）\n"); exit(EXIT_FAILURE); }
    static_sym_table = nt;
    static_sym_cap = newcap;
}

void static_sym_reset(void)
{
    for(int i = 0; i < static_sym_count; ++i) {
        free(static_sym_table[i].name);
    }
    static_sym_count = 0;
}

int static_sym_put(const char* name, ValueType ty)
{
    for(int i = 0; i < static_sym_count; ++i) {
        if(strcmp(static_sym_table[i].name, name) == 0) {
            static_sym_table[i].ty = ty;
            return 1;
        }
    }
    static_sym_ensure(static_sym_count + 1);
    static_sym_table[static_sym_count].name = strdup(name);
    static_sym_table[static_sym_count].ty = ty;
    static_sym_count++;
    return 1;
}

int static_sym_get(const char* name, ValueType* out_ty)
{
    for(int i = 0; i < static_sym_count; ++i) {
        if(strcmp(static_sym_table[i].name, name) == 0) {
            *out_ty = static_sym_table[i].ty;
            return 1;
        }
    }
    return 0;
}
