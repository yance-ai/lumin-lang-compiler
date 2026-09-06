#include "bytecode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

BytecodeFunc* bytecode_func_new(const char* name, int is_main)
{
    BytecodeFunc* fn = (BytecodeFunc*)calloc(1, sizeof(BytecodeFunc));
    if(!fn) { perror("bytecode_func_new"); exit(EXIT_FAILURE); }
    fn->name = name ? strdup(name) : NULL;
    fn->is_main = is_main;
    return fn;
}

void bytecode_func_free(BytecodeFunc* fn)
{
    if(!fn) return;
    free((void*)fn->name);
    free(fn->code);
    for(int i = 0; i < fn->sym_cnt; i++) free(fn->syms[i]);
    free(fn->syms);
    for(int i = 0; i < fn->const_cnt; i++) val_destroy(&fn->consts[i]);
    free(fn->consts);
    for(int i = 0; i < fn->param_cnt + fn->has_variadic; i++) free(fn->params[i]);
    free(fn->params);
    free(fn);
}

int bf_sym(BytecodeFunc* fn, const char* name)
{
    for(int i = 0; i < fn->sym_cnt; i++) {
        if(strcmp(fn->syms[i], name) == 0) return i;
    }
    if(fn->sym_cnt >= fn->sym_cap) {
        fn->sym_cap = fn->sym_cap ? fn->sym_cap * 2 : 16;
        fn->syms = (char**)realloc(fn->syms, sizeof(char*) * fn->sym_cap);
        if(!fn->syms) { perror("bf_sym"); exit(EXIT_FAILURE); }
    }
    fn->syms[fn->sym_cnt] = strdup(name);
    return fn->sym_cnt++;
}

static int const_equal(Value a, Value b)
{
    if(a.type != b.type) return 0;
    switch(a.type) {
        case VAL_INT:    return a.v.i == b.v.i;
        case VAL_DOUBLE: return a.v.d == b.v.d;
        case VAL_BOOL:   return a.v.b == b.v.b;
        case VAL_CHAR:   return a.v.c == b.v.c;
        case VAL_STRING: return strcmp(a.v.s, b.v.s) == 0;
        default:         return 0;
    }
}

int bf_const(BytecodeFunc* fn, Value v)
{
    for(int i = 0; i < fn->const_cnt; i++) {
        if(const_equal(fn->consts[i], v)) return i;
    }
    if(fn->const_cnt >= fn->const_cap) {
        fn->const_cap = fn->const_cap ? fn->const_cap * 2 : 16;
        fn->consts = (Value*)realloc(fn->consts, sizeof(Value) * fn->const_cap);
        if(!fn->consts) { perror("bf_const"); exit(EXIT_FAILURE); }
    }
    fn->consts[fn->const_cnt] = val_clone(&v);   // 常量池深拷贝持有
    return fn->const_cnt++;
}

void bf_emit(BytecodeFunc* fn, OpCode op, int a, int b)
{
    if(fn->code_len >= fn->code_cap) {
        fn->code_cap = fn->code_cap ? fn->code_cap * 2 : 32;
        fn->code = (Instruction*)realloc(fn->code, sizeof(Instruction) * fn->code_cap);
        if(!fn->code) { perror("bf_emit"); exit(EXIT_FAILURE); }
    }
    fn->code[fn->code_len].op = op;
    fn->code[fn->code_len].a = a;
    fn->code[fn->code_len].b = b;
    fn->code_len++;
}

int bf_emit_here(BytecodeFunc* fn, OpCode op, int a, int b)
{
    int pos = fn->code_len;
    bf_emit(fn, op, a, b);
    return pos;
}

void bf_patch(BytecodeFunc* fn, int pos, int target)
{
    if(pos < 0 || pos >= fn->code_len) {
        fprintf(stderr, "bf_patch: 越界 pos=%d len=%d\n", pos, fn->code_len);
        exit(EXIT_FAILURE);
    }
    fn->code[pos].a = target;
}
