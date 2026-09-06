// lm_lock.c —— 锁机制运行时：锁表（互斥/递归互斥/读写/自旋）+ 统一操作
// 安全设计：
//   - 锁对象（LockObj）独立 malloc，锁表只存指针 —— realloc 扩容移动表不影响已下发
//     锁 id 的稳定性；lock/unlock 在表锁外只操作稳定的 obj 指针，无悬垂风险
//   - 表内分配/查找全程 g_tbl_lock 保护；具体锁的加锁/解锁不加表锁（避免死锁）
//   - 锁不销毁（进程生命周期），id 不复用
#include "lm_lock.h"
#include "lm_value.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define LOCK_INITIAL_CAP 64

typedef enum { LK_MUTEX, LK_RMUTEX, LK_RW, LK_SPIN } LockKind;

// 自旋锁：C11 原子自旋（macOS 无 pthread_spinlock_t，且自实现跨平台）
typedef struct { _Atomic int locked; } SpinLock;

static inline void spin_lock(SpinLock* s) {
    while(atomic_exchange_explicit(&s->locked, 1, memory_order_acquire)) {
        // 忙等；短临界区场景，与 pthread_spin 语义一致
    }
}
static inline int spin_trylock(SpinLock* s) {
    return !atomic_exchange_explicit(&s->locked, 1, memory_order_acquire);
}
static inline void spin_unlock(SpinLock* s) {
    atomic_store_explicit(&s->locked, 0, memory_order_release);
}

typedef struct {
    LockKind kind;
    union {
        pthread_mutex_t mutex;      // LK_MUTEX / LK_RMUTEX（RMUTEX 用 recursive attr）
        pthread_rwlock_t rw;        // LK_RW
        SpinLock spin;              // LK_SPIN（原子自旋，calloc 初始 0 = 未锁）
    } u;
} LockObj;

typedef struct {
    int used;
    LockObj* obj;   // 独立分配，扩容移动表不影响
} LockSlot;

// 条件变量：独立于锁表（pthread_cond_t 必须与 pthread_mutex 配合使用）
typedef struct { pthread_cond_t cond; } CondObj;

typedef struct {
    int used;
    CondObj* obj;   // 独立分配，扩容移动表不影响
} CondSlot;

static LockSlot* g_locks = NULL;
static int g_lock_cap = 0;
static pthread_mutex_t g_tbl_lock = PTHREAD_MUTEX_INITIALIZER;

static CondSlot* g_conds = NULL;
static int g_cond_cap = 0;
static pthread_mutex_t g_cond_tbl_lock = PTHREAD_MUTEX_INITIALIZER;

static int lock_alloc(LockKind k)
{
    pthread_mutex_lock(&g_tbl_lock);
    if(!g_locks) {
        g_locks = (LockSlot*)calloc(LOCK_INITIAL_CAP, sizeof(LockSlot));
        if(!g_locks) { pthread_mutex_unlock(&g_tbl_lock); runtime_error("lock: 内存不足"); }
        g_lock_cap = LOCK_INITIAL_CAP;
    }
    int slot = -1;
    for(int i = 0; i < g_lock_cap; i++) {
        if(!g_locks[i].used) { slot = i; break; }
    }
    if(slot < 0) {
        int newcap = g_lock_cap * 2;
        LockSlot* ns = (LockSlot*)realloc(g_locks, (size_t)newcap * sizeof(LockSlot));
        if(!ns) { pthread_mutex_unlock(&g_tbl_lock); runtime_error("lock: 锁表扩容内存不足"); }
        memset(ns + g_lock_cap, 0, (size_t)(newcap - g_lock_cap) * sizeof(LockSlot));
        g_locks = ns;
        slot = g_lock_cap;
        g_lock_cap = newcap;
    }
    LockObj* o = (LockObj*)calloc(1, sizeof(LockObj));
    if(!o) { pthread_mutex_unlock(&g_tbl_lock); runtime_error("lock: 内存不足"); }
    o->kind = k;
    switch(k) {
        case LK_MUTEX:   pthread_mutex_init(&o->u.mutex, NULL); break;
        case LK_RMUTEX: {
            pthread_mutexattr_t a;
            pthread_mutexattr_init(&a);
            pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
            pthread_mutex_init(&o->u.mutex, &a);
            pthread_mutexattr_destroy(&a);
            break;
        }
        case LK_RW:      pthread_rwlock_init(&o->u.rw, NULL); break;
        case LK_SPIN:    atomic_init(&o->u.spin.locked, 0); break;
    }
    g_locks[slot].used = 1;
    g_locks[slot].obj = o;
    pthread_mutex_unlock(&g_tbl_lock);
    return slot;   // 锁 id = 槽位下标
}

