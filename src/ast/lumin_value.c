#include "lumin_value.h"
#include <string.h>

jmp_buf* g_err_jmp = NULL;
char g_err_msg[1024] = {0};
char g_err_type[64] = "RuntimeError";   // 最近一次错误/throw 的类型名（GET_ERR 构造错误对象用）
/* 调用栈回溯记录（VM 的 OPC_CALL / C 生成函数入口 push，返回 pop；longjmp 后由 TRY 层截断） */
const char* g_trace[64];
int g_trace_n = 0;
/* C 生成通道的 try/catch 处理器栈（VM 通道用 vm.c 的 vm_jbs，互不干扰） */
jmp_buf __g_jbs[64];
jmp_buf* __g_prev[64];
int __g_depth = 0;
int __g_sp0[64];
int __g_tgt[64];
int __g_tn[64];        /* 每层 TRY 时的调用栈深度（GET_ERR 截断残留） */
int __g_fn[64];        /* 每层 TRY 时的 finally 完成栈深度 */
int __g_fin_act[64];   /* finally 完成动作：1=JMP 2=RETHROW 3=BREAK 4=CONT 5=RETURN */
int __g_fin_tgt[64];
int __g_fin_n = 0;
Value __g_pend_val;    /* 挂起返回的值（PEND_RETURN 存，FINISH act5 恢复） */

// 运行时错误：有 try 处理器则恢复（longjmp），否则打印并退出
void runtime_error(const char* msg) {
    if(g_err_jmp) {
        snprintf(g_err_type, sizeof(g_err_type), "%s", "RuntimeError");
        snprintf(g_err_msg, sizeof(g_err_msg), "%s", msg);
        longjmp(*g_err_jmp, 1);
    }
    fprintf(stderr, "Runtime Error: %s\n", msg);
    exit(EXIT_FAILURE);
}

// 错误对象构造：type/message/stack（stack 可为空，内部复制）
Value lumin_make_error(const char* type, const char* msg, const char* stack) {
    Value v;
    v.type = VAL_ERROR;
    v.v.err.type = strdup(type ? type : "Error");
    v.v.err.message = strdup(msg ? msg : "");
    v.v.err.stack = strdup(stack ? stack : "");
    return v;
}

// 当前调用栈回溯文本（malloc，调用方 free）：at func 逐行
char* lumin_build_stack_trace(void) {
    if(g_trace_n <= 0) { char* e = (char*)malloc(1); e[0] = '\0'; return e; }
    size_t cap = 256;
    for(int i = 0; i < g_trace_n; i++) cap += strlen(g_trace[i]) + 16;
    char* out = (char*)malloc(cap);
    size_t w = 0;
    for(int i = g_trace_n - 1; i >= 0; i--) {
        if(w) out[w++] = '\n';
        const char* nm = g_trace[i] ? g_trace[i] : "<anonymous>";
        w += (size_t)snprintf(out + w, cap - w, "at %s", nm);
    }
    out[w] = '\0';
    return out;
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
    case VAL_ERROR:
        dst.v.err.type = strdup(src->v.err.type ? src->v.err.type : "");
        dst.v.err.message = strdup(src->v.err.message ? src->v.err.message : "");
        dst.v.err.stack = strdup(src->v.err.stack ? src->v.err.stack : "");
        break;

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
    case VAL_ERROR: return "error";
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
    case VAL_ERROR: printf("[error:%s] %s", v->v.err.type ? v->v.err.type : "", v->v.err.message ? v->v.err.message : ""); break;
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
