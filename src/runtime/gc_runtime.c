/* gc_runtime.c —— 完整标记-清除 GC 实现（含线程本地分配 TLA）
 *
 * 设计要点：
 *   - 所有 GC 管理的堆块前面统一加 GCObject 头（16 字节）
 *   - 全局链表 g_gc_objects 跟踪所有活跃块（含本地空闲链表中的预分配块）
 *   - gc_alloc 超过阈值时自动触发 gc_collect
 *   - g_in_gc 防止 GC 递归重入
 *   - 全局链表操作加 mutex（线程安全）
 *
 * TLA（Thread-Local Allocation）：
 *   - 小对象（user_size <= TLA_MAX_SIZE）优先从线程本地空闲链表分配（无锁）
 *   - 本地链表空时：先从全局空闲链表批量取用（一次加锁），仍不够则批量 malloc
 *   - 空闲链表直接用 GCObject.next 字段链接，回收时不触碰用户数据区域，
 *     避免多线程 GC（只扫当前线程栈）误回收其他线程对象时立即破坏其内容
 *   - 本地空闲链表中的对象保留在 g_gc_objects 中（marked=2 永生），分配无锁
 *   - 全局空闲链表中的对象不在 g_gc_objects 中，取用时空闲→活跃需插入全局链表（已持锁）
 *   - sweep 时小对象不 free，从 g_gc_objects 摘除后移入全局空闲链表
 *   - 大对象（user_size > TLA_MAX_SIZE）走原有直接 malloc + free 路径
 */
#include "gc_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

/* 编译期断言：GCObject 必须保持 16 字节（user_size 利用原填充空间） */
_Static_assert(sizeof(GCObject) == 16, "GCObject must be 16 bytes");

/* ---- TLA 常量 ---- */
#define TLA_MAX_SIZE   256   /* 用户数据 <=256 字节走 TLA */
#define TLA_BATCH      16    /* 本地链表空时批量分配/取用的对象数 */

/* 线程本地空闲链表（无锁访问）：对象在 g_gc_objects 中，marked=2 */
static _Thread_local GCObject* tla_local_free = NULL;
static _Thread_local int tla_local_count = 0;

/* 全局空闲链表（sweep 回收，线程批量取用时加锁）：对象不在 g_gc_objects 中，marked=2 */
static GCObject* tla_global_free = NULL;
static int tla_global_count = 0;

/* ---- 全局状态 ---- */
static GCObject* g_gc_objects = NULL;
static size_t g_gc_bytes = 0;
static size_t g_gc_threshold = 64 * 1024 * 1024;  /* 初始阈值 64MB */
static pthread_mutex_t g_gc_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_in_gc = 0;
static int g_gc_disable = 0;  /* GC 暂停计数器（构造复合对象时使用） */

/* ---- 协作式 Stop-The-World ----
 * GC 运行时设置 g_gc_stw，VM 解释循环在每条指令前检查并自旋等待。
 * 编译通道在循环回边/函数调用/builtin/每N条指令前检查。
 *
 * 真正的 STW 确认：每个注册线程的 entry 中有 volatile int at_safepoint。
 * 线程进入 gc_stw_check() 自旋时设置 at_safepoint=1，退出时设置 0。
 * GC 线程轮询所有注册线程直到全部 at_safepoint==1（带超时保护），
 * 确保标记时所有线程栈稳定，彻底消除 torn Value 竞态。 */
volatile int g_gc_stw = 0;

/* 当前线程的根（VM 执行循环注册，供 gc_alloc 自动触发 GC 使用） */
static _Thread_local Value*   tls_stack = NULL;
static _Thread_local int*     tls_sp = NULL;
static _Thread_local StackFrame* tls_frame = NULL;

/* 编译通道（C 代码生成）帧链顶 + 注册标记 */
static _Thread_local CFrame*  tls_cframe = NULL;
static _Thread_local int      tls_cframe_registered = 0;

