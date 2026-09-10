// lumyr-lang IR 层 peephole 优化：常量折叠（constant folding）
//
// 设计：线性重建式扫描。
//   逐旧指令读出，维护输出缓冲区 out[] 与"旧 pc → 新 pc"映射 map[]。
//   遇到二元运算时，回看输出缓冲区末尾两条：若都是 LOAD_CONST，则用运行时
//   lumyr_* 把它们折成一条 LOAD_CONST；一元/强转运算回看末尾一条。
//   因为折叠结果直接写回 out[]，后续运算立刻能看到新常量，天然级联
//   （1 + 2*3 单遍即折成 7），无需外层多轮扫描。
//
// 跳转修正：折叠会缩短指令序列，所有用绝对 pc 的指令目标必须重定位。
//   重建时为每条旧指令记录其新位置（被折叠掉的指令指向折叠结果那条 LOAD_CONST），
//   重建完成后遍历新指令流，按 map[] 修正 JMP / JMP_IF_* / TRY / ENDTRY /
//   FIN_PUSH.b / PEND_RETURN.b 的目标字段。
//
// 语义安全：
//   - 折叠直接调用运行时 lumyr_add/sub/.../lumyr_cast_*，与执行期逐位一致；
//   - DIV/MOD 右操作数为 0 时不折叠，保留运行期 inf/NaN 行为；
//   - 仅折叠纯算术/比较/一元/强转，这些运算无副作用。
#include "ir_opt.h"
#include "lm_value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------- 折叠核心（复用运行时语义） ----------------

// 右操作数是否为 0（int 0 或 double 0.0，含 -0.0）
static int rhs_is_zero(Value v)
{
    if(v.type == VAL_INT)    return v.v.i == 0;
    if(v.type == VAL_DOUBLE) return v.v.d == 0.0;
    return 0;
}

// 尝试折叠二元运算。成功返回 1 并写 *out；除零/不支持返回 0。
static int fold_binop(OpCode op, Value l, Value r, Value* out)
{
    switch(op) {
        case OPC_ADD: *out = lumyr_add(l, r); return 1;
        case OPC_SUB: *out = lumyr_sub(l, r); return 1;
        case OPC_MUL: *out = lumyr_mul(l, r); return 1;
        case OPC_DIV:
            if(rhs_is_zero(r)) return 0;          // 保留除零运行期行为
            *out = lumyr_div(l, r); return 1;
        case OPC_MOD:
            if(rhs_is_zero(r)) return 0;
            *out = lumyr_mod(l, r); return 1;
        case OPC_GT: *out = lumyr_gt(l, r); return 1;
        case OPC_LT: *out = lumyr_lt(l, r); return 1;
        case OPC_GE: *out = lumyr_ge(l, r); return 1;
        case OPC_LE: *out = lumyr_le(l, r); return 1;
        case OPC_EQ: *out = lumyr_eq(l, r); return 1;
        case OPC_NE: *out = lumyr_ne(l, r); return 1;
        default: return 0;
    }
}

// 是否为二元运算 opcode（用于回看判定）
static int is_binop_op(OpCode op)
{
    switch(op) {
        case OPC_ADD: case OPC_SUB: case OPC_MUL: case OPC_DIV: case OPC_MOD:
        case OPC_GT: case OPC_LT: case OPC_GE: case OPC_LE: case OPC_EQ: case OPC_NE:
            return 1;
        default: return 0;
    }
}

// 尝试折叠一元运算。成功返回 1 并写 *out。
static int fold_unary(OpCode op, Value v, Value* out)
{
    switch(op) {
        case OPC_NEG:         *out = lumyr_unary_minus(v); return 1;
        case OPC_POS:         *out = lumyr_unary_plus(v);  return 1;
        case OPC_LOGIC_NOT:   *out = lumyr_logic_not(v);   return 1;
        default: return 0;
    }
}

