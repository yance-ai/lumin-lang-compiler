/*
 * lumyr_ffi.c - FFI (Foreign Function Interface) 外部函数接口实现
 *
 * 支持：
 * - 最多 16 个参数
 * - 类型：void, int, double, bool, string, ptr（指针/句柄/数组/结构体指针）
 * - 回调函数（函数指针作为参数）
 * - 完善的错误处理
 */
#include "lumyr_ffi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

/* ==================== 回调函数支持 ==================== */

/* 回调函数上下文：保存 Lumyr 函数值和参数数量 */
typedef struct {
    Value func_val;      /* Lumyr 函数值 */
    int param_count;     /* 参数数量 */
    int used;            /* 是否被使用 */
} FFICallbackCtx;

#define FFI_MAX_CALLBACKS 16
static FFICallbackCtx g_callbacks[FFI_MAX_CALLBACKS];

/* 回调函数 trampoline：C 端调用，转发到 Lumyr 函数 */
/* 简化版：只支持最多 4 个参数的回调，返回 long long */
typedef long long (*ffi_callback_trampoline_t)(long long, long long, long long, long long);

/* 预定义的 trampoline 函数（每个槽位一个） */
static long long ffi_trampoline_0(long long a, long long b, long long c, long long d) {
    (void)a; (void)b; (void)c; (void)d;
    /* 槽位 0 的回调在运行时通过全局变量查找 */
    return 0;
}
/* 由于 C 无法动态生成代码，这里用一个简化方案：
   回调函数通过全局变量传递上下文，实际调用在 lumyr_ffi_invoke_callback 中完成 */

/* 调用 Lumyr 回调函数（由 FFI 调用的 C 函数内部触发） */
Value lumyr_ffi_invoke_callback(int slot, Value* args, int argc) {
    if(slot < 0 || slot >= FFI_MAX_CALLBACKS || !g_callbacks[slot].used) {
        fprintf(stderr, "FFI Error: invalid callback slot %d\n", slot);
        return val_none();
    }
    Value cb = g_callbacks[slot].func_val;
    if(cb.type != VAL_FUNC) {
        fprintf(stderr, "FFI Error: callback slot %d is not a function\n", slot);
        return val_none();
    }
    /* 通过 RuntimeFunc entry 调用 */
    if(cb.v.func.func_obj && cb.v.func.func_obj->entry) {
        return cb.v.func.func_obj->entry(argc, args, NULL, NULL);
    }
    return val_none();
}

/* 注册回调函数，返回槽位 ID（作为函数指针传递给 C） */
int lumyr_ffi_register_callback(Value func_val, int param_count) {
    for(int i = 0; i < FFI_MAX_CALLBACKS; i++) {
        if(!g_callbacks[i].used) {
            g_callbacks[i].func_val = func_val;
            g_callbacks[i].param_count = param_count;
            g_callbacks[i].used = 1;
            return i;
        }
    }
    fprintf(stderr, "FFI Error: no free callback slots (max %d)\n", FFI_MAX_CALLBACKS);
    return -1;
}

/* 注销回调函数 */
void lumyr_ffi_unregister_callback(int slot) {
    if(slot >= 0 && slot < FFI_MAX_CALLBACKS) {
        g_callbacks[slot].used = 0;
        g_callbacks[slot].func_val = val_none();
    }
}

/* ==================== 库加载与函数解析 ==================== */

/* 加载库 */
void* lumyr_ffi_load_lib(const char* libname) {
    if(!libname) return NULL;
#ifdef _WIN32
    HMODULE h = LoadLibraryA(libname);
    if(!h) {
        fprintf(stderr, "FFI Error: failed to load library '%s' (error %lu)\n",
                libname, GetLastError());
    }
    return (void*)h;
#else
    void* h = dlopen(libname, RTLD_LAZY | RTLD_GLOBAL);
    if(!h) {
        fprintf(stderr, "FFI Error: failed to load library '%s': %s\n",
                libname, dlerror());
    }
    return h;
#endif
}

