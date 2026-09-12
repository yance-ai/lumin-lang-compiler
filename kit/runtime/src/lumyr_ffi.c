/*
 * lumyr_ffi.c - FFI (Foreign Function Interface) 外部函数接口实现
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

/* 加载库 */
void* lumyr_ffi_load_lib(const char* libname) {
    if(!libname) return NULL;
#ifdef _WIN32
    return (void*)LoadLibraryA(libname);
#else
    return dlopen(libname, RTLD_LAZY | RTLD_GLOBAL);
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

/* 调用 FFI 函数（支持最多 6 个参数） */
Value lumyr_ffi_call(FFIFunc* func, Value* args, int argc) {
    if(!func || !func->func_ptr) {
        fprintf(stderr, "FFI Error: function '%s' not resolved\n", func->name ? func->name : "?");
        return val_none();
    }

    /* 转换参数为 C 类型 */
    long long iargs[6] = {0};
    double dargs[6] = {0};
    const char* sargs[6] = {0};

    for(int i = 0; i < argc && i < 6; i++) {
        switch(func->param_types[i]) {
            case FFI_INT:
                iargs[i] = (long long)value_as_number(args[i]);
                break;
            case FFI_DOUBLE:
                dargs[i] = value_as_number(args[i]);
                break;
            case FFI_BOOL:
                iargs[i] = lumyr_to_bool(args[i]) ? 1 : 0;
                break;
            case FFI_STRING:
                if(args[i].type == VAL_STRING) {
                    sargs[i] = lumyr_str_cstr(&args[i]);
                } else {
                    sargs[i] = "";
                }
                break;
            default:
                iargs[i] = 0;
                break;
        }
    }

    /* 根据参数数量和类型调用函数（用 union 转换函数指针） */
    typedef long long (*func0_t)(void);
    typedef long long (*func1_t)(long long);
    typedef long long (*func2_t)(long long, long long);
    typedef long long (*func3_t)(long long, long long, long long);
    typedef long long (*func4_t)(long long, long long, long long, long long);
    typedef long long (*func5_t)(long long, long long, long long, long long, long long);
    typedef long long (*func6_t)(long long, long long, long long, long long, long long, long long);

    /* 简化实现：所有参数都按 long long 传递（对于 int/double/string 指针都适用） */
    long long cargs[6];
    for(int i = 0; i < argc && i < 6; i++) {
        if(func->param_types[i] == FFI_DOUBLE) {
            /* double 按位复制到 long long */
            memcpy(&cargs[i], &dargs[i], sizeof(double));
        } else if(func->param_types[i] == FFI_STRING) {
            cargs[i] = (long long)(size_t)sargs[i];
        } else {
            cargs[i] = iargs[i];
        }
    }

    long long ret = 0;
    switch(argc) {
        case 0: ret = ((func0_t)func->func_ptr)(); break;
        case 1: ret = ((func1_t)func->func_ptr)(cargs[0]); break;
        case 2: ret = ((func2_t)func->func_ptr)(cargs[0], cargs[1]); break;
        case 3: ret = ((func3_t)func->func_ptr)(cargs[0], cargs[1], cargs[2]); break;
        case 4: ret = ((func4_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3]); break;
        case 5: ret = ((func5_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4]); break;
        case 6: ret = ((func6_t)func->func_ptr)(cargs[0], cargs[1], cargs[2], cargs[3], cargs[4], cargs[5]); break;
        default:
            fprintf(stderr, "FFI Error: too many arguments (%d), max 6\n", argc);
            return val_none();
    }

    /* 转换返回值 */
    switch(func->ret_type) {
        case FFI_INT:
            return val_int((int64_t)ret);
        case FFI_DOUBLE: {
            double dret;
            memcpy(&dret, &ret, sizeof(double));
            return val_double(dret);
        }
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

/* 创建 FFI 函数对象 */
FFIFunc* lumyr_ffi_func_create(const char* name, const char* libname,
                                 FFIType ret_type, FFIType* param_types, int param_count) {
    FFIFunc* func = (FFIFunc*)calloc(1, sizeof(FFIFunc));
    if(!func) return NULL;

    func->name = name ? strdup(name) : NULL;
    func->libname = libname ? strdup(libname) : NULL;
    func->ret_type = ret_type;
    func->param_count = param_count;
    if(param_count > 0 && param_types) {
        func->param_types = (FFIType*)malloc((size_t)param_count * sizeof(FFIType));
        memcpy(func->param_types, param_types, (size_t)param_count * sizeof(FFIType));
    }

    /* 加载库并解析函数地址 */
    if(func->libname) {
        func->lib_handle = lumyr_ffi_load_lib(func->libname);
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

/* 类型名转换 */
FFIType lumyr_ffi_type_from_name(const char* name) {
    if(!name) return FFI_INT; /* 默认按 int/指针处理（64位上指针和long long同宽） */
    if(strcmp(name, "int") == 0) return FFI_INT;
    if(strcmp(name, "double") == 0 || strcmp(name, "float") == 0) return FFI_DOUBLE;
    if(strcmp(name, "bool") == 0) return FFI_BOOL;
    if(strcmp(name, "string") == 0 || strcmp(name, "str") == 0) return FFI_STRING;
    if(strcmp(name, "void") == 0 || strcmp(name, "none") == 0) return FFI_VOID;
    return FFI_INT; /* 默认按 int 处理 */
}

const char* lumyr_ffi_type_to_name(FFIType type) {
    switch(type) {
        case FFI_INT: return "int";
        case FFI_DOUBLE: return "double";
        case FFI_BOOL: return "bool";
        case FFI_STRING: return "string";
        case FFI_VOID: return "void";
        default: return "int";
    }
}