// 尝试折叠强转运算。成功返回 1 并写 *out。
static int fold_cast(OpCode op, Value v, Value* out)
{
    switch(op) {
        case OPC_CAST_INT:    *out = lumyr_cast_int(v);    return 1;
        case OPC_CAST_DOUBLE: *out = lumyr_cast_double(v); return 1;
        case OPC_CAST_BOOL:   *out = lumyr_cast_bool(v);   return 1;
        case OPC_CAST_STRING: *out = lumyr_cast_string(v); return 1;
        case OPC_CAST_CHAR:   *out = lumyr_cast_char(v);   return 1;
        case OPC_CAST_ASCII:  *out = lumyr_cast_ascii(v);  return 1;
        case OPC_CAST_BYTE:   *out = lumyr_cast_byte(v);   return 1;
        case OPC_CAST_INT8:   *out = lumyr_cast_int8(v);   return 1;
        case OPC_CAST_INT16:  *out = lumyr_cast_int16(v);  return 1;
        case OPC_CAST_INT32:  *out = lumyr_cast_int32(v);  return 1;
        case OPC_CAST_INT64:  *out = lumyr_cast_int64(v);  return 1;
        case OPC_CAST_UINT8:  *out = lumyr_cast_uint8(v);  return 1;
        case OPC_CAST_UINT16: *out = lumyr_cast_uint16(v); return 1;
        case OPC_CAST_UINT32: *out = lumyr_cast_uint32(v); return 1;
        case OPC_CAST_UINT64: *out = lumyr_cast_uint64(v); return 1;
        case OPC_CAST_LONG:   *out = lumyr_cast_long(v);   return 1;
        case OPC_CAST_LONGLONG: *out = lumyr_cast_longlong(v); return 1;
        case OPC_CAST_FLOAT:  *out = lumyr_cast_float(v);  return 1;
        default: return 0;
    }
}

// ---------------- 主 pass ----------------

