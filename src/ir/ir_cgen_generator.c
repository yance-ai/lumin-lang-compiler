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
    fprintf(out, "    Value (*next)(void*, Value);  /* next() 函数指针（通用接口，包装生成器可调用） */\n");
    fprintf(out, "    int __state;          /* 执行状态：0=初始, 1..n=yield点, -1=结束（与包装生成器布局一致） */\n");
    fprintf(out, "    Value __send_val;     /* send() 发送的值，receive() 返回 */\n");
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
    fprintf(out, "static Value lumyr_gen_%s_next(void* __gptr, Value __send_val)\n{\n",
            fn->name);
    fprintf(out, "    lumyr_gen_%s* g = (lumyr_gen_%s*)__gptr;\n", fn->name, fn->name);
    fprintf(out, "    if(g->__state == -1) return val_none();\n");

    /* 设置生成器上下文：保存 send 值，标记在生成器内部 */
    fprintf(out, "    __g_gen_send_val = __send_val;\n");
    fprintf(out, "    __g_gen_in_generator = 1;\n");

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
    fprintf(out, "    __g_gen_in_generator = 0;  /* 离开生成器上下文 */\n");
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
    fprintf(out, "    __g_gen_in_generator = 0;  /* 暂停时离开生成器上下文 */\n");
    fprintf(out, "    g->__state = %d;\n", yield_id);
    fprintf(out, "    g->__sp = __sp;\n");
    fprintf(out, "    return __stk[__sp - 1];\n");
    fprintf(out, "__gen_yield_%d:\n", yield_id);
    /* 从 yield 恢复：如果有 send 值，压入栈 */
    fprintf(out, "    if(__send_val.type != VAL_NONE) { __stk[__sp++] = __send_val; }\n");
}


/*
 * 生成包装生成器（组合操作）的运行时支持代码
 */
