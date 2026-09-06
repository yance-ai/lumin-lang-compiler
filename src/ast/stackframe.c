#include "stackframe.h"
#include "lumin_value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 释放一个帧内槽位的资源。
// 函数值（VAL_FUNC）是引用语义：帧不拥有 RuntimeFunc，销毁会误伤共享对象，
// 因此跳过，只清空类型标记。
static void slot_release(Value* v) {
    if(!v) return;
    if(v->type == VAL_FUNC) {
        v->type = VAL_NONE;
        return;
    }
    val_destroy(v);
}

StackFrame* stackframe_new(StackFrame* parent)
{
    StackFrame* f = (StackFrame*)calloc(1, sizeof(StackFrame));
    if(!f) {
        perror("stackframe_new");
        exit(EXIT_FAILURE);
    }
    f->cnt = 0;
    f->parent = parent;
    return f;
}

void stackframe_destroy(StackFrame* f)
{
    if(!f) return;
    for(int i = 0; i < f->cnt; i++) {
        free(f->names[i]);
        slot_release(&f->vals[i]);
    }
    free(f);
}

// 沿 parent 链查找，返回所在帧与槽位下标；找不到返回 -1
static int find_in_chain(StackFrame* f, const char* name, StackFrame** owner)
{
    for(StackFrame* p = f; p; p = p->parent) {
        for(int i = 0; i < p->cnt; i++) {
            if(strcmp(p->names[i], name) == 0) {
                if(owner) *owner = p;
                return i;
            }
        }
    }
    return -1;
}

// 只查当前帧
static int find_in_frame(StackFrame* f, const char* name)
{
    for(int i = 0; i < f->cnt; i++) {
        if(strcmp(f->names[i], name) == 0) return i;
    }
    return -1;
}

Value* stackframe_get(StackFrame* f, const char* name)
{
    if(!f || !name) return NULL;
    StackFrame* owner = NULL;
    int idx = find_in_chain(f, name, &owner);
    if(idx < 0) return NULL;
    return &owner->vals[idx];
}

Value* stackframe_set(StackFrame* f, const char* name, Value v)
{
    if(!f || !name) return NULL;
    StackFrame* owner = NULL;
    int idx = find_in_chain(f, name, &owner);
    if(idx >= 0) {
        slot_release(&owner->vals[idx]);
        owner->vals[idx] = v;
        return &owner->vals[idx];
    }
    return stackframe_bind(f, name, v);
}

Value* stackframe_bind(StackFrame* f, const char* name, Value v)
{
    if(!f || !name) return NULL;
    int idx = find_in_frame(f, name);
    if(idx >= 0) {
        slot_release(&f->vals[idx]);
        f->vals[idx] = v;
        return &f->vals[idx];
    }
    if(f->cnt >= 64) {
        fprintf(stderr, "栈帧变量数量超限(64): %s\n", name);
        exit(EXIT_FAILURE);
    }
    f->names[f->cnt] = strdup(name);
    f->vals[f->cnt] = v;
    return &f->vals[f->cnt++];
}