int ir_opt_constant_fold(BytecodeFunc* fn)
{
    if(!fn || fn->code_len <= 0) return 0;
    int n = fn->code_len;

    // out[]：重建后的指令流（折叠只会变短，上限 n）
    Instruction* out = (Instruction*)malloc(sizeof(Instruction) * (size_t)n);
    // map[]：旧 pc → 新 pc（被折叠掉的旧指令指向折叠结果那条 LOAD_CONST）
    int* map = (int*)malloc(sizeof(int) * (size_t)n);
    if(!out || !map) { perror("ir_opt_constant_fold"); exit(EXIT_FAILURE); }

    int np = 0;   // out 下一个写入位置
    for(int i = 0; i < n; i++) {
        Instruction in = fn->code[i];

        // --- 二元折叠：out[np-2], out[np-1] 均为 LOAD_CONST ---
        if(is_binop_op(in.op) && np >= 2 &&
           out[np-2].op == OPC_LOAD_CONST && out[np-1].op == OPC_LOAD_CONST)
        {
            Value l = fn->consts[out[np-2].a];
            Value r = fn->consts[out[np-1].a];
            Value res;
            if(fold_binop(in.op, l, r, &res)) {
                int ci = bf_const(fn, res);
                out[np-2].op = OPC_LOAD_CONST;
                out[np-2].a  = ci;
                out[np-2].b  = 0;
                np--;                       // 丢弃 out[np-1]（右操作数常量）
                // 折叠结果位于 out[np-2]（原 np 计法），np-- 后即 out[np-1]
                int folded_at = np - 1;
                map[i]   = folded_at;       // 本 BINOP 旧 pc
                map[i-1] = folded_at;       // 右操作数旧 pc
                map[i-2] = folded_at;       // 左操作数旧 pc
                continue;
            }
        }

        // --- 一元/强转折叠：out[np-1] 为 LOAD_CONST ---
        if(np >= 1 && out[np-1].op == OPC_LOAD_CONST) {
            int folded = 0;
            Value res;
            if(fold_unary(in.op, fn->consts[out[np-1].a], &res) ||
               fold_cast(in.op,  fn->consts[out[np-1].a], &res))
            {
                int ci = bf_const(fn, res);
                out[np-1].op = OPC_LOAD_CONST;
                out[np-1].a  = ci;
                out[np-1].b  = 0;
                map[i]   = np - 1;
                map[i-1] = np - 1;
                folded = 1;
            }
            if(folded) continue;
        }

        // --- 普通保留 ---
        out[np] = in;
        map[i] = np;
        np++;
    }

    // 原地替换 code
    int changed = (np != n);
    free(fn->code);
    fn->code     = out;
    fn->code_len = np;
    fn->code_cap = n;

    // ---------------- 跳转目标修正 ----------------
    // 旧 pc 全部落在 [0, n)；目标字段原值是旧 pc，按 map[] 重定位到新 pc。
    // 哨兵 0（TRY.a=无 catch / TRY.b=无 finally / PEND_RETURN.b=直接返回）
    // 映射 map[0]=0（函数入口永远在新 pc 0），保持 0 不变，语义安全。
    for(int i = 0; i < np; i++) {
        Instruction* p = &fn->code[i];
        switch(p->op) {
            case OPC_JMP:
            case OPC_JMP_IF_FALSE:
            case OPC_JMP_IF_TRUE:
                if(p->a >= 0 && p->a < n) p->a = map[p->a];
                break;
            case OPC_TRY:
                if(p->a > 0 && p->a < n) p->a = map[p->a];
                if(p->b > 0 && p->b < n) p->b = map[p->b];
                break;
            case OPC_ENDTRY:
                if(p->a >= 0 && p->a < n) p->a = map[p->a];
                break;
            case OPC_FIN_PUSH:
                if(p->b > 0 && p->b < n) p->b = map[p->b];   // a=完成动作码，不动
                break;
            case OPC_PEND_RETURN:
                if(p->b > 0 && p->b < n) p->b = map[p->b];   // b=0 表示直接返回
                break;
            default: break;
        }
    }

    free(map);
    return changed;
}

/* ========================================================================
 * 复用工具：按旧→新位置映射重定位各类跳转/异常目标字段
 * ======================================================================== */
static void remap_targets(BytecodeFunc* fn, int np, int n, const int* map)
{
    (void)n;
    for(int i = 0; i < np; i++) {
        Instruction* p = &fn->code[i];
        switch(p->op) {
            case OPC_JMP:
            case OPC_JMP_IF_FALSE:
            case OPC_JMP_IF_TRUE:
                if(p->a >= 0 && p->a < n && map[p->a] >= 0) p->a = map[p->a];
                break;
            case OPC_TRY:
                if(p->a > 0 && p->a < n && map[p->a] >= 0) p->a = map[p->a];
                if(p->b > 0 && p->b < n && map[p->b] >= 0) p->b = map[p->b];
                break;
            case OPC_ENDTRY:
                if(p->a > 0 && p->a < n && map[p->a] >= 0) p->a = map[p->a];
                break;
            case OPC_FIN_PUSH:
            case OPC_PEND_RETURN:
                if(p->b > 0 && p->b < n && map[p->b] >= 0) p->b = map[p->b];
                break;
            default: break;
        }
    }
}

/* ========================================================================
 * Pass B：常量条件分支折叠
 *   `LOAD_CONST c; JMP_IF_FALSE T` / `LOAD_CONST c; JMP_IF_TRUE T`
 *   - 条件恒真/恒假时，折叠为无条件 JMP 或直接删除（落到下一指令）。
 *   栈平衡：原序列 LOAD_CONST(+1) + JMP_IF_*(−1) = 净 0；
 *           折叠为 JMP(净0) 或 空(净0)，栈深不变。
 *   安全：若该对指令任一位于块入口（可能被跳转跳入），不折叠。
 * ======================================================================== */
