#include "gc_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static GCObject* gc_head = NULL;

GCObject* gc_alloc(size_t payload_size, GCObjTag tag)
{
    size_t total = sizeof(GCObject) + payload_size;
    GCObject* p = malloc(total);
    if (!p) {
        fprintf(stderr,"GC out of memory\n");
        abort();
    }
    p->tag = tag;
    p->marked = 0;
    /* 无锁头插：CAS 保证多线程分配时链表不损坏（当前 gc_collect 无调用点，
     * 链表只增不减；未来启用回收时再评估每线程堆或加锁方案） */
    for (;;) {
        GCObject* old = gc_head;
        p->next = old;
        if (__sync_bool_compare_and_swap(&gc_head, old, p)) break;
    }
    return p;
}

void gc_mark_object(GCObject* obj)
{
    if (!obj) return;
    if (obj->marked) return;
    obj->marked = 1;
    // 字符串无内部引用；数组后续在这里递归标记子元素
}

void gc_sweep(void)
{
    GCObject** pp = &gc_head;
    while (*pp)
    {
        GCObject* cur = *pp;
        if (!cur->marked)
        {
            *pp = cur->next;
            free(cur);
        }
        else
        {
            cur->marked = 0;
            pp = &cur->next;
        }
    }
}

void gc_collect(void)
{
    /* 当前简化版：无栈根扫描，仅sweep
     * 注意：过早调用会释放仍在使用对象，用户手动控制时机
     */
    gc_sweep();
}

char* gc_new_str(const char* s)
{
    size_t len = strlen(s);
    GCObject* hdr = gc_alloc(len + 1, GC_OBJ_STRING);
    char* buf = (char*)(hdr + 1);
    memcpy(buf, s, len);
    buf[len] = '\0';
    return buf;
}

char* lumin_concat(const char* s1, const char* s2)
{
    size_t l1 = strlen(s1);
    size_t l2 = strlen(s2);
    GCObject* hdr = gc_alloc(l1 + l2 + 1, GC_OBJ_STRING);
    char* out = (char*)(hdr + 1);
    memcpy(out, s1, l1);
    memcpy(out + l1, s2, l2);
    out[l1 + l2] = '\0';
    return out;
}