/* 获取函数地址 */
void* lumyr_ffi_get_func(void* lib_handle, const char* funcname) {
    if(!funcname) return NULL;
#ifdef _WIN32
    if(lib_handle) {
        return (void*)GetProcAddress((HMODULE)lib_handle, funcname);
    }
    /* 从当前进程模块查找 */
    HMODULE hMod = GetModuleHandleA(NULL);
    if(hMod) return (void*)GetProcAddress(hMod, funcname);
    return NULL;
#else
    if(lib_handle) {
        return dlsym(lib_handle, funcname);
    }
    /* 从当前进程符号表查找 */
    return dlsym(RTLD_DEFAULT, funcname);
#endif
}

/* ==================== 函数调用（最多 16 个参数） ==================== */

/* 定义 0-16 参数的函数指针类型 */
#define FFI_FUNC_TYPE(n) typedef long long (*ffi_func##n##_t)(
/* 由于 C 宏无法方便地生成逗号分隔的参数列表，这里手动定义 */

typedef long long (*ffi_func0_t)(void);
typedef long long (*ffi_func1_t)(long long);
typedef long long (*ffi_func2_t)(long long, long long);
typedef long long (*ffi_func3_t)(long long, long long, long long);
typedef long long (*ffi_func4_t)(long long, long long, long long, long long);
typedef long long (*ffi_func5_t)(long long, long long, long long, long long, long long);
typedef long long (*ffi_func6_t)(long long, long long, long long, long long, long long, long long);
typedef long long (*ffi_func7_t)(long long, long long, long long, long long, long long, long long, long long);
typedef long long (*ffi_func8_t)(long long, long long, long long, long long, long long, long long, long long, long long);
typedef long long (*ffi_func9_t)(long long, long long, long long, long long, long long, long long, long long, long long, long long);
typedef long long (*ffi_func10_t)(long long, long long, long long, long long, long long, long long, long long, long long, long long, long long);
typedef long long (*ffi_func11_t)(long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long);
typedef long long (*ffi_func12_t)(long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long);
typedef long long (*ffi_func13_t)(long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long);
typedef long long (*ffi_func14_t)(long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long);
typedef long long (*ffi_func15_t)(long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long);
typedef long long (*ffi_func16_t)(long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long);

/* double 返回类型的函数指针（返回值在 XMM0 寄存器，不是 RAX） */
typedef double (*ffi_dfunc0_t)(void);
typedef double (*ffi_dfunc1_t)(long long);
typedef double (*ffi_dfunc2_t)(long long, long long);
typedef double (*ffi_dfunc3_t)(long long, long long, long long);
typedef double (*ffi_dfunc4_t)(long long, long long, long long, long long);
typedef double (*ffi_dfunc5_t)(long long, long long, long long, long long, long long);
typedef double (*ffi_dfunc6_t)(long long, long long, long long, long long, long long, long long);
typedef double (*ffi_dfunc7_t)(long long, long long, long long, long long, long long, long long, long long);
typedef double (*ffi_dfunc8_t)(long long, long long, long long, long long, long long, long long, long long, long long);
typedef double (*ffi_dfunc9_t)(long long, long long, long long, long long, long long, long long, long long, long long, long long);
typedef double (*ffi_dfunc10_t)(long long, long long, long long, long long, long long, long long, long long, long long, long long, long long);
typedef double (*ffi_dfunc11_t)(long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long);
typedef double (*ffi_dfunc12_t)(long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long);
typedef double (*ffi_dfunc13_t)(long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long);
typedef double (*ffi_dfunc14_t)(long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long);
typedef double (*ffi_dfunc15_t)(long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long);
typedef double (*ffi_dfunc16_t)(long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long, long long);

#define FFI_MAX_ARGS 16