static int branch_fold_pass(BytecodeFunc* fn)
{
    int n = fn->code_len;
    if(n == 0) return 0;

    /* is_target[i]：指令 i 是否为某条跳转/异常路径的目标（块入口） */
    char* is_target = (char*)calloc((size_t)n, 1);
    int* map = (int*)malloc(sizeof(int) * (size_t)n);
    char* dead = (char*)calloc((size_t)n, 1);
    if(!is_target || !map || !dead) { perror("branch_fold"); exit(EXIT_FAILURE); }

    for(int i = 0; i < n; i++) {
        Instruction in = fn->code[i];
        int t;
        switch(in.op) {
            case OPC_JMP: case OPC_JMP_IF_FALSE: case OPC_JMP_IF_TRUE:
                t = in.a; if(t >= 0 && t < n) is_target[t] = 1; break;
            case OPC_TRY:
                if(in.a > 0 && in.a < n) is_target[in.a] = 1;
                if(in.b > 0 && in.b < n) is_target[in.b] = 1;
                break;
            case OPC_ENDTRY:
                if(in.a > 0 && in.a < n) is_target[in.a] = 1;
                break;
            case OPC_FIN_PUSH: case OPC_PEND_RETURN:
                if(in.b > 0 && in.b < n) is_target[in.b] = 1;
                break;
            default: break;
        }
    }

    int changed = 0;
    for(int i = 0; i + 1 < n; i++) {
        if(fn->code[i].op != OPC_LOAD_CONST) continue;
        Instruction br = fn->code[i+1];
        if(br.op != OPC_JMP_IF_FALSE && br.op != OPC_JMP_IF_TRUE) continue;
        /* 块入口不折叠：避免把"跳入中间"的语义改坏 */
        if(is_target[i] || is_target[i+1]) continue;

        Value c = fn->consts[fn->code[i].a];
        int cond = lumyr_to_bool(c);
        int taken = (br.op == OPC_JMP_IF_TRUE) ? cond : !cond;

        if(taken) {
            /* 恒跳转：对改写为 JMP T，删除原条件跳转 */
            fn->code[i].op = OPC_JMP;
            fn->code[i].a  = br.a;
            fn->code[i].b  = 0;
            dead[i+1] = 1;
        } else {
            /* 永不跳转：两条都删，落到 i+2 */
            dead[i]   = 1;
            dead[i+1] = 1;
        }
        changed = 1;
    }

    free(is_target);
    if(!changed) { free(map); free(dead); return 0; }

    /* 压缩：旧 pc -> 新 pc（被删指令记 -1） */
    int newlen = 0;
    for(int i = 0; i < n; i++) map[i] = dead[i] ? -1 : newlen++;

    Instruction* out = (Instruction*)malloc(sizeof(Instruction) * (size_t)(newlen ? newlen : 1));
    if(!out) { perror("branch_fold out"); exit(EXIT_FAILURE); }
    int k = 0;
    for(int i = 0; i < n; i++) {
        if(dead[i]) continue;
        out[k] = fn->code[i];
        k++;
    }
    free(fn->code);
    fn->code     = out;
    fn->code_len = newlen;
    fn->code_cap = newlen;
    remap_targets(fn, newlen, n, map);
    free(map);
    free(dead);
    return 1;
}

/* ========================================================================
 * Pass C：死代码消除（DCE）
 *   从 pc=0 出发做可达性分析（与 bc_analyze_stack 的后继规则一致），
 *   删除所有不可达指令。块入口（catch/finally 等异常路径）均纳入可达。
 * ======================================================================== */