/* ---- 多线程栈根注册表 ----
 * GC 时遍历所有注册线程，扫描每个线程的操作数栈 + 帧链局部变量。
 * 用 g_gc_mutex 保护（gc_collect 已持有该锁，遍历无需额外加锁）。
 * 支持同一线程嵌套注册（头插，栈式语义），unregister 移除最近的 entry。 */
typedef struct GCThreadEntry {
    pthread_t tid;
    Value* stack;
    int* sp_ptr;
    StackFrame* frame;
    volatile int at_safepoint;  /* STW 确认：线程在 gc_stw_check 自旋时为 1 */
    struct GCThreadEntry* next;
} GCThreadEntry;

static GCThreadEntry* g_gc_threads = NULL;

/* ---- 编译通道 CFrame 线程注册表 ----
 * GC 时遍历所有注册线程，扫描每个线程的 CFrame 链（操作数栈 + 局部变量指针数组）。
 * 用 g_gc_mutex 保护（gc_collect 已持有该锁，遍历无需额外加锁）。
 * cframe_ptr 指向该线程的 tls_cframe 变量地址，GC 时解引用获取当前链顶。 */
typedef struct GCCFrameEntry {
    pthread_t tid;
    CFrame** cframe_ptr;
    volatile int at_safepoint;  /* STW 确认：线程在 gc_stw_check 自旋时为 1 */
    struct GCCFrameEntry* next;
} GCCFrameEntry;

static GCCFrameEntry* g_gc_cframe_threads = NULL;

/* 当前线程的注册 entry 指针（gc_stw_check 用它们设置 at_safepoint） */
static _Thread_local GCThreadEntry* tls_cur_vm_entry = NULL;
static _Thread_local GCCFrameEntry* tls_cur_cf_entry = NULL;

/* 协作式 STW 安全点：VM 解释循环每条指令前调用，编译通道每N条指令/循环/调用前调用。
 * 快速路径：无 GC 时直接返回（仅一次 volatile 读），降低频繁检查的开销。
 * GC 运行时设置 at_safepoint=1 后自旋，GC 线程轮询到所有线程 at_safepoint==1 才开始标记。 */
void gc_stw_check(void) {
    if (!g_gc_stw) return;
    if (tls_cur_vm_entry) tls_cur_vm_entry->at_safepoint = 1;
    if (tls_cur_cf_entry) tls_cur_cf_entry->at_safepoint = 1;
    while (g_gc_stw) { sched_yield(); }
    if (tls_cur_vm_entry) tls_cur_vm_entry->at_safepoint = 0;
    if (tls_cur_cf_entry) tls_cur_cf_entry->at_safepoint = 0;
}

/* ---- 内部辅助：用户指针 <-> GCObject ---- */
static inline GCObject* ptr_to_obj(void* ptr) {
    return (GCObject*)((char*)ptr - sizeof(GCObject));
}
static inline void* obj_to_ptr(GCObject* obj) {
    return (void*)((char*)obj + sizeof(GCObject));
}

/* TLA 实际分配大小：用户数据至少 16 字节（确保小对象也能存下未来的内部指针等） */
static inline size_t tla_real_size(size_t size) {
    return (size < 16) ? 16 : size;
}

/* ============================================================
 * 分配
 * ============================================================ */
