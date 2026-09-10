#include "ast_runtime_sym.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char** sym_names = NULL;
Value* sym_vals = NULL;
int sym_cnt = 0;
int sym_cap = 0;

/* 容量不足时翻倍扩容（realloc；表项只被下标访问，无外部持有指针） */
void sym_ensure(int need)
{
    if(need <= sym_cap) return;
    int newcap = sym_cap > 0 ? sym_cap : SYM_INITIAL_CAP;
    while(newcap < need) newcap *= 2;
    char** nn = (char**)realloc(sym_names, (size_t)newcap * sizeof(char*));
    if(!nn) { fprintf(stderr, "变量数量超限（内存不足）\n"); exit(EXIT_FAILURE); }
    sym_names = nn;
    Value* nv = (Value*)realloc(sym_vals, (size_t)newcap * sizeof(Value));
    if(!nv) { fprintf(stderr, "变量数量超限（内存不足）\n"); exit(EXIT_FAILURE); }
    sym_vals = nv;
    sym_cap = newcap;
}


static int sym_lookup(const char* n) {
    for(int i = 0; i < sym_cnt; ++i) {
        if(strcmp(sym_names[i], n) == 0) return i;
    }
    return -1;
}


void sym_set(const char* n, Value v) {
    int idx = sym_lookup(n);
    if(idx >= 0) {
        if(sym_vals[idx].type == VAL_STRING && !sym_vals[idx].str_inline) {
            free(sym_vals[idx].v.s);
        }
        sym_vals[idx] = v;
        return;
    }
    sym_ensure(sym_cnt + 1);
    sym_names[sym_cnt] = strdup(n);
    sym_vals[sym_cnt] = v;
    sym_cnt++;
}

Value sym_get(const char* n) {
    int idx = sym_lookup(n);
    if(idx < 0) {
        fprintf(stderr,"未定义变量: %s\n",n);
        exit(EXIT_FAILURE);
    }
    return sym_vals[idx];
}

Value* sym_get_ptr(const char* n) {
    int idx = sym_lookup(n);
    if(idx < 0) {
        fprintf(stderr,"未定义变量: %s\n",n);
        exit(EXIT_FAILURE);
    }
    return &sym_vals[idx];
}

_Bool sym_has(const char* n) {
    return sym_lookup(n) >= 0;
}

double val_to_num(Value v) {
    if(v.type == VAL_INT) return (double)v.v.i;
    if(v.type == VAL_DOUBLE) return v.v.d;
    if(v.type == VAL_CHAR) return (double)(unsigned char)v.v.c;
    return 0.0;
}

int value_equal(Value a, Value b) {
    if(a.type != b.type) {
        if ((a.type == VAL_CHAR && b.type == VAL_INT)){
            return ((long long)(unsigned char)a.v.c) == b.v.i;
        }
        if ((a.type == VAL_INT && b.type == VAL_CHAR)){
            return a.v.i == (long long)(unsigned char)b.v.c;
        }
        return 0;
    }
    switch(a.type){
        case VAL_INT:     return a.v.i == b.v.i;
        case VAL_DOUBLE:  return a.v.d == b.v.d;
        case VAL_BOOL:    return a.v.b == b.v.b;
        case VAL_CHAR:    return a.v.c == b.v.c;
        case VAL_STRING:  return strcmp(lumyr_str_cstr(&a), lumyr_str_cstr(&b)) == 0;
        default: return 0;
    }
}

char* lumyr_concat(const char* s1, const char* s2) {
    size_t l1 = strlen(s1);
    size_t l2 = strlen(s2);
    char* out = malloc(l1 + l2 + 1);
    memcpy(out, s1, l1);
    memcpy(out+l1, s2, l2);
    out[l1+l2] = '\0';
    return out;
}