static void compute_reachable(BytecodeFunc* fn, char* reach)
{
    int n = fn->code_len;
    memset(reach, 0, (size_t)n);
    if(n == 0) return;
    reach[0] = 1;
    int changed = 1;
    while(changed) {
        changed = 0;
        for(int i = 0; i < n; i++) {
            if(!reach[i]) continue;
            Instruction in = fn->code[i];
            /* 顺序后继 */
            if(in.op != OPC_RETURN && in.op != OPC_RETURN_NIL &&
               in.op != OPC_HALT && in.op != OPC_JMP) {
                if(i + 1 < n && !reach[i+1]) { reach[i+1] = 1; changed = 1; }
            }
            int t;
            switch(in.op) {
                case OPC_JMP: case OPC_JMP_IF_FALSE: case OPC_JMP_IF_TRUE:
                    t = in.a; if(t >= 0 && t < n && !reach[t]) { reach[t] = 1; changed = 1; }
                    break;
                case OPC_TRY:
                    if(in.a > 0 && in.a < n && !reach[in.a]) { reach[in.a] = 1; changed = 1; }
                    if(in.b > 0 && in.b < n && !reach[in.b]) { reach[in.b] = 1; changed = 1; }
                    break;
                case OPC_ENDTRY:
                    if(in.a > 0 && in.a < n && !reach[in.a]) { reach[in.a] = 1; changed = 1; }
                    break;
                case OPC_FIN_PUSH: case OPC_PEND_RETURN:
                    if(in.b > 0 && in.b < n && !reach[in.b]) { reach[in.b] = 1; changed = 1; }
                    break;
                default: break;
            }
        }
    }
}

static int dce_pass(BytecodeFunc* fn)
{
    int n = fn->code_len;
    if(n == 0) return 0;
    char* reach = (char*)malloc((size_t)n);
    int* map = (int*)malloc(sizeof(int) * (size_t)n);
    if(!reach || !map) { perror("dce"); exit(EXIT_FAILURE); }

    compute_reachable(fn, reach);

    int newlen = 0;
    for(int i = 0; i < n; i++) map[i] = reach[i] ? newlen++ : -1;
    if(newlen == n) { free(reach); free(map); return 0; }

    Instruction* out = (Instruction*)malloc(sizeof(Instruction) * (size_t)(newlen ? newlen : 1));
    if(!out) { perror("dce out"); exit(EXIT_FAILURE); }
    int k = 0;
    for(int i = 0; i < n; i++) {
        if(!reach[i]) continue;
        out[k++] = fn->code[i];
    }
    free(fn->code);
    fn->code     = out;
    fn->code_len = newlen;
    fn->code_cap = newlen;
    remap_targets(fn, newlen, n, map);
    free(reach);
    free(map);
    return 1;
}

/* ========================================================================
 * Pass D：简单常量传播
 *   线性扫描，跟踪 `LOAD_CONST c; STORE_VAR s` 存入常量的变量。
 *   其后 LOAD_VAR s 替换为 LOAD_CONST c。
 *   保守失效：
 *     - 非 LOAD_CONST 形式的 STORE_VAR / 前后自增自减 → 清该变量；
 *     - CALL/CALLV/BUILTIN/MKCLOSURE → 清空全部（调用可能改任意变量）；
 *     - 任意跳转/返回 → 清空全部（跨基本块不追踪）。
 *   前置 DCE 已删除不可达代码，线性扫描不会穿过死块误传播。
 * ======================================================================== */
