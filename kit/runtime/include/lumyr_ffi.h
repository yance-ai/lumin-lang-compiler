/*
 * lumyr_ffi.h - FFI (Foreign Function Interface) 外部函数接口
 *
 * 支持动态加载 C 库并调用外部函数。
 * 编译通道：直接生成 extern 声明和 C 函数调用
 * VM 通道：通过动态加载库 + 函数指针调用
 */
#ifndef LUMYR_FFI_H
#define LUMYR_FFI_H

#include "lm_value.h"

#ifdef __cplusplus
extern "C" {
#endif

/* FFI 类型枚举 */
typedef enum {
    FFI_VOID = 0,
    FFI_INT,
    FFI_DOUBLE,
    FFI_BOOL,
    FFI_STRING,
    FFI_PTR
} FFIType;

/* FFI 函数签名 */
struct FFIFunc {
    char* name;           /* 函数名 */
    char* libname;        /* 库名（NULL 表示从当前进程符号表查找） */
    FFIType ret_type;     /* 返回值类型 */
    int param_count;      /* 参数数量 */
    FFIType* param_types; /* 参数类型数组 */
    void* func_ptr;       /* 函数指针（运行时解析） */
    void* lib_handle;     /* 库句柄（运行时加载） */
};

/* 加载库（Windows: LoadLibrary, POSIX: dlopen） */
void* lumyr_ffi_load_lib(const char* libname);

/* 获取函数地址（Windows: GetProcAddress, POSIX: dlsym） */
void* lumyr_ffi_get_func(void* lib_handle, const char* funcname);

/* 调用 FFI 函数（支持最多 6 个参数） */
Value lumyr_ffi_call(FFIFunc* func, Value* args, int argc);

/* 创建 FFI 函数对象 */
FFIFunc* lumyr_ffi_func_create(const char* name, const char* libname,
                                 FFIType ret_type, FFIType* param_types, int param_count);

/* 释放 FFI 函数对象 */
void lumyr_ffi_func_free(FFIFunc* func);

/* 类型名转换 */
FFIType lumyr_ffi_type_from_name(const char* name);
const char* lumyr_ffi_type_to_name(FFIType type);

#ifdef __cplusplus
}
#endif

#endif /* LUMYR_FFI_H */
