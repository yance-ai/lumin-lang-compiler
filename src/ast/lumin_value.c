#include "lumin_value.h"
#include <string.h>

void runtime_error(const char* msg) {
    fprintf(stderr, "Runtime Error: %s\n", msg);
    exit(EXIT_FAILURE);
}

// -------- 值构造 --------
Value val_none(void) {
    Value r;
    r.type = VAL_NONE;
    return r;
}

Value val_int(long long v) {
    Value r;
    r.type = VAL_INT;
    r.v.i = v;
    return r;
}

Value val_double(double v) {
    Value r;
    r.type = VAL_DOUBLE;
    r.v.d = v;
    return r;
}

Value val_bool(_Bool v) {
    Value r;
    r.type = VAL_BOOL;
    r.v.b = v;
    return r;
}

Value val_char(char v) {
    Value r;
    r.type = VAL_CHAR;
    r.v.c = v;
    return r;
}

Value val_string(const char* s) {
    Value r;
    r.type = VAL_STRING;
    r.v.s = strdup(s);
    return r;
}

// ❗ 删除 val_func(AstNode* func_ast) 整个函数

Value val_array(int len) {
    Value r;
    r.type = VAL_ARRAY;
    r.v.array.len = len;
    if(len > 0) {
        r.v.array.items = (Value*)malloc(sizeof(Value) * len);
        for(int i = 0; i < len; i++) {
            r.v.array.items[i] = val_none();
        }
    } else {
        r.v.array.items = NULL;
    }
    return r;
}

Value val_map(void) {
    Value r;
    r.type = VAL_MAP;
    r.v.map = (ValueMap*)malloc(sizeof(ValueMap));
    r.v.map->len = 0;
    r.v.map->cap = 0;
    r.v.map->keys = NULL;
    r.v.map->values = NULL;
    return r;
}

// -------- 销毁 --------
void val_destroy(Value* v) {
    if(!v) return;
    switch(v->type) {
    case VAL_STRING:
        free(v->v.s);
        v->v.s = NULL;
        break;
    case VAL_ARRAY: {
        for(int i = 0; i < v->v.array.len; i++) {
            val_destroy(&v->v.array.items[i]);
        }
        free(v->v.array.items);
        v->v.array.items = NULL;
        v->v.array.len = 0;
        break;
    }
    case VAL_MAP: {
        ValueMap* m = v->v.map;
        if(m) {
            for(int i = 0; i < m->len; i++) {
                free(m->keys[i]);
                val_destroy(&m->values[i]);
            }
            free(m->keys);
            free(m->values);
            free(m);
            v->v.map = NULL;
        }
        break;
    }
    case VAL_FUNC: {
        // 释放运行时函数对象
        RuntimeFunc* f = v->v.func.func_obj;
        if(f) {
            // 销毁捕获变量
            for(int i = 0; i < f->capture_count; ++i) {
                val_destroy(&f->captures[i]);
            }
            free(f->captures);
            free(f);
        }
        break;
    }
    default:
        break;
    }
    v->type = VAL_NONE;
}

// -------- 深度拷贝 --------
Value val_clone(const Value* src) {
    Value dst;
    dst.type = src->type;
    switch(src->type) {
    case VAL_INT:    dst.v.i = src->v.i; break;
    case VAL_DOUBLE: dst.v.d = src->v.d; break;
    case VAL_BOOL:   dst.v.b = src->v.b; break;
    case VAL_CHAR:   dst.v.c = src->v.c; break;
    case VAL_STRING: dst.v.s = strdup(src->v.s); break;

    case VAL_FUNC:
        // 函数对象是引用语义，只复制指针，不复制整个RuntimeFunc
        dst.v.func.func_obj = src->v.func.func_obj;
        break;

    case VAL_ARRAY: {
        int n = src->v.array.len;
        dst = val_array(n);
        for(int i = 0; i < n; i++) {
            dst.v.array.items[i] = val_clone(&src->v.array.items[i]);
        }
        break;
    }
    case VAL_MAP: {
        ValueMap* srcm = src->v.map;
        dst = val_map();
        ValueMap* dm = dst.v.map;
        for(int i = 0; i < srcm->len; i++) {
            if(dm->len >= dm->cap) {
                int ncap = dm->cap ? dm->cap * 2 : 8;
                dm->keys = (char**)realloc(dm->keys, sizeof(char*) * ncap);
                dm->values = (Value*)realloc(dm->values, sizeof(Value) * ncap);
                dm->cap = ncap;
            }
            dm->keys[dm->len] = strdup(srcm->keys[i]);
            dm->values[dm->len] = val_clone(&srcm->values[i]);
            dm->len++;
        }
        break;
    }
    case VAL_NONE:
    default:
        dst = val_none();
        break;
    }
    return dst;
}

// -------- debug打印 --------
const char* val_typename(ValueType t) {
    switch(t) {
    case VAL_NONE: return "none";
    case VAL_INT: return "int";
    case VAL_DOUBLE: return "double";
    case VAL_BOOL: return "bool";
    case VAL_CHAR: return "char";
    case VAL_STRING: return "string";
    case VAL_FUNC: return "func";
    case VAL_ARRAY: return "array";
    case VAL_MAP: return "map";
    default: return "unknown";
    }
}

void val_print(const Value* v) {
    if(!v) { printf("(null value)"); return; }
    switch(v->type) {
    case VAL_INT: printf("%lld", v->v.i); break;
    case VAL_DOUBLE: printf("%g", v->v.d); break;
    case VAL_BOOL: printf("%s", v->v.b ? "true" : "false"); break;
    case VAL_CHAR: printf("'%c'", v->v.c); break;
    case VAL_STRING: printf("\"%s\"", v->v.s); break;
    case VAL_FUNC: printf("<func>"); break;
    case VAL_ARRAY: {
        printf("[");
        for(int i=0;i<v->v.array.len;i++){
            if(i>0)printf(",");
            val_print(&v->v.array.items[i]);
        }
        printf("]");
        break;
    }
    case VAL_NONE: printf("null"); break;
    default: printf("<?type=%d>",(int)v->type);
    }
}

// ==================== 自增自减 ====================
Value lumin_post_inc(Value* v) {
    Value old = *v;
    switch(v->type) {
        case VAL_INT:    v->v.i += 1; break;
        case VAL_DOUBLE: v->v.d += 1.0; break;
        case VAL_CHAR:   v->v.c += 1; break;
        default: runtime_error("post_inc:类型不支持++");
    }
    return old;
}

Value lumin_pre_inc(Value* v) {
    switch(v->type) {
        case VAL_INT:    v->v.i += 1; break;
        case VAL_DOUBLE: v->v.d += 1.0; break;
        case VAL_CHAR:   v->v.c += 1; break;
        default: runtime_error("pre_inc:类型不支持++");
    }
    return *v;
}

Value lumin_post_dec(Value* v) {
    Value old = *v;
    switch(v->type) {
        case VAL_INT:    v->v.i -= 1; break;
        case VAL_DOUBLE: v->v.d -= 1.0; break;
        case VAL_CHAR:   v->v.c -= 1; break;
        default: runtime_error("post_dec:类型不支持--");
    }
    return old;
}

Value lumin_pre_dec(Value* v) {
    switch(v->type) {
        case VAL_INT:    v->v.i -= 1; break;
        case VAL_DOUBLE: v->v.d -= 1.0; break;
        case VAL_CHAR:   v->v.c -= 1; break;
        default: runtime_error("pre_dec:类型不支持--");
    }
    return *v;
}
