/*
 * ir_cgen_generator.c - 生成器函数 C 代码生成（状态机重写）
 *
 * 将 gen func 转换为：
 *   1. 状态机结构体（保存所有局部变量、栈、状态）
 *   2. 创建函数（分配结构体并初始化参数）
 *   3. next() 函数（switch 状态 + goto 恢复执行）
 *   4. OPC_YIELD 处保存状态并返回
 *
 * 零成本抽象：编译期状态机重写，运行时无 VM 开销
 */
#include "ir_cgen_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 生成器状态计数 */
int g_gen_yield_count = 0;
int g_is_generator = 0;  /* 当前是否在生成器函数中 */

/*
 * 统计函数中 OPC_YIELD 的数量
 */
int count_yields(const BytecodeFunc* fn) {
    int cnt = 0;
    for(int i = 0; i < fn->code_len; i++) {
        if(fn->code[i].op == OPC_YIELD) cnt++;
    }
    return cnt;
}

/*
 * 生成生成器状态机结构体定义
 */
void emit_gen_struct(BytecodeFunc* fn) {
    int maxd = bc_analyze_stack(fn, NULL, 0);
    int total = fn->param_cnt + (fn->has_variadic ? 1 : 0);

    fprintf(out, "/* 生成器状态机结构体：%s */\n", fn->name);
    fprintf(out, "struct lumyr_gen_%s {\n", fn->name);
    fprintf(out, "    Value (*next)(struct lumyr_gen_%s*, Value);  /* next() 函数指针 */\n", fn->name);
    fprintf(out, "    int __state;          /* 执行状态：0=初始, 1..n=yield点, -1=结束 */\n");
    fprintf(out, "    int __sp;             /* 操作数栈指针 */\n");
    fprintf(out, "    Value __stk[%d];      /* 操作数栈 */\n", maxd + 2);

    /* 参数 */
    for(int i = 0; i < total; i++) {
        if(fn->params[i]) {
            fprintf(out, "    Value lmloc_%s;       /* 参数 */\n", fn->params[i]);
        }
    }

    /* 局部变量 */
    for(int i = 0; i < fn_locals.count; i++) {
        fprintf(out, "    Value lmloc_%s;       /* 局部变量 */\n", fn_locals.names[i]);
    }

    fprintf(out, "};\n\n");
}

/*
 * 生成生成器创建函数
 */
void emit_gen_create(BytecodeFunc* fn) {
    int total = fn->param_cnt + (fn->has_variadic ? 1 : 0);
    int has_caps = lambda_has_captures(fn->name);

    fprintf(out, "/* 创建生成器实例 */\n");
    fprintf(out, "static lumyr_gen_%s* lumyr_gen_%s_create(", fn->name, fn->name);
    if(has_caps) fprintf(out, "Value** __caps");
    for(int i = 0; i < total; i++) {
        if(has_caps || i) fprintf(out, ", ");
        fprintf(out, "Value lmloc_%s", fn->params[i]);
    }
    fprintf(out, ")\n{\n");
    fprintf(out, "    lumyr_gen_%s* g = (lumyr_gen_%s*)malloc(sizeof(lumyr_gen_%s));\n",
            fn->name, fn->name, fn->name);
    fprintf(out, "    memset(g, 0, sizeof(lumyr_gen_%s));\n", fn->name);
    fprintf(out, "    g->next = lumyr_gen_%s_next;\n", fn->name);
    fprintf(out, "    g->__state = 0;\n");
    fprintf(out, "    g->__sp = 0;\n");

    /* 初始化参数 */
    for(int i = 0; i < total; i++) {
        if(fn->params[i]) {
            fprintf(out, "    g->lmloc_%s = lmloc_%s;\n", fn->params[i], fn->params[i]);
        }
    }

    fprintf(out, "    return g;\n");
    fprintf(out, "}\n\n");
}

/*
 * 生成生成器 next() 函数的开头（switch 状态 + goto 标签）
 */
void emit_gen_next_header(BytecodeFunc* fn) {
    int nyields = count_yields(fn);
    int maxd = bc_analyze_stack(fn, NULL, 0);

    fprintf(out, "/* 生成器 next() 函数：恢复执行并返回 yield 值 */\n");
    fprintf(out, "static Value lumyr_gen_%s_next(lumyr_gen_%s* g, Value __send_val)\n{\n",
            fn->name, fn->name);
    fprintf(out, "    if(g->__state == -1) return val_none();\n");

    /* 从状态机恢复栈和栈指针 */
    fprintf(out, "    Value* __stk = g->__stk;\n");
    fprintf(out, "    int __sp = g->__sp;\n");

    /* 函数边界保存（简化版：生成器暂不支持 try-catch-finally） */
    fprintf(out, "    int __g_d0 = __g_depth; jmp_buf* __g_gj0 = g_err_jmp; int __g_fin0 = __g_fin_n;\n");
    fprintf(out, "    g_trace_push(\"%s\");\n", fn->name);

    /* 状态 switch：跳转到对应 yield 点 */
    if(nyields > 0) {
        fprintf(out, "    switch(g->__state) {\n");
        fprintf(out, "        case 0: goto __gen_start;\n");
        for(int i = 1; i <= nyields; i++) {
            fprintf(out, "        case %d: goto __gen_yield_%d;\n", i, i);
        }
        fprintf(out, "        default: return val_none();\n");
        fprintf(out, "    }\n");
    }

    fprintf(out, "__gen_start:\n");
    fprintf(out, "    (void)__send_val;\n");
}

/*
 * 生成生成器 next() 函数的结尾
 */
void emit_gen_next_footer(BytecodeFunc* fn) {
    fprintf(out, "    g->__state = -1;  /* 标记为已结束 */\n");
    fprintf(out, "    return val_none();\n");
    fprintf(out, "}\n\n");
}

/*
 * 生成 OPC_YIELD 指令的 C 代码
 * 保存状态，返回栈顶值
 */
void emit_gen_yield(BytecodeFunc* fn, int yield_id) {
    fprintf(out, "    /* YIELD #%d：保存状态并返回 */\n", yield_id);
    fprintf(out, "    g->__state = %d;\n", yield_id);
    fprintf(out, "    g->__sp = __sp;\n");
    fprintf(out, "    return __stk[__sp - 1];\n");
    fprintf(out, "__gen_yield_%d:\n", yield_id);
    /* 从 yield 恢复：如果有 send 值，压入栈 */
    fprintf(out, "    if(__send_val.type != VAL_NONE) { __stk[__sp++] = __send_val; }\n");
}
