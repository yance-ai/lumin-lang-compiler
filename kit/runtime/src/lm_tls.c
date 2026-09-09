// lm_tls.c —— 线程局部变量运行时：每线程一张 名字→值 表（pthread_key 管理）
// 安全设计：
//   - 每线程的表只由该线程访问（TLS），无需加锁
//   - 线程退出时 pthread_key destructor 自动清理表内字符串/值资源
//   - 表动态扩容，无硬上限（受系统内存限制）
#include "lm_tls.h"
#include "lm_value.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    char* name;    // strdup 副本
    Value val;     // 深拷贝（val_clone）隔离，跨线程不可见
} TLSSlot;

typedef struct {
    TLSSlot* slots;
    int cnt;
    int cap;
} TLSMap;

static pthread_key_t g_tls_key;
static pthread_once_t g_tls_once = PTHREAD_ONCE_INIT;

static void tls_map_destroy(void* p)
{
    TLSMap* m = (TLSMap*)p;
    if(!m) return;
    for(int i = 0; i < m->cnt; i++) {
        free(m->slots[i].name);
        if(m->slots[i].val.type == VAL_FUNC) {
            m->slots[i].val.type = VAL_NONE;   // 函数引用语义，不销毁
            continue;
        }
        val_destroy(&m->slots[i].val);
    }
    free(m->slots);
    free(m);
}

static void tls_key_init(void)
{
    pthread_key_create(&g_tls_key, tls_map_destroy);
}

static TLSMap* tls_map_cur(void)
{
    pthread_once(&g_tls_once, tls_key_init);
    TLSMap* m = (TLSMap*)pthread_getspecific(g_tls_key);
    if(!m) {
        m = (TLSMap*)calloc(1, sizeof(TLSMap));
        if(!m) { perror("threadlocal"); exit(EXIT_FAILURE); }
        pthread_setspecific(g_tls_key, m);
    }
    return m;
}

static int tls_find(TLSMap* m, const char* name)
{
    for(int i = 0; i < m->cnt; i++) {
        if(strcmp(m->slots[i].name, name) == 0) return i;
    }
    return -1;
}

void lumin_tls_set(const char* name, Value v)
{
    if(!name) runtime_error("threadlocal_set(): 名字参数不能为空");
    TLSMap* m = tls_map_cur();
    int idx = tls_find(m, name);
    Value c = val_clone(&v);          // 深拷贝：线程间值隔离
    if(idx >= 0) {
        if(m->slots[idx].val.type != VAL_FUNC) val_destroy(&m->slots[idx].val);
        m->slots[idx].val = c;
        return;
    }
    if(m->cnt >= m->cap) {
        int nc = m->cap > 0 ? m->cap * 2 : 16;
        TLSSlot* ns = (TLSSlot*)realloc(m->slots, (size_t)nc * sizeof(TLSSlot));
        if(!ns) { perror("threadlocal"); exit(EXIT_FAILURE); }
        m->slots = ns;
        m->cap = nc;
    }
    m->slots[m->cnt].name = strdup(name);
    m->slots[m->cnt].val = c;
    m->cnt++;
}

Value lumin_tls_get(const char* name)
{
    if(!name) runtime_error("threadlocal_get(): 名字参数不能为空");
    TLSMap* m = tls_map_cur();
    int idx = tls_find(m, name);
    if(idx < 0) {
        char buf[512];
        snprintf(buf, sizeof(buf), "threadlocal_get(): 当前线程未初始化的线程局部变量: %s", name);
        runtime_error(buf);
    }
    return m->slots[idx].val;         // 浅拷贝返回（表内值仅当前线程持有，安全）
}
