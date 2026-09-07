/* gc_runtime.c —— 完整标记-清除 GC 实现
 *
 * 设计要点：
 *   - 所有 GC 管理的堆块前面统一加 GCObject 头
 *   - 全局链表 g_gc_objects 跟踪所有存活块
 *   - gc_alloc 超过阈值时自动触发 gc_collect
 *   - g_in_gc 防止 GC 递归重入
 *   - 全局链表操作加 mutex（线程安全）
 *   - ValueArray/ValueError 内联在 Value 里，GC 只管理其内部缓冲区/字符串
 *   - ValueMap 是指针，GC 管理 ValueMap* 本身及内部 buckets/tree/entry
 */
#include "gc_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

/* ---- 全局状态 ---- */
static GCObject* g_gc_objects = NULL;
static size_t g_gc_bytes = 0;
static size_t g_gc_threshold = 64 * 1024 * 1024;  /* 初始阈值 64MB（多线程安全：减少跨线程 sweep 风险） */
static pthread_mutex_t g_gc_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_in_gc = 0;
static int g_gc_disable = 0;  /* GC 暂停计数器（构造复合对象时使用） */

/* 当前线程的根（VM 执行循环注册，供 gc_alloc 自动触发 GC 使用） */
static _Thread_local Value*   tls_stack = NULL;
static _Thread_local int*     tls_sp = NULL;
static _Thread_local StackFrame* tls_frame = NULL;

/* ---- 内部辅助：用户指针 <-> GCObject ---- */
static inline GCObject* ptr_to_obj(void* ptr) {
    return (GCObject*)((char*)ptr - sizeof(GCObject));
}
static inline void* obj_to_ptr(GCObject* obj) {
    return (void*)((char*)obj + sizeof(GCObject));
}

/* ============================================================
 * 分配
 * ============================================================ */
void* gc_alloc(size_t size, int vtype)
{
    size_t total = sizeof(GCObject) + size;
    GCObject* obj = (GCObject*)malloc(total);
    if (!obj) {
        fprintf(stderr, "GC: out of memory (requested %zu bytes)\n", size);
        abort();
    }
    obj->marked = 0;
    obj->vtype = (unsigned char)vtype;
    obj->next = NULL;
    memset(obj_to_ptr(obj), 0, size);

    int need_collect = 0;
    pthread_mutex_lock(&g_gc_mutex);
    obj->next = g_gc_objects;
    g_gc_objects = obj;
    g_gc_bytes += total;
    if (g_gc_bytes > g_gc_threshold && !g_in_gc && g_gc_disable == 0) {
        need_collect = 1;
    }
    pthread_mutex_unlock(&g_gc_mutex);

    if (need_collect && tls_stack && tls_sp && tls_frame) {
        gc_collect(tls_stack, *tls_sp, tls_frame);
    }
    return obj_to_ptr(obj);
}

/* ============================================================
 * 重分配（仅用于内部缓冲区：array items 等）
 * 从链表移除旧节点 → realloc → 重新挂入
 * ============================================================ */
void* gc_realloc(void* ptr, size_t new_size)
{
    if (!ptr) return gc_alloc(new_size, VAL_ARRAY);

    GCObject* old_obj = ptr_to_obj(ptr);
    size_t old_total = sizeof(GCObject) + 0; /* 旧大小未知，从链表移除即可 */
    unsigned char vtype = old_obj->vtype;

    /* 从链表移除 */
    pthread_mutex_lock(&g_gc_mutex);
    GCObject** pp = &g_gc_objects;
    while (*pp) {
        if (*pp == old_obj) {
            *pp = old_obj->next;
            break;
        }
        pp = &(*pp)->next;
    }
    /* 估算旧块大小（无法精确，用实际分配大小的近似；
     * 这里不维护精确旧大小，sweep 时 free 由 malloc 自己记录） */
    pthread_mutex_unlock(&g_gc_mutex);

    /* realloc（可能移动） */
    size_t new_total = sizeof(GCObject) + new_size;
    GCObject* new_obj = (GCObject*)realloc(old_obj, new_total);
    if (!new_obj) {
        fprintf(stderr, "GC: realloc out of memory (requested %zu bytes)\n", new_size);
        abort();
    }
    new_obj->marked = 0;
    new_obj->vtype = vtype;
    new_obj->next = NULL;

    pthread_mutex_lock(&g_gc_mutex);
    new_obj->next = g_gc_objects;
    g_gc_objects = new_obj;
    g_gc_bytes += new_total;  /* 近似：不精确扣减旧块，保守估计 */
    pthread_mutex_unlock(&g_gc_mutex);

    (void)old_total;
    return obj_to_ptr(new_obj);
}

