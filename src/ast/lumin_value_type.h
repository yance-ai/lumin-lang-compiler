#ifndef LUMIN_VALUE_TYPE_H
#define LUMIN_VALUE_TYPE_H

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
    CAST_CHAR
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
    VAL_MAP
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
typedef struct StackFrame {
    char* names[64];
    Value vals[64];
    int cnt;
    struct StackFrame* parent;
} StackFrame;

#endif //LUMIN_VALUE_TYPE_H