void* gc_alloc(size_t size, int vtype)
{
    /* 分配前 STW 安全点检查：若 GC 运行中，设置 at_safepoint 并自旋等待。
     * 必须用 gc_stw_check() 而非裸 while(g_gc_stw)，否则 GC 轮询时看不到本线程暂停，造成死锁。 */
    gc_stw_check();
    /* ---- TLA 快速路径：小对象从本地空闲链表分配（无锁） ----
     * 注意：GC 运行期间（g_in_gc）跳过本地空闲链表，强制走加锁路径，
     * 避免无锁修改 marked 位与并发 sweep 产生竞态。 */
    if (size <= TLA_MAX_SIZE && !g_in_gc) {
        size_t real_sz = tla_real_size(size);

        /* 1. 扫描本地空闲链表，找第一个 user_size >= 请求大小的对象 */
        GCObject* prev = NULL;
        GCObject* cur = tla_local_free;
        while (cur) {
            if (cur->user_size >= real_sz) {
                /* 从本地链表摘除 */
                if (prev) prev->next = cur->next;
                else tla_local_free = cur->next;
                tla_local_count--;
                /* 初始化：对象已在 g_gc_objects 中，marked 从 2 改为 3（新分配预标记）。
                 * marked=3 确保对象在被调用方存入根之前的首个 GC 周期不被 sweep；
                 * gc_mark_ptr 视 3 为未标记（需遍历内部引用），sweep 视 3 为活跃（清为 0）。 */
                cur->marked = 3;
                cur->vtype = (unsigned char)vtype;
                memset(obj_to_ptr(cur), 0, size);
                /* 不增加 g_gc_bytes（对象一直在全局链表中，已被统计） */
                return obj_to_ptr(cur);
            }
            prev = cur;
            cur = cur->next;
        }

        /* 2. 本地链表没找到，尝试从全局空闲链表批量取用 */
        if (tla_global_count > 0) {
            pthread_mutex_lock(&g_gc_mutex);
            int taken = 0;
            while (tla_global_free && taken < TLA_BATCH) {
                GCObject* obj = tla_global_free;
                tla_global_free = obj->next;
                tla_global_count--;
                /* 全局空闲对象不在 g_gc_objects 中，需插入 */
                obj->next = g_gc_objects;
                g_gc_objects = obj;
                g_gc_bytes += sizeof(GCObject) + obj->user_size;
                /* 移入本地空闲链表（marked 保持 2） */
                obj->next = tla_local_free;
                tla_local_free = obj;
                tla_local_count++;
                taken++;
            }
            pthread_mutex_unlock(&g_gc_mutex);

            /* 重试一次本地分配 */
            prev = NULL;
            cur = tla_local_free;
            while (cur) {
                if (cur->user_size >= real_sz) {
                    if (prev) prev->next = cur->next;
                    else tla_local_free = cur->next;
                    tla_local_count--;
                    cur->marked = 3;  /* 新分配预标记 */
                    cur->vtype = (unsigned char)vtype;
                    memset(obj_to_ptr(cur), 0, size);
                    return obj_to_ptr(cur);
                }
                prev = cur;
                cur = cur->next;
            }
        }

        /* 3. 还是没有，批量 malloc 新对象 */
        size_t total = sizeof(GCObject) + real_sz;
        GCObject* batch[TLA_BATCH];
        for (int i = 0; i < TLA_BATCH; i++) {
            batch[i] = (GCObject*)malloc(total);
            if (!batch[i]) {
                fprintf(stderr, "GC: out of memory (requested %zu bytes)\n", size);
                abort();
            }
            batch[i]->marked = 0;
            batch[i]->vtype = 0;
            batch[i]->user_size = (uint32_t)real_sz;
            batch[i]->next = NULL;
        }

        /* 预分配块（i>=1）标记为永生（本地空闲链表）；
         * batch[0] 标记为 3（新分配预标记），确保插入 g_gc_objects 后、
         * 调用方存入根之前的首个 GC 周期不被 sweep */
        for (int i = 1; i < TLA_BATCH; i++) batch[i]->marked = 2;
        batch[0]->marked = 3;

        int need_collect = 0;
        pthread_mutex_lock(&g_gc_mutex);
        for (int i = 0; i < TLA_BATCH; i++) {
            batch[i]->next = g_gc_objects;
            g_gc_objects = batch[i];
            g_gc_bytes += total;
        }
        if (g_gc_bytes > g_gc_threshold && !g_in_gc && g_gc_disable == 0) {
            need_collect = 1;
        }
        pthread_mutex_unlock(&g_gc_mutex);

        /* 第 0 个作为返回值（活跃对象） */
        batch[0]->vtype = (unsigned char)vtype;
        memset(obj_to_ptr(batch[0]), 0, size);

        /* 剩余 TLA_BATCH-1 个放入本地空闲链表（marked=2 永生，保留在 g_gc_objects） */
        for (int i = 1; i < TLA_BATCH; i++) {
            batch[i]->next = tla_local_free;
            tla_local_free = batch[i];
            tla_local_count++;
        }

        if (need_collect && tls_stack && tls_sp && (tls_frame || tls_cframe)) {
            gc_collect(tls_stack, *tls_sp, tls_frame);
        }
        return obj_to_ptr(batch[0]);
    }

    /* ---- 大对象：原有直接 malloc + 插入链表路径 ---- */
    size_t total = sizeof(GCObject) + size;
    GCObject* obj = (GCObject*)malloc(total);
    if (!obj) {
        fprintf(stderr, "GC: out of memory (requested %zu bytes)\n", size);
        abort();
    }
    obj->marked = 3;  /* 新分配预标记：确保随后 gc_collect 不 sweep 尚未入根的对象 */
    obj->vtype = (unsigned char)vtype;
    obj->user_size = (uint32_t)size;
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

    if (need_collect && tls_stack && tls_sp && (tls_frame || tls_cframe)) {
        gc_collect(tls_stack, *tls_sp, tls_frame);
    }
    return obj_to_ptr(obj);
}