// 校验 id → 返回独立分配的锁对象指针（表锁内取；对象生命周期为进程，指针稳定）
static LockObj* lock_get(int id)
{
    pthread_mutex_lock(&g_tbl_lock);
    LockObj* o = NULL;
    if(id >= 0 && id < g_lock_cap && g_locks && g_locks[id].used)
        o = g_locks[id].obj;
    pthread_mutex_unlock(&g_tbl_lock);
    return o;
}

int lumin_mutex_create(void)    { return lock_alloc(LK_MUTEX); }
int lumin_rmutex_create(void)   { return lock_alloc(LK_RMUTEX); }
int lumin_rwlock_create(void)   { return lock_alloc(LK_RW); }
int lumin_spinlock_create(void) { return lock_alloc(LK_SPIN); }

void lumin_lock(int id)
{
    LockObj* o = lock_get(id);
    if(!o) runtime_error("lock(): 无效的锁id（未创建或已销毁）");
    switch(o->kind) {
        case LK_MUTEX: case LK_RMUTEX: pthread_mutex_lock(&o->u.mutex); break;
        case LK_SPIN:  spin_lock(&o->u.spin); break;
        case LK_RW:    runtime_error("lock(): 读写锁请用 rdlock()/wrlock()");
    }
}

void lumin_unlock(int id)
{
    LockObj* o = lock_get(id);
    if(!o) runtime_error("unlock(): 无效的锁id（未创建或已销毁）");
    switch(o->kind) {
        case LK_MUTEX: case LK_RMUTEX: pthread_mutex_unlock(&o->u.mutex); break;
        case LK_RW:    pthread_rwlock_unlock(&o->u.rw); break;
        case LK_SPIN:  spin_unlock(&o->u.spin); break;
    }
}

int lumin_trylock(int id)
{
    LockObj* o = lock_get(id);
    if(!o) runtime_error("trylock(): 无效的锁id（未创建或已销毁）");
    switch(o->kind) {
        case LK_MUTEX: case LK_RMUTEX: return pthread_mutex_trylock(&o->u.mutex) == 0;
        case LK_SPIN:  return spin_trylock(&o->u.spin);
        case LK_RW:    runtime_error("trylock(): 读写锁请用 rdlock()/wrlock()");
    }
    return 0;
}

void lumin_rdlock(int id)
{
    LockObj* o = lock_get(id);
    if(!o) runtime_error("rdlock(): 无效的锁id（未创建或已销毁）");
    if(o->kind != LK_RW) runtime_error("rdlock(): 只适用于读写锁（rwlock() 创建）");
    pthread_rwlock_rdlock(&o->u.rw);
}

void lumin_wrlock(int id)
{
    LockObj* o = lock_get(id);
    if(!o) runtime_error("wrlock(): 无效的锁id（未创建或已销毁）");
    if(o->kind != LK_RW) runtime_error("wrlock(): 只适用于读写锁（rwlock() 创建）");
    pthread_rwlock_wrlock(&o->u.rw);
}

int lumin_tryrdlock(int id)
{
    LockObj* o = lock_get(id);
    if(!o) runtime_error("tryrdlock(): 无效的锁id（未创建或已销毁）");
    if(o->kind != LK_RW) runtime_error("tryrdlock(): 只适用于读写锁（rwlock() 创建）");
    return pthread_rwlock_tryrdlock(&o->u.rw) == 0;
}