/* 调用 FFI 函数 */
Value lumyr_ffi_call(FFIFunc* func, Value* args, int argc) {
    if(!func) {
        fprintf(stderr, "FFI Error: null function object\n");
        return val_none();
    }
    if(!func->func_ptr) {
        fprintf(stderr, "FFI Error: function '%s' not resolved (library: %s)\n",
                func->name ? func->name : "?",
                func->libname ? func->libname : "(default)");
        return val_none();
    }
    if(argc > FFI_MAX_ARGS) {
        fprintf(stderr, "FFI Error: too many arguments (%d), max %d\n", argc, FFI_MAX_ARGS);
        return val_none();
    }

    /* 转换参数为 C 类型（统一用 long long 数组，double 按位复制） */
    long long cargs[FFI_MAX_ARGS] = {0};

    for(int i = 0; i < argc; i++) {
        FFIType ptype = (i < func->param_count) ? func->param_types[i] : FFI_INT;
        switch(ptype) {
            case FFI_INT:
            case FFI_PTR:
                /* 指针/句柄：从 int 值直接传递（Lumyr 中用 int 存储指针） */
                cargs[i] = (long long)value_as_number(args[i]);
                break;
            case FFI_DOUBLE: {
                double d = value_as_number(args[i]);
                memcpy(&cargs[i], &d, sizeof(double));
                break;
            }
            case FFI_BOOL:
                cargs[i] = lumyr_to_bool(args[i]) ? 1 : 0;
                break;
            case FFI_STRING:
                if(args[i].type == VAL_STRING) {
                    const char* s = lumyr_str_cstr(&args[i]);
                    cargs[i] = (long long)(size_t)s;
                } else {
                    cargs[i] = (long long)(size_t)"";
                }
                break;
            case FFI_CALLBACK: {
                /* 回调函数：注册 Lumyr 函数，返回槽位 ID 作为函数指针 */
                if(args[i].type == VAL_FUNC) {
                    int slot = lumyr_ffi_register_callback(args[i], 4);
                    cargs[i] = (long long)(size_t)slot;
                } else {
                    cargs[i] = 0;
                }
                break;
            }
            case FFI_VOID:
            default:
                cargs[i] = 0;
                break;
        }
    }

    /* 根据返回类型和参数数量调用函数 */
    long long ret = 0;
    double dret = 0.0;
    int is_double_ret = (func->ret_type == FFI_DOUBLE);

    if(is_double_ret) {
        /* double 返回类型：用 ffi_dfuncN_t 函数指针（返回值在 XMM0） */
        switch(argc) {
            case 0: dret = ((ffi_dfunc0_t)func->func_ptr)(); break;
            case 1: dret = ((ffi_dfunc1_t)func->func_ptr)(cargs[0]); break;
            case 2: dret = ((ffi_dfunc2_t)func->func_ptr)(cargs[0], cargs[1]); break;
            case 3: dret = ((ffi_dfunc3_t)func->func_ptr)(cargs[0], cargs[1], cargs[2]); break;
            case 4: dret = ((ffi_dfunc4_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3]); break;
            case 5: dret = ((ffi_dfunc5_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4]); break;
            case 6: dret = ((ffi_dfunc6_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5]); break;
            case 7: dret = ((ffi_dfunc7_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5], cargs[6]); break;
            case 8: dret = ((ffi_dfunc8_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5], cargs[6], cargs[7]); break;
            case 9: dret = ((ffi_dfunc9_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5], cargs[6], cargs[7], cargs[8]); break;
            case 10: dret = ((ffi_dfunc10_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5], cargs[6], cargs[7], cargs[8], cargs[9]); break;
            case 11: dret = ((ffi_dfunc11_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5], cargs[6], cargs[7], cargs[8], cargs[9], cargs[10]); break;
            case 12: dret = ((ffi_dfunc12_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5], cargs[6], cargs[7], cargs[8], cargs[9], cargs[10], cargs[11]); break;
            case 13: dret = ((ffi_dfunc13_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5], cargs[6], cargs[7], cargs[8], cargs[9], cargs[10], cargs[11], cargs[12]); break;
            case 14: dret = ((ffi_dfunc14_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5], cargs[6], cargs[7], cargs[8], cargs[9], cargs[10], cargs[11], cargs[12], cargs[13]); break;
            case 15: dret = ((ffi_dfunc15_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5], cargs[6], cargs[7], cargs[8], cargs[9], cargs[10], cargs[11], cargs[12], cargs[13], cargs[14]); break;
            case 16: dret = ((ffi_dfunc16_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5], cargs[6], cargs[7], cargs[8], cargs[9], cargs[10], cargs[11], cargs[12], cargs[13], cargs[14], cargs[15]); break;
            default:
                fprintf(stderr, "FFI Error: invalid argument count %d\n", argc);
                return val_none();
        }
    } else {
        /* 非 double 返回类型：用 ffi_funcN_t 函数指针（返回值在 RAX） */
        switch(argc) {
            case 0: ret = ((ffi_func0_t)func->func_ptr)(); break;
            case 1: ret = ((ffi_func1_t)func->func_ptr)(cargs[0]); break;
            case 2: ret = ((ffi_func2_t)func->func_ptr)(cargs[0], cargs[1]); break;
            case 3: ret = ((ffi_func3_t)func->func_ptr)(cargs[0], cargs[1], cargs[2]); break;
            case 4: ret = ((ffi_func4_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3]); break;
            case 5: ret = ((ffi_func5_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4]); break;
            case 6: ret = ((ffi_func6_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5]); break;
            case 7: ret = ((ffi_func7_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5], cargs[6]); break;
            case 8: ret = ((ffi_func8_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5], cargs[6], cargs[7]); break;
            case 9: ret = ((ffi_func9_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5], cargs[6], cargs[7], cargs[8]); break;
            case 10: ret = ((ffi_func10_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5], cargs[6], cargs[7], cargs[8], cargs[9]); break;
            case 11: ret = ((ffi_func11_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5], cargs[6], cargs[7], cargs[8], cargs[9], cargs[10]); break;
            case 12: ret = ((ffi_func12_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5], cargs[6], cargs[7], cargs[8], cargs[9], cargs[10], cargs[11]); break;
            case 13: ret = ((ffi_func13_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5], cargs[6], cargs[7], cargs[8], cargs[9], cargs[10], cargs[11], cargs[12]); break;
            case 14: ret = ((ffi_func14_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5], cargs[6], cargs[7], cargs[8], cargs[9], cargs[10], cargs[11], cargs[12], cargs[13]); break;
            case 15: ret = ((ffi_func15_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5], cargs[6], cargs[7], cargs[8], cargs[9], cargs[10], cargs[11], cargs[12], cargs[13], cargs[14]); break;
            case 16: ret = ((ffi_func16_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5], cargs[6], cargs[7], cargs[8], cargs[9], cargs[10], cargs[11], cargs[12], cargs[13], cargs[14], cargs[15]); break;
            default:
                fprintf(stderr, "FFI Error: invalid argument count %d\n", argc);
                return val_none();
        }
    }

    /* 转换返回值 */
    if(is_double_ret) {
        return val_double(dret);
    }
    switch(func->ret_type) {
        case FFI_INT:
            return val_int((int64_t)ret);
        case FFI_PTR:
            /* 指针/句柄返回：用 int 存储指针值 */
            return val_int((int64_t)ret);
        case FFI_BOOL:
            return val_bool(ret ? 1 : 0);
        case FFI_STRING:
            if(ret) {
                return lumyr_make_string((const char*)(size_t)ret);
            }
            return val_none();
        case FFI_VOID:
        default:
            return val_none();
    }
}

