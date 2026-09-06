#ifndef LUMIN_VALUE_TYPE_H
#define LUMIN_VALUE_TYPE_H

#include <pthread.h>

typedef struct Value Value;
typedef struct EvalCtx EvalCtx;
// ✅ 新增前置声明：FuncEntry 参数需要 StackFrame*，此时还没完整定义 StackFrame
typedef struct StackFrame StackFrame;

// ✅ 修改：函数入口回调类型，新增 StackFrame* frame 参数
// 函数原型类型，RuntimeFunc.entry 使用 FuncEntry*
typedef Value (FuncEntry)(int arg_cnt, const Value* args, EvalCtx* ctx, StackFrame* frame);

// 运行时函数对象：所有运行时需要的信息全部在这里
typedef struct RuntimeFunc {
    FuncEntry* entry;        // 执行入口：解释器就写解释器入口；编译器替换成机器码入口
    int param_count;         // 普通参数个数
    int has_variadic;        // 新增：是否有 ...rest 可变参数
    Value* captures;         // 闭包捕获值
    int capture_count;
} RuntimeFunc;

// 强制转换类型，给 new_cast_node 使用
typedef enum {
    CAST_INT,
    CAST_DOUBLE,
    CAST_STRING,
    CAST_BOOL,
    CAST_ASCII,
    CAST_CHAR,
    CAST_BYTE     // (byte)x 强转：C 风格截断为 8 位无符号整数
} CastKind;

// 值类型：语言支持的数据类型
typedef enum {
    VAL_NONE = 0,
    VAL_INT,
    VAL_DOUBLE,
    VAL_BOOL,
    VAL_CHAR,
    VAL_STRING,
    VAL_FUNC,
    VAL_ARRAY,
    VAL_MAP,
    VAL_ERROR,     // 错误对象：type/message/stack（throw 与运行时错误统一）
    VAL_BYTE       // 8 位无符号整数（0-255，C 风格截断；算术/比较按数值类型处理）
} ValueType;

// 数组运行时对象，VAL_ARRAY 使用
typedef struct {
    Value* items;
    int len;
} ValueArray;

// 字典运行时对象，VAL_MAP 使用（线性探测哈希：字符串键 → 值）
typedef struct {
    char** keys;     // 键（strdup 堆分配）
    Value* values;   // 值（与 keys 同序）
    int len;         // 当前键数
    int cap;         // 容量
} ValueMap;

// 错误对象，VAL_ERROR 使用（type 为错误类别，message 为消息，stack 为调用栈回溯）
typedef struct {
    char* type;      // 错误类型名（"RuntimeError" / throw 自定义）
    char* message;   // 错误消息
    char* stack;     // 调用栈回溯文本（可空）
} ValueError;

// VAL_FUNC：直接持有独立运行时函数堆对象
typedef struct {
    RuntimeFunc* func_obj;
} ValueFunc;

// 运行时带标签的值（支持多类型，字符串堆分配）
struct Value {
    ValueType type;
    union {
        long long i;
        double d;
        _Bool b;
        char c;
        char* s;   // VAL_STRING：堆上字符串
        ValueFunc func;        // VAL_FUNC
        ValueArray array;      // VAL_ARRAY
        ValueMap* map;         // VAL_MAP：堆上共享对象（原地改语义与数组 items 一致）
        ValueError err;        // VAL_ERROR：错误对象（type/message/stack，堆上字符串）
    } v;
};

// 解释器执行上下文：只负责控制流 break/continue/return，不存局部变量
struct EvalCtx {
    int hit_break;
    int hit_continue;
    int hit_return;
    Value ret_val;
};

// 新增，不修改原有EvalCtx，栈帧独立
// 多线程安全：main/全局帧 shared=1，get/set/bind 走 rwlock（主线程扩容 realloc 不移动共享帧内存
// 时，其他线程读取同帧会 use-after-free，故共享帧所有访问加锁）；函数帧为线程私有 shared=0 不加锁。
typedef struct StackFrame {
    char** names;    // 动态：按需扩容，无硬上限
    Value* vals;
    int cnt;
    int cap;
    struct StackFrame* parent;
    pthread_rwlock_t rw;   // 共享帧（全局帧）读写锁；私有帧不使用
    _Bool shared;          // 1 = 全局共享帧（main 顶层帧），多线程可见
} StackFrame;

#endif //LUMIN_VALUE_TYPE_H