int lumin_trywrlock(int id)
{
    LockObj* o = lock_get(id);
    if(!o) runtime_error("trywrlock(): 无效的锁id（未创建或已销毁）");
    if(o->kind != LK_RW) runtime_error("trywrlock(): 只适用于读写锁（rwlock() 创建）");
    return pthread_rwlock_trywrlock(&o->u.rw) == 0;
}

// ===================== 条件变量 =====================
// 条件表：与锁表同构——CondObj 独立 malloc，表只存指针；realloc 扩容不影响已下发 id。
// 条件变量与锁（mutex/rmutex）配合：cond_wait 原子释放锁并阻塞，唤醒后重新获取锁。

static int cond_alloc(void)
{
    pthread_mutex_lock(&g_cond_tbl_lock);
    if(!g_conds) {
        g_conds = (CondSlot*)calloc(LOCK_INITIAL_CAP, sizeof(CondSlot));
        if(!g_conds) { pthread_mutex_unlock(&g_cond_tbl_lock); runtime_error("condvar: 内存不足"); }
        g_cond_cap = LOCK_INITIAL_CAP;
    }
    int slot = -1;
    for(int i = 0; i < g_cond_cap; i++) {
        if(!g_conds[i].used) { slot = i; break; }
    }
    if(slot < 0) {
        int newcap = g_cond_cap * 2;
        CondSlot* ns = (CondSlot*)realloc(g_conds, (size_t)newcap * sizeof(CondSlot));
        if(!ns) { pthread_mutex_unlock(&g_cond_tbl_lock); runtime_error("condvar: 条件表扩容内存不足"); }
        memset(ns + g_cond_cap, 0, (size_t)(newcap - g_cond_cap) * sizeof(CondSlot));
        g_conds = ns;
        slot = g_cond_cap;
        g_cond_cap = newcap;
    }
    CondObj* o = (CondObj*)calloc(1, sizeof(CondObj));
    if(!o) { pthread_mutex_unlock(&g_cond_tbl_lock); runtime_error("condvar: 内存不足"); }
    pthread_cond_init(&o->cond, NULL);
    g_conds[slot].used = 1;
    g_conds[slot].obj = o;
    pthread_mutex_unlock(&g_cond_tbl_lock);
    return slot;   // 条件 id = 槽位下标
}

static CondObj* cond_get(int id)
{
    pthread_mutex_lock(&g_cond_tbl_lock);
    CondObj* o = NULL;
    if(id >= 0 && id < g_cond_cap && g_conds && g_conds[id].used)
        o = g_conds[id].obj;
    pthread_mutex_unlock(&g_cond_tbl_lock);
    return o;
}

int lumin_condvar_create(void) { return cond_alloc(); }

void lumin_cond_wait(int cond, int lock)
{
    CondObj* c = cond_get(cond);
    if(!c) runtime_error("cond_wait(): 无效的条件id（未创建或已销毁）");
    LockObj* o = lock_get(lock);
    if(!o) runtime_error("cond_wait(): 无效的锁id（未创建或已销毁）");
    if(o->kind == LK_RW)  runtime_error("cond_wait(): 读写锁不能配条件变量（无互斥阻塞语义），请用 mutex()/rmutex()");
    if(o->kind == LK_SPIN) runtime_error("cond_wait(): 自旋锁不能配条件变量（忙等无阻塞释放），请用 mutex()/rmutex()");
    // LK_MUTEX / LK_RMUTEX：pthread_cond_wait 原子释放一次锁并阻塞，唤醒后重新获取
    // （递归锁释放一次、唤醒后重获一次，与 pthread 语义一致）
    pthread_cond_wait(&c->cond, &o->u.mutex);
}

void lumin_cond_signal(int cond)
{
    CondObj* c = cond_get(cond);
    if(!c) runtime_error("cond_signal(): 无效的条件id（未创建或已销毁）");
    pthread_cond_signal(&c->cond);
}

void lumin_cond_broadcast(int cond)
{
    CondObj* c = cond_get(cond);
    if(!c) runtime_error("cond_broadcast(): 无效的条件id（未创建或已销毁）");
    pthread_cond_broadcast(&c->cond);
}
