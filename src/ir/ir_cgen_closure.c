/*
 * ir_cgen 模块：闭包装箱分析
 * 自动从 ir_cgen.c 拆分
 */
#include "ir_cgen_internal.h"

void analyze_boxing(BytecodeFunc* fn)
{
    memset(&g_boxed, 0, sizeof(g_boxed));
    memset(&g_cur_caps, 0, sizeof(g_cur_caps));
    /* 当前函数若是有捕获的 lambda，登记其捕获变量名（顺序 = __caps 下标） */
    if(lambda_has_captures(fn->name)) {
        int ncap = lambda_capture_count(fn->name);
        for(int i = 0; i < ncap; i++)
            ns_add(&g_cur_caps, lambda_capture_name(fn->name, i));
    }
    /* 扫描 OPC_MKCLOSURE：每个的 a 操作数是 lambda 名，收集其捕获变量名 */
    for(int i = 0; i < fn->code_len; i++) {
        if(fn->code[i].op != OPC_MKCLOSURE) continue;
        const char* lname = (fn->code[i].a >= 0 && fn->code[i].a < fn->sym_cnt)
                            ? fn->syms[fn->code[i].a] : NULL;
        if(!lname) continue;
        int ncap = lambda_capture_count(lname);
        for(int j = 0; j < ncap; j++) {
            const char* capnm = lambda_capture_name(lname, j);
            /* 若被捕获变量是本函数的参数/局部变量 → 需要装箱 */
            if(fn_has_param(fn, capnm) || ns_has(&fn_locals, capnm))
                ns_add(&g_boxed, capnm);
            /* 若该变量是本函数自己的捕获变量（已在 g_cur_caps），则它已经是 cell，
             * 创建内层闭包时直接透传 __caps[idx]，无需重复装箱。 */
        }
    }
}

// ---------------- 文本工具 ----------------