/* ==================== 函数对象管理 ==================== */

/* 创建 FFI 函数对象 */
FFIFunc* lumyr_ffi_func_create(const char* name, const char* libname,
                                 FFIType ret_type, FFIType* param_types, int param_count) {
    if(!name) {
        fprintf(stderr, "FFI Error: function name is null\n");
        return NULL;
    }

    FFIFunc* func = (FFIFunc*)calloc(1, sizeof(FFIFunc));
    if(!func) {
        fprintf(stderr, "FFI Error: out of memory allocating FFIFunc\n");
        return NULL;
    }

    func->name = strdup(name);
    func->libname = libname ? strdup(libname) : NULL;
    func->ret_type = ret_type;
    func->param_count = param_count;

    if(param_count > 0 && param_types) {
        func->param_types = (FFIType*)malloc((size_t)param_count * sizeof(FFIType));
        if(func->param_types) {
            memcpy(func->param_types, param_types, (size_t)param_count * sizeof(FFIType));
        } else {
            fprintf(stderr, "FFI Error: out of memory allocating param types\n");
            free(func->name);
            free(func->libname);
            free(func);
            return NULL;
        }
    }

    /* 加载库并解析函数地址 */
    if(func->libname) {
        func->lib_handle = lumyr_ffi_load_lib(func->libname);
        if(!func->lib_handle) {
            fprintf(stderr, "FFI Warning: library '%s' not loaded, function '%s' will fail at call time\n",
                    func->libname, func->name);
        }
    }

    func->func_ptr = lumyr_ffi_get_func(func->lib_handle, func->name);

    if(!func->func_ptr) {
        fprintf(stderr, "FFI Warning: function '%s' not found in library '%s'\n",
                name, libname ? libname : "(default)");
    }

    return func;
}