static int propagate_pass(BytecodeFunc* fn)
{
    int n = fn->code_len;
    if(n == 0) return 0;
    int nsym = fn->sym_cnt;
    if(nsym == 0) return 0;

    /* is_target[i]：i 是某条跳转/异常路径的目标（基本块入口）。
     * 在块入口处，变量可能由多条前驱边到达，常量到达的假设不成立，
     * 必须清空已知常量——这正是防止把循环头的 induction 变量错当常量的关键。 */
    char* is_target = (char*)calloc((size_t)n, 1);
    int* known = (int*)malloc(sizeof(int) * (size_t)nsym);
    if(!is_target || !known) { perror("propagate"); exit(EXIT_FAILURE); }
    for(int i = 0; i < nsym; i++) known[i] = -1;

    for(int i = 0; i < n; i++) {
        Instruction in = fn->code[i];
        int t;
        switch(in.op) {
            case OPC_JMP: case OPC_JMP_IF_FALSE: case OPC_JMP_IF_TRUE:
                t = in.a; if(t >= 0 && t < n) is_target[t] = 1; break;
            case OPC_TRY:
                if(in.a > 0 && in.a < n) is_target[in.a] = 1;
                if(in.b > 0 && in.b < n) is_target[in.b] = 1;
                break;
            case OPC_ENDTRY:
                if(in.a > 0 && in.a < n) is_target[in.a] = 1;
                break;
            case OPC_FIN_PUSH: case OPC_PEND_RETURN:
                if(in.b > 0 && in.b < n) is_target[in.b] = 1;
                break;
            default: break;
        }
    }

    int changed = 0;
    for(int i = 0; i < n; i++) {
        Instruction* p = &fn->code[i];

        /* 基本块入口：清空全部已知常量（跨边不可信） */
        if(is_target[i]) memset(known, -1, sizeof(int) * (size_t)nsym);

        switch(p->op) {
            case OPC_LOAD_CONST:
                /* 紧接着 STORE_VAR → 该变量取常量值 */
                if(i + 1 < n && fn->code[i+1].op == OPC_STORE_VAR)
                    known[fn->code[i+1].a] = p->a;
                break;
            case OPC_STORE_VAR:
                /* 仅当不是 `LOAD_CONST; STORE_VAR` 配对时才失效该变量 */
                if(!(i >= 1 && fn->code[i-1].op == OPC_LOAD_CONST))
                    known[p->a] = -1;
                break;
            case OPC_PRE_INC: case OPC_POST_INC:
            case OPC_PRE_DEC: case OPC_POST_DEC:
                known[p->a] = -1;
                break;
            case OPC_LOAD_VAR:
                if(p->a >= 0 && p->a < nsym && known[p->a] >= 0) {
                    p->op = OPC_LOAD_CONST;
                    p->a  = known[p->a];
                    p->b  = 0;
                    changed = 1;
                }
                break;
            case OPC_CALL: case OPC_CALLV: case OPC_BUILTIN: case OPC_MKCLOSURE:
            case OPC_JMP: case OPC_JMP_IF_FALSE: case OPC_JMP_IF_TRUE:
            case OPC_RETURN: case OPC_RETURN_NIL: case OPC_HALT:
                memset(known, -1, sizeof(int) * (size_t)nsym);
                break;
            default: break;
        }
    }
    free(is_target);
    free(known);
    return changed;
}

/* ========================================================================
 * 优化驱动：迭代至收敛
 *
 * 启用：Pass A 常量折叠 → Pass C DCE → 再常量折叠（DCE 后可能暴露新折叠机会）。
 * Pass B 分支折叠已并入常量折叠。
 * Pass D 常量传播存在循环归纳变量误传播缺陷，暂不启用。
 * 环境变量 LM_OPT=0 可整体关闭优化（调试用）。
 * ======================================================================== */
void ir_optimize(BytecodeFunc* fn)
{
    if(!fn || fn->code_len <= 0) return;

    const char* env = getenv("LM_OPT");
    if(env && strcmp(env, "0") == 0) return;

    for(int iter = 0; iter < 16; iter++) {
        int changed = ir_opt_constant_fold(fn);   // Pass A：常量折叠
        changed |= dce_pass(fn);                  // Pass C：死代码消除
        if(!changed) break;
    }

    /* 自校验：栈深分析不得报告下溢（返回 -1） */
    int after_max = bc_analyze_stack(fn, NULL, 0);
    if(after_max < 0) {
        fprintf(stderr, "ir_optimize 警告: 函数 %s 优化后栈深分析异常（下溢）\n",
                fn->name ? fn->name : "<main>");
    }
}
