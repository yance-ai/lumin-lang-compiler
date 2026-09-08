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
    f->cap = 0;
    f->names = NULL;
    f->vals = NULL;
    f->parent = parent;
    f->shared = 0;
    pthread_rwlock_init(&f->rw, NULL);
    return f;
}

void stackframe_set_shared(StackFrame* f)
{
    if(f) f->shared = 1;
}

/* 帧内变量槽扩容：翻倍，无硬上限。调用方必须已持有该帧的写锁（若 shared）
 *
 * 注意：使用 malloc + memcpy 而非 realloc，因为 realloc 可能释放旧缓冲区，
 * 而 f->vals/f->names 指针在 realloc 返回后才更新。在这个窗口内，另一个线程
 * 的 GC 可能扫描该帧并读到已释放的旧指针 → UAF → segfault。
 * 改用 malloc + memcpy 后，先更新指针再 free 旧缓冲区，GC 永远不会读到已释放指针。 */
static void frame_ensure(StackFrame* f, int need)
{
    if(need <= f->cap) return;
    int newcap = f->cap > 0 ? f->cap : 16;
    while(newcap < need) newcap *= 2;

    /* 扩容 names：malloc + memcpy，更新指针后 free 旧缓冲区 */
    char** nn = (char**)malloc((size_t)newcap * sizeof(char*));
    if(!nn) { perror("stackframe expand names"); exit(EXIT_FAILURE); }
    if(f->names) {
        memcpy(nn, f->names, (size_t)f->cap * sizeof(char*));
    }
    /* 新槽位初始化为 NULL */
    for(int i = f->cap; i < newcap; i++) nn[i] = NULL;
    char** old_names = f->names;
    f->names = nn;
    free(old_names);

    /* 扩容 vals：malloc + memcpy，更新指针后 free 旧缓冲区 */
    Value* nv = (Value*)malloc((size_t)newcap * sizeof(Value));
    if(!nv) { perror("stackframe expand vals"); exit(EXIT_FAILURE); }
    if(f->vals) {
        memcpy(nv, f->vals, (size_t)f->cap * sizeof(Value));
    }
    /* 新槽位初始化为 VAL_NONE */
    for(int i = f->cap; i < newcap; i++) {
        nv[i].type = VAL_NONE;
        nv[i].v.i = 0;
    }
    Value* old_vals = f->vals;
    f->vals = nv;
    free(old_vals);

    f->cap = newcap;
}

void stackframe_destroy(StackFrame* f)
{
    if(!f) return;
    for(int i = 0; i < f->cnt; i++) {
        free(f->names[i]);
        slot_release(&f->vals[i]);
    }
    free(f->names);
    free(f->vals);
    pthread_rwlock_destroy(&f->rw);
    free(f);
}

// 只查当前帧（调用方必须已持有该帧锁，若 shared）
static int find_in_frame(StackFrame* f, const char* name)
{
    for(int i = 0; i < f->cnt; i++) {
        if(strcmp(f->names[i], name) == 0) return i;
    }
    return -1;
}

Value stackframe_get(StackFrame* f, const char* name, _Bool* found)
{
    Value zero;
    memset(&zero, 0, sizeof(zero));
    if(found) *found = 0;
    if(!f || !name) return zero;
    for(StackFrame* p = f; p; p = p->parent) {
        int hl = p->shared ? (pthread_rwlock_rdlock(&p->rw), 1) : 0;
        for(int i = 0; i < p->cnt; i++) {
            if(strcmp(p->names[i], name) == 0) {
                Value v = p->vals[i];   // 锁内拷贝（浅拷贝，语义与旧实现一致）
                if(hl) pthread_rwlock_unlock(&p->rw);
                if(found) *found = 1;
                return v;
            }
        }
        if(hl) pthread_rwlock_unlock(&p->rw);
    }
    return zero;
}

void stackframe_set(StackFrame* f, const char* name, Value v)
{
    if(!f || !name) return;
    StackFrame* owner = NULL;
    for(StackFrame* p = f; p; p = p->parent) {
        int hl = p->shared ? (pthread_rwlock_rdlock(&p->rw), 1) : 0;
        if(find_in_frame(p, name) >= 0) { owner = p; }
        if(hl) pthread_rwlock_unlock(&p->rw);
        if(owner) break;
    }
    if(owner) {
        int hl = owner->shared ? (pthread_rwlock_wrlock(&owner->rw), 1) : 0;
        int idx = find_in_frame(owner, name);   // 锁内重查（扩容只搬移数组，槽位内容保留）
        if(idx >= 0) {
            slot_release(&owner->vals[idx]);
            owner->vals[idx] = v;
        }
        if(hl) pthread_rwlock_unlock(&owner->rw);
        return;
    }
    stackframe_bind(f, name, v);
}

void stackframe_bind(StackFrame* f, const char* name, Value v)
{
    if(!f || !name) return;
    int hl = f->shared ? (pthread_rwlock_wrlock(&f->rw), 1) : 0;
    int idx = find_in_frame(f, name);
    if(idx >= 0) {
        slot_release(&f->vals[idx]);
        f->vals[idx] = v;
    } else {
        frame_ensure(f, f->cnt + 1);
        f->names[f->cnt] = strdup(name);
        f->vals[f->cnt] = v;
        f->cnt++;
    }
    if(hl) pthread_rwlock_unlock(&f->rw);
}