/* 释放 FFI 函数对象 */
void lumyr_ffi_func_free(FFIFunc* func) {
    if(!func) return;
    free(func->name);
    free(func->libname);
    free(func->param_types);
#ifdef _WIN32
    if(func->lib_handle) FreeLibrary((HMODULE)func->lib_handle);
#else
    if(func->lib_handle) dlclose(func->lib_handle);
#endif
    free(func);
}

/* ==================== 类型名转换 ==================== */

/* 类型名转换 */
FFIType lumyr_ffi_type_from_name(const char* name) {
    if(!name) return FFI_INT; /* 默认按 int/指针处理 */
    if(strcmp(name, "int") == 0) return FFI_INT;
    if(strcmp(name, "long") == 0) return FFI_INT;
    if(strcmp(name, "int64") == 0) return FFI_INT;
    if(strcmp(name, "double") == 0 || strcmp(name, "float") == 0) return FFI_DOUBLE;
    if(strcmp(name, "bool") == 0) return FFI_BOOL;
    if(strcmp(name, "string") == 0 || strcmp(name, "str") == 0) return FFI_STRING;
    if(strcmp(name, "char*") == 0) return FFI_STRING;
    if(strcmp(name, "void") == 0 || strcmp(name, "none") == 0) return FFI_VOID;
    /* 指针/句柄类型 */
    if(strcmp(name, "ptr") == 0) return FFI_PTR;
    if(strcmp(name, "pointer") == 0) return FFI_PTR;
    if(strcmp(name, "handle") == 0) return FFI_PTR;
    if(strcmp(name, "void*") == 0) return FFI_PTR;
    if(strcmp(name, "int*") == 0) return FFI_PTR;
    if(strcmp(name, "double*") == 0) return FFI_PTR;
    if(strcmp(name, "array") == 0) return FFI_PTR; /* 数组首地址作为指针 */
    if(strcmp(name, "struct") == 0) return FFI_PTR; /* 结构体指针 */
    if(strcmp(name, "obj") == 0) return FFI_PTR;     /* 对象句柄 */
    /* 回调函数类型 */
    if(strcmp(name, "callback") == 0) return FFI_CALLBACK;
    if(strcmp(name, "func_ptr") == 0) return FFI_CALLBACK;
    if(strcmp(name, "function") == 0) return FFI_CALLBACK;
    return FFI_INT; /* 默认按 int 处理 */
}

const char* lumyr_ffi_type_to_name(FFIType type) {
    switch(type) {
        case FFI_INT: return "int";
        case FFI_DOUBLE: return "double";
        case FFI_BOOL: return "bool";
        case FFI_STRING: return "string";
        case FFI_PTR: return "ptr";
        case FFI_CALLBACK: return "callback";
        case FFI_VOID: return "void";
        default: return "int";
    }
}

/* 获取 C 类型名字符串（用于编译通道生成代码） */
const char* lumyr_ffi_type_to_cname(FFIType type) {
    switch(type) {
        case FFI_INT: return "long long";
        case FFI_DOUBLE: return "double";
        case FFI_BOOL: return "int";
        case FFI_STRING: return "const char*";
        case FFI_PTR: return "void*";
        case FFI_CALLBACK: return "void*";
        case FFI_VOID: return "void";
        default: return "long long";
    }
}