void emit_gen_wrapper_support(void) {
    fprintf(out, "/* ===== 生成器组合操作运行时支持 ===== */\n");
    fprintf(out, "typedef enum { WRAP_NONE=0, WRAP_MAP=1, WRAP_FILTER=2, WRAP_SKIP=3, WRAP_TAKE=4, WRAP_ENUMERATE=5, WRAP_CHAIN=6, WRAP_ZIP=7 } WrapType;\n");
    fprintf(out, "typedef struct lumyr_gen_wrap {\n");
    fprintf(out, "    Value (*next)(void*, Value);\n");
    fprintf(out, "    int __state;\n");
    fprintf(out, "    int is_wrapped;\n");
    fprintf(out, "    int wrap_type;\n");
    fprintf(out, "    void* wrapped_gen;\n");
    fprintf(out, "    void* wrapped_gen2;\n");
    fprintf(out, "    Value wrap_fn;\n");
    fprintf(out, "    int wrap_arg;\n");
    fprintf(out, "    int wrap_index;\n");
    fprintf(out, "} lumyr_gen_wrap;\n\n");

    fprintf(out, "static Value lumyr_gen_wrap_next(void* __gptr, Value __send_val) {\n");
    fprintf(out, "    lumyr_gen_wrap* g = (lumyr_gen_wrap*)__gptr;\n");
    fprintf(out, "    if(g->__state == -1) return val_none();\n");
    fprintf(out, "    Value (*wnext)(void*, Value) = *(Value(**)(void*,Value))g->wrapped_gen;\n");
    fprintf(out, "    switch(g->wrap_type) {\n");

    fprintf(out, "        case WRAP_MAP: {\n");
    fprintf(out, "            Value v = wnext(g->wrapped_gen, val_none());\n");
    fprintf(out, "            if(v.type == VAL_NONE) { g->__state = -1; return val_none(); }\n");
    fprintf(out, "            Value args[1]; args[0] = v;\n");
    fprintf(out, "            Value (*fn)(Value*,int) = (Value(*)(Value*,int))((RuntimeFunc*)g->wrap_fn.v.func.func_obj)->entry;\n");
    fprintf(out, "            return fn(args, 1);\n");
    fprintf(out, "        }\n");

    fprintf(out, "        case WRAP_FILTER: {\n");
    fprintf(out, "            while(1) {\n");
    fprintf(out, "                Value v = wnext(g->wrapped_gen, val_none());\n");
    fprintf(out, "                if(v.type == VAL_NONE) { g->__state = -1; return val_none(); }\n");
    fprintf(out, "                Value args[1]; args[0] = v;\n");
    fprintf(out, "                Value (*fn)(Value*,int) = (Value(*)(Value*,int))((RuntimeFunc*)g->wrap_fn.v.func.func_obj)->entry;\n");
    fprintf(out, "                Value r = fn(args, 1);\n");
    fprintf(out, "                if(lumyr_to_bool(r)) return v;\n");
    fprintf(out, "            }\n");
    fprintf(out, "        }\n");

    fprintf(out, "        case WRAP_SKIP: {\n");
    fprintf(out, "            while(g->wrap_index < g->wrap_arg) {\n");
    fprintf(out, "                Value v = wnext(g->wrapped_gen, val_none());\n");
    fprintf(out, "                if(v.type == VAL_NONE) { g->__state = -1; return val_none(); }\n");
    fprintf(out, "                g->wrap_index++;\n");
    fprintf(out, "            }\n");
    fprintf(out, "            Value v = wnext(g->wrapped_gen, val_none());\n");
    fprintf(out, "            if(v.type == VAL_NONE) { g->__state = -1; return val_none(); }\n");
    fprintf(out, "            return v;\n");
    fprintf(out, "        }\n");

    fprintf(out, "        case WRAP_TAKE: {\n");
    fprintf(out, "            if(g->wrap_index >= g->wrap_arg) { g->__state = -1; return val_none(); }\n");
    fprintf(out, "            Value v = wnext(g->wrapped_gen, val_none());\n");
    fprintf(out, "            if(v.type == VAL_NONE) { g->__state = -1; return val_none(); }\n");
    fprintf(out, "            g->wrap_index++;\n");
    fprintf(out, "            return v;\n");
    fprintf(out, "        }\n");

    fprintf(out, "        case WRAP_ENUMERATE: {\n");
    fprintf(out, "            Value v = wnext(g->wrapped_gen, val_none());\n");
    fprintf(out, "            if(v.type == VAL_NONE) { g->__state = -1; return val_none(); }\n");
    fprintf(out, "            Value arr = val_array(2);\n");
    fprintf(out, "            arr.v.array->items[0] = lumyr_make_int(g->wrap_index);\n");
    fprintf(out, "            arr.v.array->items[1] = v;\n");
    fprintf(out, "            g->wrap_index++;\n");
    fprintf(out, "            return arr;\n");
    fprintf(out, "        }\n");

    fprintf(out, "        case WRAP_CHAIN: {\n");
    fprintf(out, "            if(g->wrap_index == 0) {\n");
    fprintf(out, "                Value v = wnext(g->wrapped_gen, val_none());\n");
    fprintf(out, "                if(v.type != VAL_NONE) return v;\n");
    fprintf(out, "                g->wrap_index = 1;\n");
    fprintf(out, "            }\n");
    fprintf(out, "            Value (*wnext2)(void*,Value) = *(Value(**)(void*,Value))g->wrapped_gen2;\n");
    fprintf(out, "            Value v = wnext2(g->wrapped_gen2, val_none());\n");
    fprintf(out, "            if(v.type == VAL_NONE) { g->__state = -1; return val_none(); }\n");
    fprintf(out, "            return v;\n");
    fprintf(out, "        }\n");

    fprintf(out, "        case WRAP_ZIP: {\n");
    fprintf(out, "            Value (*wnext2)(void*,Value) = *(Value(**)(void*,Value))g->wrapped_gen2;\n");
    fprintf(out, "            Value v1 = wnext(g->wrapped_gen, val_none());\n");
    fprintf(out, "            Value v2 = wnext2(g->wrapped_gen2, val_none());\n");
    fprintf(out, "            if(v1.type == VAL_NONE || v2.type == VAL_NONE) { g->__state = -1; return val_none(); }\n");
    fprintf(out, "            Value arr = val_array(2);\n");
    fprintf(out, "            arr.v.array->items[0] = v1;\n");
    fprintf(out, "            arr.v.array->items[1] = v2;\n");
    fprintf(out, "            return arr;\n");
    fprintf(out, "        }\n");

    fprintf(out, "        default: g->__state = -1; return val_none();\n");
    fprintf(out, "    }\n");
    fprintf(out, "}\n\n");

    fprintf(out, "static Value lumyr_wrap_create(int wtype, Value g1, Value g2, Value fn, int arg) {\n");
    fprintf(out, "    if(g1.type != VAL_GENERATOR) runtime_error(\"组合操作第一个参数必须是生成器\");\n");
    fprintf(out, "    lumyr_gen_wrap* wg = (lumyr_gen_wrap*)calloc(1, sizeof(lumyr_gen_wrap));\n");
    fprintf(out, "    wg->next = lumyr_gen_wrap_next;\n");
    fprintf(out, "    wg->is_wrapped = 1;\n");
    fprintf(out, "    wg->wrap_type = wtype;\n");
    fprintf(out, "    wg->wrapped_gen = g1.v.generator;\n");
    fprintf(out, "    if(g2.type == VAL_GENERATOR) wg->wrapped_gen2 = g2.v.generator;\n");
    fprintf(out, "    wg->wrap_fn = fn;\n");
    fprintf(out, "    wg->wrap_arg = arg;\n");
    fprintf(out, "    wg->wrap_index = 0;\n");
    fprintf(out, "    Value gv; gv.type = VAL_GENERATOR; gv.v.generator = (void*)wg;\n");
    fprintf(out, "    return gv;\n");
    fprintf(out, "}\n\n");
    fprintf(out, "/* ===== 生成器组合操作支持结束 ===== */\n\n");
}