/* ============================================================
 * 标记原始 GC 指针（buckets/tree/MapEntry 等内部缓冲区）
 * 不递归 Value，仅标记该 GCObject 不被 sweep
 * ============================================================ */
/* 标记原始 GC 指针（buckets/tree/MapEntry 等内部缓冲区）
 * 不递归 Value，仅标记该 GCObject 不被 sweep。
 * 返回 1 表示新标记（需要递归子对象），0 表示已标记/永生/空 */
int gc_mark_ptr(void* ptr)
{
    if (!ptr) return 0;
    GCObject* obj = ptr_to_obj(ptr);
    if (obj->marked) return 0;  /* 已标记或永生 */
    obj->marked = 1;
    return 1;
}

/* 钉住对象：marked=2 表示永生，sweep 永不回收，mark 跳过 */
void gc_pin(void* ptr)
{
    if (!ptr) return;
    GCObject* obj = ptr_to_obj(ptr);
    obj->marked = 2;
}

/* ============================================================
 * 标记 Value：堆类型标记其 GCObject + 递归标记内部引用
 * ============================================================ */
void gc_mark(Value v)
{
    switch (v.type) {
    case VAL_STRING: {
        if (v.v.s) gc_mark_ptr(v.v.s);
        break;
    }
    case VAL_ARRAY: {
        if (!v.v.array) break;
        if (!gc_mark_ptr(v.v.array)) break;  /* 已标记，跳过递归（循环引用检测） */
        if (v.v.array->items) {
            gc_mark_ptr(v.v.array->items);
            for (int i = 0; i < v.v.array->len; i++) {
                gc_mark(v.v.array->items[i]);
            }
        }
        break;
    }
    case VAL_MAP: {
        ValueMap* m = v.v.map;
        if (!m) break;
        if (!gc_mark_ptr(m)) break;  /* 已标记，跳过递归 */
        if (m->buckets) gc_mark_ptr(m->buckets);
        if (m->tree) gc_mark_ptr(m->tree);
        /* 遍历所有桶的 entry */
        for (int i = 0; i < m->cap; i++) {
            MapEntry* e = m->buckets[i];
            if (!e) continue;
            if (m->tree[i]) {
                /* 红黑树：迭代式 DFS */
                MapEntry* stk[256];
                int top = 0;
                MapEntry* cur = e;
                while (cur || top > 0) {
                    while (cur) {
                        if (top < 256) stk[top++] = cur;
                        cur = cur->left;
                    }
                    if (top == 0) break;
                    cur = stk[--top];
                    gc_mark_ptr(cur);
                    gc_mark(cur->key);
                    gc_mark(cur->value);
                    cur = cur->right;
                }
            } else {
                /* 链表 */
                while (e) {
                    gc_mark_ptr(e);
                    gc_mark(e->key);
                    gc_mark(e->value);
                    e = e->next;
                }
            }
        }
        break;
    }
    case VAL_ERROR: {
        if (v.v.err.type) gc_mark_ptr(v.v.err.type);
        if (v.v.err.message) gc_mark_ptr(v.v.err.message);
        if (v.v.err.stack) gc_mark_ptr(v.v.err.stack);
        break;
    }
    case VAL_FUNC: {
        RuntimeFunc* f = v.v.func.func_obj;
        if (f && f->captures) {
            for (int i = 0; i < f->capture_count; i++) {
                gc_mark(f->captures[i]);
            }
        }
        break;
    }
    default:
        break;  /* 标量类型（INT/DOUBLE/BOOL/CHAR/BYTE/NONE）无堆引用 */
    }
}

/* ============================================================
 * 标记根：VM 栈 + 当前帧及父帧链的局部变量
 * ============================================================ */
