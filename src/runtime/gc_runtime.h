#ifndef GC_RUNTIME_H
#define GC_RUNTIME_H

#include <stddef.h>

typedef enum {
    GC_OBJ_STRING,
    GC_OBJ_ARRAY
} GCObjTag;

typedef struct GCObject {
    GCObjTag tag;
    char marked;
    struct GCObject* next;
} GCObject;

// GC API
GCObject* gc_alloc(size_t payload_size, GCObjTag tag);
void gc_mark_object(GCObject* obj);
void gc_sweep(void);
void gc_collect(void);

// string helper
char* gc_new_str(const char* s);
char* lumin_concat(const char* s1, const char* s2);

#endif
