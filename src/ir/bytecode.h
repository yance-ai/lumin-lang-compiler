// lumin-lang 字节码 IR 定义
// 基于栈的 VM：表达式求值压栈，跳转指令用绝对 pc 目标。
#ifndef LUMIN_IR_BYTECODE_H
#define LUMIN_IR_BYTECODE_H

#include "ast/lumin_value_type.h"
#include "ast/lumin_value.h"

typedef enum {
    OPC_NOP,
    OPC_LOAD_CONST,     // a=常量池下标
    OPC_LOAD_VAR,       // a=符号表下标
    OPC_STORE_VAR,      // a=符号表下标；弹值写变量（深拷贝入帧），原值压回（表达式值）
    OPC_ADD, OPC_SUB, OPC_MUL, OPC_DIV,
    OPC_GT, OPC_LT, OPC_GE, OPC_LE, OPC_EQ, OPC_NE,
    OPC_NEG, OPC_POS,
    OPC_PRE_INC, OPC_POST_INC, OPC_PRE_DEC, OPC_POST_DEC,  // a=符号表下标
    OPC_CAST_INT, OPC_CAST_DOUBLE, OPC_CAST_CHAR, OPC_CAST_BOOL, OPC_CAST_STRING, OPC_CAST_ASCII,
    OPC_PRINT,        // 打印栈顶，不弹出
    OPC_TO_BOOL,      // 弹1压1 bool
    OPC_DUP,          // 复制栈顶
    OPC_POP,          // 丢弃栈顶
    OPC_JMP,          // a=目标pc
    OPC_JMP_IF_FALSE, // a=目标pc；弹条件，假则跳
    OPC_JMP_IF_TRUE,  // a=目标pc；弹条件，真则跳
    OPC_CALL,         // a=函数名符号下标，b=实参个数
    OPC_RETURN,       // 弹值返回（深拷贝）
    OPC_RETURN_NIL,   // 无返回值返回
    OPC_HALT
} OpCode;

typedef struct {
    OpCode op;
    int a;
    int b;
} Instruction;

// 一个可执行单元：main 或一个 lum 函数
typedef struct {
    const char* name;          // 函数名（main 为 NULL）
    int is_main;
    Instruction* code;
    int code_len, code_cap;
    char** syms;               // 符号名池（变量名/函数名）
    int sym_cnt, sym_cap;
    Value* consts;             // 常量池
    int const_cnt, const_cap;
    char** params;             // 参数名（普通参数在前，可变参数最后）
    int param_cnt;             // 普通参数个数
    int has_variadic;
} BytecodeFunc;

BytecodeFunc* bytecode_func_new(const char* name, int is_main);
void bytecode_func_free(BytecodeFunc* fn);
int bf_sym(BytecodeFunc* fn, const char* name);
int bf_const(BytecodeFunc* fn, Value v);
void bf_emit(BytecodeFunc* fn, OpCode op, int a, int b);
int bf_emit_here(BytecodeFunc* fn, OpCode op, int a, int b);
void bf_patch(BytecodeFunc* fn, int pos, int target);

#endif // LUMIN_IR_BYTECODE_H