/* ============================================================
 * 重分配（仅用于内部缓冲区：array items 等）
 * 改用 malloc + memcpy，旧对象保留在链表中不释放。
 *
 * 为什么不用 realloc：realloc 可能移动内存并释放旧块，但调用方
 * （如 lumin_array_add）在锁外更新 items 指针。在 gc_realloc 返回
 * 和调用方更新指针之间，另一个线程的 GC 可能扫描到已释放的旧指针，
 * 导致 gc_mark_ptr 读取已释放内存 → segfault。
 *
 * 改用 malloc + memcpy 后：旧对象保留在 g_gc_objects 中，内存不被
 * 释放。调用方更新指针后，旧对象不再被引用，下一次 GC sweep 回收它。
 * ============================================================ */
void* gc_realloc(void* ptr, size_t new_size)
{
    if (!ptr) return gc_alloc(new_size, VAL_ARRAY);

    GCObject* old_obj = ptr_to_obj(ptr);
    unsigned char vtype = old_obj->vtype;
    size_t old_user = old_obj->user_size;
    size_t copy_size = old_user < new_size ? old_user : new_size;

    /* 分配新对象 + 复制（不使用 realloc，避免旧块被提前释放） */
    size_t new_total = sizeof(GCObject) + new_size;
    GCObject* new_obj = (GCObject*)malloc(new_total);
    if (!new_obj) {
        fprintf(stderr, "GC: realloc out of memory (requested %zu bytes)\n", new_size);
        abort();
    }
    memcpy(new_obj, old_obj, sizeof(GCObject) + copy_size);
    new_obj->user_size = (uint32_t)new_size;
    new_obj->marked = 0;
    new_obj->vtype = vtype;
    new_obj->next = NULL;
    if (new_size > copy_size) {
        memset((char*)new_obj + sizeof(GCObject) + copy_size, 0, new_size - copy_size);
    }

    pthread_mutex_lock(&g_gc_mutex);
    /* 新对象插入全局链表 */
    new_obj->next = g_gc_objects;
    g_gc_objects = new_obj;
    g_gc_bytes += new_total;
    /* 旧对象保留在链表中，marked 保持原值（通常为 0）。
     * 调用方更新指针后，旧对象不再被引用，下一次 GC sweep 释放它。
     * 在调用方更新指针前，并发 GC 可安全扫描旧对象（内存未被释放）。 */
    pthread_mutex_unlock(&g_gc_mutex);

    return obj_to_ptr(new_obj);
}

