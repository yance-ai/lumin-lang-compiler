// lm_thread.c —— 多线程运行时：线程表 + 通用线程启动/join
// 依赖上一批多线程预备：执行器状态 _Thread_local（vm.c / lumin_value.c）、GC CAS 头插
#include "lm_thread.h"
#include "lm_value.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    pthread_t handle;
    int used;       // 槽位占用
    int done;       // 线程已退出（结果已写）
    int id;         // 用户可见线程 id（自增）
    Value result;   // 线程返回值（join 时克隆取走）
} ThreadSlot;

static ThreadSlot g_slots[LM_MAX_THREADS];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_next_id = 0;

typedef struct {
    ThreadBody body;
    void* data;
    int slot;
    int argc;
    Value* args;    // 克隆参数（线程退出后释放数组本身；值本体与全库一致不主动 free）
} ThreadJob;

static void* lm_thread_main(void* p)
{
    ThreadJob* job = (ThreadJob*)p;
    ThreadLaunch t;
    t.slot = job->slot;
    t.argc = job->argc;
    t.args = job->args;
    t.data = job->data;
    job->body(&t);   // 线程体：执行函数调用 + lumin_thread_set_result
    pthread_mutex_lock(&g_lock);
    g_slots[job->slot].done = 1;
    pthread_mutex_unlock(&g_lock);
    free(job->args);
    free(job);
    return NULL;
}

int lumin_thread_start(ThreadBody body, void* data, const Value* args, int argc)
{
    pthread_mutex_lock(&g_lock);
    int slot = -1;
    for(int i = 0; i < LM_MAX_THREADS; i++) {
        if(!g_slots[i].used) { slot = i; break; }
    }
    if(slot < 0) {
        pthread_mutex_unlock(&g_lock);
        runtime_error("thread: 线程表已满(64)，请 thread_join 已结束的线程");
    }
    g_slots[slot].used = 1;
    g_slots[slot].done = 0;
    g_slots[slot].result = val_none();
    g_slots[slot].id = ++g_next_id;
    int tid = g_slots[slot].id;
    pthread_mutex_unlock(&g_lock);

    Value* cargs = (argc > 0) ? (Value*)malloc((size_t)argc * sizeof(Value)) : NULL;
    if(argc > 0 && !cargs) {
        pthread_mutex_lock(&g_lock);
        g_slots[slot].used = 0;
        pthread_mutex_unlock(&g_lock);
        runtime_error("thread: 内存不足");
    }
    for(int i = 0; i < argc; i++) cargs[i] = val_clone(&args[i]);

    ThreadJob* job = (ThreadJob*)malloc(sizeof(ThreadJob));
    if(!job) {
        free(cargs);
        pthread_mutex_lock(&g_lock);
        g_slots[slot].used = 0;
        pthread_mutex_unlock(&g_lock);
        runtime_error("thread: 内存不足");
    }
    job->body = body;
    job->data = data;
    job->slot = slot;
    job->argc = argc;
    job->args = cargs;

    if(pthread_create(&g_slots[slot].handle, NULL, lm_thread_main, job) != 0) {
        pthread_mutex_lock(&g_lock);
        g_slots[slot].used = 0;
        pthread_mutex_unlock(&g_lock);
        free(cargs);
        free(job);
        runtime_error("thread: 创建线程失败");
    }
    return tid;
}

void lumin_thread_set_result(ThreadLaunch* t, Value r)
{
    pthread_mutex_lock(&g_lock);
    g_slots[t->slot].result = val_clone(&r);
    pthread_mutex_unlock(&g_lock);
}

// C 生成端线程体：直接调函数指针
static void lm_c_thread_body(ThreadLaunch* t)
{
    Value (*cf)(Value*, int) = (Value(*)(Value*, int))t->data;
    Value r = cf(t->args, t->argc);
    lumin_thread_set_result(t, r);
}

int lumin_thread_start_c(Value (*cf)(Value*, int), const Value* args, int argc)
{
    return lumin_thread_start(lm_c_thread_body, (void*)cf, args, argc);
}

Value lumin_thread_join(int id)
{
    pthread_mutex_lock(&g_lock);
    int slot = -1;
    for(int i = 0; i < LM_MAX_THREADS; i++) {
        if(g_slots[i].used && g_slots[i].id == id) { slot = i; break; }
    }
    if(slot < 0) {
        pthread_mutex_unlock(&g_lock);
        runtime_error("thread_join: 无效的线程id（不存在或已 join）");
    }
    pthread_t h = g_slots[slot].handle;
    pthread_mutex_unlock(&g_lock);

    pthread_join(h, NULL);   // 等待线程退出（此时 result 已写）

    pthread_mutex_lock(&g_lock);
    Value r = val_clone(&g_slots[slot].result);
    g_slots[slot].used = 0;
    g_slots[slot].done = 0;
    pthread_mutex_unlock(&g_lock);
    return r;
}