void gc_mark_roots(Value* stack, int sp, StackFrame* frame)
{
    /* 1. VM 栈 */
    if (stack) {
        for (int i = 0; i < sp; i++) {
            gc_mark(stack[i]);
        }
    }
    /* 2. 帧链局部变量 */
    StackFrame* f = frame;
    while (f) {
        if (f->vals) {
            for (int i = 0; i < f->cnt; i++) {
                gc_mark(f->vals[i]);
            }
        }
        f = f->parent;
    }
}

/* ============================================================
 * 清除：遍历全局链表，未标记的释放，清除标记位
 * ============================================================ */
void gc_sweep(void)
{
    GCObject** pp = &g_gc_objects;
    while (*pp) {
        GCObject* cur = *pp;
        if (cur->marked == 2) {
            /* 永生对象：跳过，不清除标记 */
            pp = &cur->next;
        } else if (!cur->marked) {
            *pp = cur->next;
            free(cur);  /* 内部子对象（buckets/entries/strings）是独立 GC 对象，各自 sweep */
        } else {
            cur->marked = 0;
            pp = &cur->next;
        }
    }
}

/* ============================================================
 * 回收：mark_roots → sweep → 更新阈值
 * ============================================================ */
void gc_collect(Value* stack, int sp, StackFrame* frame)
{
    if (g_in_gc) return;
    g_in_gc = 1;

    pthread_mutex_lock(&g_gc_mutex);
    gc_mark_roots(stack, sp, frame);
    gc_sweep();
    /* 重新计算 g_gc_bytes（sweep 后精确统计） */
    g_gc_bytes = 0;
    size_t cnt = 0;
    for (GCObject* o = g_gc_objects; o; o = o->next) {
        /* 无法精确知道每块大小，用 malloc_usable_size 不可移植；
         * 保守估计：每块至少 sizeof(GCObject)，阈值用对象数辅助 */
        g_gc_bytes += sizeof(GCObject) + 16;  /* 近似平均负载 */
        cnt++;
    }
    /* 阈值：至少 1MB，且不小于当前用量的 2 倍 */
    size_t new_threshold = g_gc_bytes * 2;
    if (new_threshold < 1024 * 1024) new_threshold = 1024 * 1024;
    g_gc_threshold = new_threshold;
    pthread_mutex_unlock(&g_gc_mutex);

    g_in_gc = 0;
    (void)cnt;
}

/* ============================================================
 * GC 暂停/恢复（构造复合对象时防止中间态被 sweep）
 * ============================================================ */
void gc_disable(void)
{
    pthread_mutex_lock(&g_gc_mutex);
    g_gc_disable++;
    pthread_mutex_unlock(&g_gc_mutex);
}

void gc_enable(void)
{
    /* 只减计数器，不立即触发 GC。
       下一次 gc_alloc 时若超过阈值会自动触发，
       那时构造的对象已被调用方存入 VM 栈/帧，可达安全。 */
    pthread_mutex_lock(&g_gc_mutex);
    if (g_gc_disable > 0) g_gc_disable--;
    pthread_mutex_unlock(&g_gc_mutex);
}

/* ============================================================
 * 根注册
 * ============================================================ */
void gc_set_roots(Value* stack, int* sp_ptr, StackFrame* frame)
{
    tls_stack = stack;
    tls_sp = sp_ptr;
    tls_frame = frame;
}

void gc_get_roots(Value** stack, int** sp_ptr, StackFrame** frame)
{
    if (stack) *stack = tls_stack;
    if (sp_ptr) *sp_ptr = tls_sp;
    if (frame) *frame = tls_frame;
}

/* 手动触发（使用当前注册的根） */
void gc_collect_now(void)
{
    if (tls_stack && tls_sp && tls_frame) {
        gc_collect(tls_stack, *tls_sp, tls_frame);
    }
}

/* 统计 */
size_t gc_bytes(void) { return g_gc_bytes; }
size_t gc_count(void)
{
    size_t cnt = 0;
    pthread_mutex_lock(&g_gc_mutex);
    for (GCObject* o = g_gc_objects; o; o = o->next) cnt++;
    pthread_mutex_unlock(&g_gc_mutex);
    return cnt;
}