/* ============================================================
 * 标记原始 GC 指针（buckets/tree/MapEntry 等内部缓冲区）
 * 不递归 Value，仅标记该 GCObject 不被 sweep
 * ============================================================ */
int gc_mark_ptr(void* ptr)
{
    if (!ptr) return 0;
    GCObject* obj = ptr_to_obj(ptr);
    /* marked=1 已标记；marked=2 永生（钉住/空闲链表）；
     * marked=3 新分配预标记（需遍历，视同未标记） */
    if (obj->marked == 1 || obj->marked == 2) return 0;
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
 *
 * 指针校验：并发标记时，GC 基于 sp 快照扫描栈，线程可能在扫描期间
 * pop 槽位再 push 新值覆盖，导致 GC 读到撕裂 Value（type 合法但 pointer
 * 为旧 int 值或垃圾）。此处校验指针范围，跳过明显非法的指针，防止
 * gc_mark_ptr 访问非法地址导致 segfault。
 * ============================================================ */
#define GC_VALID_PTR(p) ((p) && (unsigned long long)(p) >= 4096 && (unsigned long long)(p) <= 0x00007fffffffffffULL)

void gc_mark(Value v)
{
    switch (v.type) {
    case VAL_STRING: {
        if (!v.str_inline && GC_VALID_PTR(v.v.s)) gc_mark_ptr(v.v.s);
        break;
    }
    case VAL_ARRAY: {
        if (!GC_VALID_PTR(v.v.array)) break;
        if (v.v.array->stack_alloc) {
            if (v.v.array->items) {
                if (!v.v.array->items_stack_alloc) {
                    gc_mark_ptr(v.v.array->items);
                }
                for (int i = 0; i < v.v.array->len; i++) {
                    gc_mark(v.v.array->items[i]);
                }
            }
        } else {
            if (!gc_mark_ptr(v.v.array)) break;
            if (v.v.array->items) {
                gc_mark_ptr(v.v.array->items);
                for (int i = 0; i < v.v.array->len; i++) {
                    gc_mark(v.v.array->items[i]);
                }
            }
        }
        break;
    }
    case VAL_MAP: {
        ValueMap* m = v.v.map;
        if (!GC_VALID_PTR(m)) break;
        if (m->stack_alloc) {
            /* 栈分配 ValueMap：无 GCObject 头，只标记内部缓冲区和递归键值 */
        } else {
            if (!gc_mark_ptr(m)) break;
        }
        if (m->buckets) gc_mark_ptr(m->buckets);
        if (m->tree) gc_mark_ptr(m->tree);
        for (int i = 0; i < m->cap; i++) {
            MapEntry* e = m->buckets[i];
            if (!e) continue;
            if (m->tree[i]) {
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
        break;
    }
}

/* ============================================================
 * 标记根：VM 栈 + 当前帧及父帧链的局部变量
 * ============================================================ */
void gc_mark_roots(Value* stack, int sp, StackFrame* frame)
{
    if (stack) {
        for (int i = 0; i < sp; i++) {
            gc_mark(stack[i]);
        }
    }
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
 * 清除：遍历全局链表，未标记的释放/回收，清除标记位
 *
 * 小对象（user_size <= TLA_MAX_SIZE）：不 free，从 g_gc_objects 摘除
 *   后移入全局空闲链表（marked=2，用 next 链接，不触碰用户数据区域）
 * 大对象：free()，从链表摘除
 * 永生对象（marked==2，含钉住对象和本地空闲链表对象）：跳过
 * ============================================================ */
void gc_sweep(void)
{
    GCObject** pp = &g_gc_objects;
    while (*pp) {
        GCObject* cur = *pp;
        if (cur->marked == 2) {
            /* 永生对象（钉住 / 本地空闲链表预分配）：跳过 */
            pp = &cur->next;
        } else if (!cur->marked) {
            /* 未标记：需要回收 */
            if (cur->user_size <= TLA_MAX_SIZE) {
                /* 小对象：从 g_gc_objects 摘除，移入全局空闲链表
                 * 不触碰用户数据区域（避免多线程 GC 误回收时立即破坏对象内容） */
                *pp = cur->next;
                g_gc_bytes -= sizeof(GCObject) + cur->user_size;
                cur->marked = 2;
                cur->next = tla_global_free;
                tla_global_free = cur;
                tla_global_count++;
            } else {
                /* 大对象：free */
                *pp = cur->next;
                g_gc_bytes -= sizeof(GCObject) + cur->user_size;
                free(cur);
            }
        } else {
            /* 已标记（活跃）：清除标记位，继续 */
            cur->marked = 0;
            pp = &cur->next;
        }
    }
}

/* ============================================================
 * 回收：mark_roots（遍历所有注册线程）→ sweep → 更新阈值
 * ============================================================ */
void gc_collect(Value* stack, int sp, StackFrame* frame)
{
    if (g_in_gc) return;
    g_in_gc = 1;

    /* 真正的 STW 确认机制：
     * 1. 设置 g_gc_stw=1 通知所有线程暂停
     * 2. GC 线程自身标记为 at_safepoint（它正在执行 GC，不在执行用户指令）
     * 3. 轮询所有注册线程直到全部 at_safepoint==1（带 100ms 超时保护）
     * 4. 所有线程栈稳定后再标记，彻底消除 torn Value 竞态 */
    g_gc_stw = 1;
    if (tls_cur_vm_entry) tls_cur_vm_entry->at_safepoint = 1;
    if (tls_cur_cf_entry) tls_cur_cf_entry->at_safepoint = 1;

    /* 轮询所有注册线程到达安全点。
     * 用 sched_yield() 自旋而非 nanosleep(1us)：macOS 定时器粒度约 1ms，
     * nanosleep(1us) 实际睡 1ms，导致每次 GC 轮询开销巨大。
     * sched_yield() 仅让出 CPU，常见情况所有线程在微秒级到达安全点。
     * 超时用迭代次数兜底（100M 次 yield 约 100ms-1s，取决于系统负载）。 */
    #define STW_POLL_MAX_ITERS 100000000
    int poll_iters = 0;
    int stw_timed_out = 0;
    while (1) {
        int all_paused = 1;
        for (GCThreadEntry* e = g_gc_threads; e; e = e->next) {
            if (!e->at_safepoint) { all_paused = 0; break; }
        }
        if (all_paused) {
            for (GCCFrameEntry* e = g_gc_cframe_threads; e; e = e->next) {
                if (!e->at_safepoint) { all_paused = 0; break; }
            }
        }
        if (all_paused) break;
        if (poll_iters >= STW_POLL_MAX_ITERS) {
            fprintf(stderr, "GC: WARNING: STW timeout, some threads not at safepoint (blocked in IO/lock?)\n");
            stw_timed_out = 1;
            break;
        }
        sched_yield();
        poll_iters++;
    }
    (void)stw_timed_out;

    pthread_mutex_lock(&g_gc_mutex);

    /* 标记阶段：遍历全局线程注册表，扫描每个注册线程的栈 + 帧链。
     * 注册表非空时以注册表为准（覆盖所有活跃线程）；
     * 注册表为空时回退到扫传入参数（兼容 gc_collect_now / 未注册的调用方）。 */
    if (g_gc_threads) {
        for (GCThreadEntry* e = g_gc_threads; e; e = e->next) {
            gc_mark_roots(e->stack, e->sp_ptr ? *e->sp_ptr : 0, e->frame);
        }
    } else {
        gc_mark_roots(stack, sp, frame);
    }

    /* 扫描编译通道 CFrame 链：遍历所有注册线程，每个线程扫描其 CFrame 链。
     * g_gc_cframe_threads 的遍历在 g_gc_mutex 锁内（gc_collect 已持有该锁），无需额外加锁。 */
    for (GCCFrameEntry* e = g_gc_cframe_threads; e; e = e->next) {
        CFrame* cf = *e->cframe_ptr;
        while (cf) {
            if (cf->stack && cf->sp) {
                for (int i = 0; i < *cf->sp; i++) gc_mark(cf->stack[i]);
            }
            if (cf->local_ptrs) {
                for (int i = 0; i < cf->nlocals; i++) {
                    if (cf->local_ptrs[i]) gc_mark(*cf->local_ptrs[i]);
                }
            }
            cf = cf->parent;
        }
    }
    /* 安全兜底：扫描当前线程 tls_cframe（未注册的情况） */
    if (tls_cframe && !g_gc_cframe_threads) {
        CFrame* cf = tls_cframe;
        while (cf) {
            if (cf->stack && cf->sp) {
                for (int i = 0; i < *cf->sp; i++) gc_mark(cf->stack[i]);
            }
            if (cf->local_ptrs) {
                for (int i = 0; i < cf->nlocals; i++) {
                    if (cf->local_ptrs[i]) gc_mark(*cf->local_ptrs[i]);
                }
            }
            cf = cf->parent;
        }
    }

    gc_sweep();
    /* 精确重新计算 g_gc_bytes（利用 user_size 字段） */
    g_gc_bytes = 0;
    for (GCObject* o = g_gc_objects; o; o = o->next) {
        g_gc_bytes += sizeof(GCObject) + o->user_size;
    }
    /* 阈值：至少 1MB，且不小于当前用量的 2 倍 */
    size_t new_threshold = g_gc_bytes * 2;
    if (new_threshold < 1024 * 1024) new_threshold = 1024 * 1024;
    g_gc_threshold = new_threshold;
    pthread_mutex_unlock(&g_gc_mutex);

    g_gc_stw = 0;
    /* 清除 GC 线程自身的安全点标志 */
    if (tls_cur_vm_entry) tls_cur_vm_entry->at_safepoint = 0;
    if (tls_cur_cf_entry) tls_cur_cf_entry->at_safepoint = 0;
    g_in_gc = 0;
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
    pthread_mutex_lock(&g_gc_mutex);
    if (g_gc_disable > 0) g_gc_disable--;
    int need = (g_gc_disable == 0 && !g_in_gc && g_gc_bytes > g_gc_threshold);
    pthread_mutex_unlock(&g_gc_mutex);
    /* 计数器归零时若超过阈值则触发一次 GC（构造函数内批量分配常伴随 gc_disable，
     * 自动 GC 条件在 gc_alloc 中被 g_gc_disable 阻塞，需在 enable 时补触发） */
    if (need && tls_stack && tls_sp && (tls_frame || tls_cframe)) {
        gc_collect(tls_stack, *tls_sp, tls_frame);
    }
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
    if (tls_stack && tls_sp && (tls_frame || tls_cframe)) {
        gc_collect(tls_stack, *tls_sp, tls_frame);
    }
}

/* ============================================================
 * 多线程栈根注册表
 * ============================================================ */
void gc_register_thread(Value* stack, int* sp_ptr, StackFrame* frame)
{
    GCThreadEntry* e = (GCThreadEntry*)malloc(sizeof(GCThreadEntry));
    if (!e) {
        fprintf(stderr, "GC: out of memory registering thread\n");
        abort();
    }
    e->tid = pthread_self();
    e->stack = stack;
    e->sp_ptr = sp_ptr;
    e->frame = frame;
    e->at_safepoint = 0;

    pthread_mutex_lock(&g_gc_mutex);
    /* 头插：最近注册的 entry 在链表头部，unregister 时移除第一个匹配 tid 的 entry */
    e->next = g_gc_threads;
    g_gc_threads = e;
    pthread_mutex_unlock(&g_gc_mutex);

    /* 设置线程本地指针，供 gc_stw_check 设置 at_safepoint */
    tls_cur_vm_entry = e;
}

void gc_unregister_thread(void)
{
    pthread_t self = pthread_self();
    pthread_mutex_lock(&g_gc_mutex);
    /* 移除第一个 tid == self 的 entry（头插保证它是最近注册的，栈式语义） */
    GCThreadEntry** pp = &g_gc_threads;
    GCThreadEntry* next_for_self = NULL;
    while (*pp) {
        if (pthread_equal((*pp)->tid, self)) {
            GCThreadEntry* victim = *pp;
            *pp = victim->next;
            /* 查找同线程的下一个 entry（嵌套注册时更新 TLS 指针） */
            for (GCThreadEntry* e = *pp; e; e = e->next) {
                if (pthread_equal(e->tid, self)) { next_for_self = e; break; }
            }
            free(victim);
            break;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&g_gc_mutex);
    tls_cur_vm_entry = next_for_self;
}

/* ============================================================
 * 编译通道 CFrame 帧链管理
 * ============================================================ */
void gc_push_cframe(CFrame* f)
{
    /* 首次注册线程（内部加锁，必须在 push 加锁前完成，否则双重锁死锁） */
    if (!tls_cframe_registered) {
        gc_register_cframe_thread();
        tls_cframe_registered = 1;
    }
    pthread_mutex_lock(&g_gc_mutex);
    f->parent = tls_cframe;
    tls_cframe = f;
    pthread_mutex_unlock(&g_gc_mutex);
    /* tls_stack/tls_sp 是线程本地，仅当前线程访问，无竞态，锁外设置 */
    tls_stack = f->stack;
    tls_sp = f->sp;
}

void gc_pop_cframe(void)
{
    pthread_mutex_lock(&g_gc_mutex);
    CFrame* parent = NULL;
    if (tls_cframe) {
        parent = tls_cframe->parent;
        tls_cframe = parent;
    }
    pthread_mutex_unlock(&g_gc_mutex);
    if (parent) {
        tls_stack = parent->stack;
        tls_sp = parent->sp;
    } else {
        tls_stack = NULL;
        tls_sp = NULL;
    }
}

CFrame* gc_cframe_top(void)
{
    return tls_cframe;
}

void gc_cframe_restore(CFrame* top)
{
    pthread_mutex_lock(&g_gc_mutex);
    tls_cframe = top;
    pthread_mutex_unlock(&g_gc_mutex);
    if (top) {
        tls_stack = top->stack;
        tls_sp = top->sp;
    } else {
        tls_stack = NULL;
        tls_sp = NULL;
    }
}

void gc_register_cframe_thread(void)
{
    GCCFrameEntry* e = (GCCFrameEntry*)malloc(sizeof(GCCFrameEntry));
    if (!e) {
        fprintf(stderr, "GC: out of memory registering cframe thread\n");
        abort();
    }
    e->tid = pthread_self();
    e->cframe_ptr = &tls_cframe;
    e->at_safepoint = 0;

    pthread_mutex_lock(&g_gc_mutex);
    e->next = g_gc_cframe_threads;
    g_gc_cframe_threads = e;
    pthread_mutex_unlock(&g_gc_mutex);

    /* 设置线程本地指针，供 gc_stw_check 设置 at_safepoint */
    tls_cur_cf_entry = e;
}

void gc_unregister_cframe_thread(void)
{
    pthread_t self = pthread_self();
    pthread_mutex_lock(&g_gc_mutex);
    GCCFrameEntry** pp = &g_gc_cframe_threads;
    GCCFrameEntry* next_for_self = NULL;
    while (*pp) {
        if (pthread_equal((*pp)->tid, self)) {
            GCCFrameEntry* victim = *pp;
            *pp = victim->next;
            for (GCCFrameEntry* e = *pp; e; e = e->next) {
                if (pthread_equal(e->tid, self)) { next_for_self = e; break; }
            }
            free(victim);
            break;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&g_gc_mutex);
    tls_cur_cf_entry = next_for_self;
    tls_cframe_registered = 0;
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
