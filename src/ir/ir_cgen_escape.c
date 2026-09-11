/*
 * ir_cgen 模块：逃逸分析与标量替换
 * 自动从 ir_cgen.c 拆分
 */
#include "ir_cgen_internal.h"

int ea_is_global(BytecodeFunc* fn, int sym_idx)
{
    if(sym_idx < 0 || sym_idx >= fn->sym_cnt) return 1;  /* 未知符号保守视为全局 */
    const char* nm = fn->syms[sym_idx];
    if(!nm) return 1;
    if(g_cur_fn && (fn_has_param(fn, nm) || ns_has(&fn_locals, nm))) return 0;
    return 1;  /* main 中所有变量都是全局 */
}

/* 泛化逃逸分析：对 target_op（OPC_ARRAY_LIT 或 OPC_MAP_LIT）的字面量做逃逸分析。
 * struct_result[i]=1：指令 i 处的 target_op 字面量不逃逸，结构体可栈分配。
 * items_result[i]=1：仅数组（track_items=1），items 缓冲区也可栈分配。
 * 调用方需预分配 struct_result / items_result（大小 fn->code_len，已 zero）。
 * track_items=0 时 items_result 可为 NULL。 */
void analyze_escape_for(BytecodeFunc* fn, int target_op,
                                uint8_t* struct_result, uint8_t* items_result,
                                int track_items)
{
    int n = fn->code_len;
    int other_op = (target_op == OPC_ARRAY_LIT) ? OPC_MAP_LIT : OPC_ARRAY_LIT;

    /* 1. 为每个 target_op 字面量分配 bit 位 */
    int* lit_bit = (int*)malloc(sizeof(int) * n);
    int lit_cnt = 0;
    for(int i = 0; i < n; i++) {
        lit_bit[i] = -1;
        if(fn->code[i].op == target_op) {
            if(lit_cnt < 64) {
                lit_bit[i] = lit_cnt++;
            } else {
                /* >64 个字面量：保守回退，全部堆分配 */
                free(lit_bit);
                return;
            }
        }
    }
    if(lit_cnt == 0) { free(lit_bit); return; }

    /* 2. 获取每条指令的栈深 */
    int maxd = bc_analyze_stack(fn, NULL, 0);
    int* depths = (int*)malloc(sizeof(int) * n);
    bc_analyze_stack(fn, depths, n);

    /* 3. 每指令入口状态：栈槽掩码 + 变量掩码 */
    int stk_stride = maxd + 2;
    uint64_t* entry_stk = (uint64_t*)calloc((size_t)n * stk_stride, sizeof(uint64_t));
    uint64_t* entry_var = (uint64_t*)calloc((size_t)n * fn->sym_cnt, sizeof(uint64_t));
    uint64_t* tmp_stk = (uint64_t*)malloc(sizeof(uint64_t) * stk_stride);
    uint64_t* tmp_var = (uint64_t*)malloc(sizeof(uint64_t) * (fn->sym_cnt > 0 ? fn->sym_cnt : 1));

    uint64_t escaped = 0;   /* 累积逃逸的位掩码 */
    uint64_t may_grow = 0;  /* 累积可能被扩容的位掩码（仅数组 track_items 时使用） */

    /* 4. 不动点前向传播 */
    for(int iter = 0; iter < 20; iter++) {
        int changed = 0;

        for(int i = 0; i < n; i++) {
            Instruction in = fn->code[i];
            int depth = depths[i];
            if(depth < 0) continue;

            memcpy(tmp_stk, &entry_stk[i * stk_stride], sizeof(uint64_t) * stk_stride);
            memcpy(tmp_var, &entry_var[i * fn->sym_cnt], sizeof(uint64_t) * fn->sym_cnt);

            int sp = depth;
            uint64_t esc = 0;

            switch(in.op) {
                case OPC_LOAD_CONST:
                case OPC_GETFUNC:
                    tmp_stk[sp] = 0;
                    sp++;
                    break;

                case OPC_LOAD_VAR:
                    tmp_stk[sp] = (in.a >= 0 && in.a < fn->sym_cnt) ? tmp_var[in.a] : 0;
                    sp++;
                    break;

                case OPC_STORE_VAR: {
                    uint64_t m = (sp > 0) ? tmp_stk[sp - 1] : 0;
                    sp--;
                    if(in.a >= 0 && in.a < fn->sym_cnt) tmp_var[in.a] |= m;
                    tmp_stk[sp] = m;
                    sp++;
                    if(ea_is_global(fn, in.a)) esc |= m;
                    break;
                }

                /* 目标字面量：弹元素，标记逃逸，压自身 bit */
                case OPC_ARRAY_LIT:
                case OPC_MAP_LIT: {
                    int ne = (in.op == OPC_MAP_LIT) ? in.b * 2 : in.b;
                    for(int k = 0; k < ne && (sp - ne + k) >= 0; k++)
                        esc |= tmp_stk[sp - ne + k];
                    sp -= ne;
                    if(sp < 0) sp = 0;
                    if(in.op == target_op && lit_bit[i] >= 0) {
                        tmp_stk[sp] = (1ULL << lit_bit[i]);
                    } else {
                        tmp_stk[sp] = 0;  /* 非目标字面量：不追踪 */
                    }
                    sp++;
                    break;
                }

                case OPC_INDEX_GET:
                    sp -= 2;
                    if(sp < 0) sp = 0;
                    tmp_stk[sp] = 0;
                    sp++;
                    break;

                case OPC_INDEX_SET: {
                    uint64_t val_m = (sp > 0) ? tmp_stk[sp - 1] : 0;
                    uint64_t arr_m = (sp > 2) ? tmp_stk[sp - 3] : 0;
                    sp -= 3;
                    if(sp < 0) sp = 0;
                    esc |= arr_m;
                    esc |= val_m;
                    tmp_stk[sp] = 0;
                    sp++;
                    break;
                }

                case OPC_CALL: {
                    int argc = in.b;
                    for(int k = 0; k < argc && (sp - argc + k) >= 0; k++)
                        esc |= tmp_stk[sp - argc + k];
                    sp -= argc;
                    if(sp < 0) sp = 0;
                    tmp_stk[sp] = 0;
                    sp++;
                    break;
                }
                case OPC_CALLV: {
                    int argc = in.b;
                    for(int k = 0; k < argc && (sp - argc + k) >= 0; k++)
                        esc |= tmp_stk[sp - argc + k];
                    sp -= (argc + 1);
                    if(sp < 0) sp = 0;
                    tmp_stk[sp] = 0;
                    sp++;
                    break;
                }

                case OPC_RETURN: {
                    uint64_t m = (sp > 0) ? tmp_stk[sp - 1] : 0;
                    esc |= m;
                    sp--;
                    break;
                }
                case OPC_PEND_RETURN: {
                    uint64_t m = (sp > 0) ? tmp_stk[sp - 1] : 0;
                    esc |= m;
                    sp--;
                    break;
                }
                case OPC_THROW: {
                    uint64_t m = (sp > 0) ? tmp_stk[sp - 1] : 0;
                    esc |= m;
                    sp--;
                    break;
                }

                case OPC_BUILTIN: {
                    int argc = in.b;
                    int is_recv_returning = 0;
                    switch(in.a) {
                        case BUILTIN_ARRAY_CLEAR:
                        case BUILTIN_ARRAY_ADD:
                        case BUILTIN_INSERT:
                        case BUILTIN_DEL:
                        case BUILTIN_ARRAY_REMOVE:
                        case BUILTIN_ARRAY_SET:
                        case BUILTIN_ARRAY_ADDALL:
                            is_recv_returning = 1;
                            break;
                        default:
                            break;
                    }
                    uint64_t recv_m = (argc > 0 && sp >= argc) ? tmp_stk[sp - argc] : 0;

                    /* 扩容检测（仅数组 items 栈分配时需要） */
                    if(track_items) {
                        switch(in.a) {
                            case BUILTIN_ARRAY_ADD:
                                if(in.b >= 2) may_grow |= recv_m;
                                break;
                            case BUILTIN_INSERT:
                            case BUILTIN_ARRAY_ADDALL:
                                may_grow |= recv_m;
                                break;
                            default:
                                break;
                        }
                    }

                    if(in.a == BUILTIN_THREAD || in.a == BUILTIN_THREADLOCAL_SET) {
                        for(int k = 0; k < argc && (sp - argc + k) >= 0; k++)
                            esc |= tmp_stk[sp - argc + k];
                    }
                    else if(in.a == BUILTIN_ARRAY_ADD) {
                        if(in.b >= 2) {
                            if(sp > 0) esc |= tmp_stk[sp - 1];
                        }
                    }
                    else if(in.a == BUILTIN_INSERT || in.a == BUILTIN_ARRAY_SET) {
                        if(sp > 0) esc |= tmp_stk[sp - 1];
                    }
                    else if(in.a == BUILTIN_ARRAY_ADDALL) {
                        if(sp > 0) esc |= tmp_stk[sp - 1];
                    }

                    sp -= argc;
                    if(sp < 0) sp = 0;
                    tmp_stk[sp] = is_recv_returning ? recv_m : 0;
                    sp++;
                    break;
                }

                case OPC_ADD: case OPC_SUB: case OPC_MUL: case OPC_DIV: case OPC_MOD:
                case OPC_GT: case OPC_LT: case OPC_GE: case OPC_LE: case OPC_EQ: case OPC_NE:
                    sp -= 2;
                    if(sp < 0) sp = 0;
                    tmp_stk[sp] = 0;
                    sp++;
                    break;

                case OPC_NEG: case OPC_POS: case OPC_LOGIC_NOT: case OPC_TO_BOOL:
                case OPC_CAST_INT: case OPC_CAST_DOUBLE: case OPC_CAST_CHAR:
                case OPC_CAST_BOOL: case OPC_CAST_STRING: case OPC_CAST_ASCII:
                case OPC_CAST_BYTE: case OPC_CAST_INT8: case OPC_CAST_INT16:
                case OPC_CAST_INT32: case OPC_CAST_INT64: case OPC_CAST_UINT8:
                case OPC_CAST_UINT16: case OPC_CAST_UINT32: case OPC_CAST_UINT64:
                case OPC_CAST_LONG: case OPC_CAST_LONGLONG: case OPC_CAST_FLOAT:
                    sp -= 1;
                    if(sp < 0) sp = 0;
                    tmp_stk[sp] = 0;
                    sp++;
                    break;

                case OPC_PRE_INC: case OPC_POST_INC:
                case OPC_PRE_DEC: case OPC_POST_DEC:
                    tmp_stk[sp] = 0;
                    sp++;
                    break;

                case OPC_POP:
                    sp--;
                    if(sp < 0) sp = 0;
                    break;
                case OPC_DUP:
                    if(sp > 0) { tmp_stk[sp] = tmp_stk[sp - 1]; sp++; }
                    break;
                case OPC_PRINT:
                    break;

                case OPC_GET_ERR:
                    tmp_stk[sp] = 0;
                    sp++;
                    break;
                case OPC_TRY:
                case OPC_ENDTRY:
                case OPC_FIN_PUSH:
                    break;
                case OPC_FINISH:
                    for(int d = 0; d < sp; d++) esc |= tmp_stk[d];
                    break;

                case OPC_JMP_IF_FALSE:
                case OPC_JMP_IF_TRUE:
                case OPC_JMP_IF_NULL:
                    sp--;
                    if(sp < 0) sp = 0;
                    break;
                case OPC_JMP:
                    break;

                case OPC_RETURN_NIL:
                case OPC_HALT:
                case OPC_NOP:
                    break;

                default:
                    break;
            }

            escaped |= esc;

            /* 后继指令合并 */
            int succ[3];
            int succ_cnt = 0;
            if(in.op == OPC_JMP) {
                if(in.a >= 0 && in.a < n) succ[succ_cnt++] = in.a;
            } else if(in.op == OPC_JMP_IF_FALSE || in.op == OPC_JMP_IF_TRUE || in.op == OPC_JMP_IF_NULL) {
                if(in.a >= 0 && in.a < n) succ[succ_cnt++] = in.a;
                if(i + 1 < n) succ[succ_cnt++] = i + 1;
            } else if(in.op == OPC_TRY) {
                if(i + 1 < n) succ[succ_cnt++] = i + 1;
                if(in.a > 0 && in.a < n) succ[succ_cnt++] = in.a;
            } else if(in.op == OPC_ENDTRY) {
                if(i + 1 < n) succ[succ_cnt++] = i + 1;
            } else if(in.op == OPC_PEND_RETURN) {
                if(in.b > 0 && in.b < n) succ[succ_cnt++] = in.b;
            } else if(in.op == OPC_RETURN || in.op == OPC_RETURN_NIL ||
                      in.op == OPC_HALT || in.op == OPC_THROW ||
                      in.op == OPC_FINISH) {
            } else {
                if(i + 1 < n) succ[succ_cnt++] = i + 1;
            }

            for(int s = 0; s < succ_cnt; s++) {
                int si = succ[s];
                uint64_t* dst_stk = &entry_stk[si * stk_stride];
                uint64_t* dst_var = &entry_var[si * fn->sym_cnt];
                for(int d = 0; d < stk_stride; d++) {
                    uint64_t old = dst_stk[d];
                    dst_stk[d] |= tmp_stk[d];
                    if(dst_stk[d] != old) changed = 1;
                }
                for(int v = 0; v < fn->sym_cnt; v++) {
                    uint64_t old = dst_var[v];
                    dst_var[v] |= tmp_var[v];
                    if(dst_var[v] != old) changed = 1;
                }
            }
        }

        if(!changed) break;
    }

    /* 5. 循环检测 */
    uint8_t* in_loop = (uint8_t*)calloc(n, sizeof(uint8_t));
    for(int i = 0; i < n; i++) {
        Instruction in = fn->code[i];
        if((in.op == OPC_JMP || in.op == OPC_JMP_IF_FALSE || in.op == OPC_JMP_IF_TRUE || in.op == OPC_JMP_IF_NULL)
           && in.a >= 0 && in.a < i) {
            for(int k = in.a; k <= i; k++) in_loop[k] = 1;
        }
    }

    /* 6. 最终判定：非逃逸且非循环中的目标字面量 → 结构体栈分配 */
    for(int i = 0; i < n; i++) {
        if(fn->code[i].op == target_op && lit_bit[i] >= 0) {
            uint64_t bit = 1ULL << lit_bit[i];
            if(!(escaped & bit) && !in_loop[i]) {
                struct_result[i] = 1;
            }
        }
    }

    /* 7. items 栈分配判定（仅数组） */
    if(track_items && items_result) {
        for(int i = 0; i < n; i++) {
            if(struct_result[i] && lit_bit[i] >= 0) {
                uint64_t bit = 1ULL << lit_bit[i];
                int ne = fn->code[i].b;
                if(!(may_grow & bit) && ne > 0 && ne <= ITEMS_STACK_MAX) {
                    items_result[i] = 1;
                }
            }
        }
    }

    free(lit_bit);
    free(depths);
    free(entry_stk);
    free(entry_var);
    free(tmp_stk);
    free(tmp_var);
    free(in_loop);
    (void)other_op;
}

/* 标量替换分析：在逃逸分析之后运行。
 * 识别"不逃逸数组/map 字面量立即赋值给局部变量，且该变量仅用于常量下标读写或 len()"
 * 的模式，标记为可标量替换。代码生成时将其拆解为一组标量局部变量。
 *
 * 适用模式（保守）：
 *   a = [1,2,3];            // ARRAY_LIT + STORE_VAR + POP
 *   print(a[0]);            // LOAD_VAR a + LOAD_CONST 0 + INDEX_GET
 *   a[1] = 5;               // LOAD_VAR a + LOAD_CONST 1 + val + INDEX_SET
 *   print(len(a));          // LOAD_VAR a + BUILTIN_LEN
 * 不适用：动态下标、传参、return、迭代、多次赋值、非 POP 上下文等。
 */
void analyze_scalar_replacement(BytecodeFunc* fn)
{
    int n = fn->code_len;
    int sc = fn->sym_cnt;

    /* 释放上一次结果 */
    if(g_scalar_var) { free(g_scalar_var); g_scalar_var = NULL; }
    if(g_scalar_kind) { free(g_scalar_kind); g_scalar_kind = NULL; }
    if(g_scalar_count) { free(g_scalar_count); g_scalar_count = NULL; }
    if(g_scalar_keys) {
        for(int v = 0; v < g_scalar_sym_cnt; v++)
            if(g_scalar_keys[v]) free(g_scalar_keys[v]);
        free(g_scalar_keys);
        g_scalar_keys = NULL;
    }
    g_scalar_sym_cnt = 0;
    if(sc == 0) return;

    g_scalar_var = (uint8_t*)calloc(sc, sizeof(uint8_t));
    g_scalar_kind = (uint8_t*)calloc(sc, sizeof(uint8_t));
    g_scalar_count = (int*)calloc(sc, sizeof(int));
    g_scalar_keys = (int**)calloc(sc, sizeof(int*));
    g_scalar_sym_cnt = sc;

    for(int i = 0; i < n; i++) {
        int is_arr = (fn->code[i].op == OPC_ARRAY_LIT);
        int is_map = (fn->code[i].op == OPC_MAP_LIT);
        if(!is_arr && !is_map) continue;

        /* 必须紧跟 STORE_VAR + POP（语句级赋值） */
        if(i + 2 >= n) continue;
        if(fn->code[i+1].op != OPC_STORE_VAR) continue;
        if(fn->code[i+2].op != OPC_POP) continue;

        int v = fn->code[i+1].a;
        if(v < 0 || v >= sc) continue;
        /* 必须是局部变量（非全局） */
        if(ea_is_global(fn, v)) continue;

        int cnt = fn->code[i].b;
        if(cnt <= 0 || cnt > SCALAR_REPL_MAX) continue;

        /* map：所有键值必须是单条 LOAD_CONST（常量 map 字面量）。
         * 值为多指令表达式时，键的偏移不固定，无法安全提取键映射 → 不标量替换。 */
        int* key_consts = NULL;
        if(is_map) {
            key_consts = (int*)malloc(sizeof(int) * cnt);
            int ok = 1;
            for(int k = 0; k < cnt; k++) {
                int kidx = i - 2 * cnt + 2 * k;       /* 键指令位置 */
                int vidx = i - 2 * cnt + 2 * k + 1;   /* 值指令位置 */
                if(kidx < 0 || vidx < 0) { ok = 0; break; }
                if(fn->code[kidx].op != OPC_LOAD_CONST) { ok = 0; break; }
                if(fn->code[vidx].op != OPC_LOAD_CONST) { ok = 0; break; }
                int ci = fn->code[kidx].a;
                if(ci < 0 || ci >= fn->const_cnt) { ok = 0; break; }
                if(fn->consts[ci].type != VAL_STRING) { ok = 0; break; }
                key_consts[k] = ci;
            }
            if(!ok) { free(key_consts); continue; }
        }

        /* 扫描该变量的所有使用：必须全部是常量下标 INDEX_GET/INDEX_SET 或 BUILTIN_LEN */
        int eligible = 1;
        int assign_count = 0;
        for(int j = 0; j < n; j++) {
            Instruction in = fn->code[j];
            if(in.op == OPC_STORE_VAR && in.a == v) {
                assign_count++;
                if(assign_count > 1) { eligible = 0; break; }
                continue;
            }
            if(in.op != OPC_LOAD_VAR || in.a != v) continue;

            /* LOAD_VAR 之后必须是：
             *   LOAD_CONST + INDEX_GET（常量下标读）
             *   LOAD_CONST + ... + INDEX_SET（常量下标写，值可以是任意表达式）
             *   BUILTIN_LEN
             */
            if(j + 1 >= n) { eligible = 0; break; }
            Instruction next = fn->code[j+1];

            if(next.op == OPC_BUILTIN && next.a == BUILTIN_LEN) {
                continue;  /* len(a) → 常量替换 */
            }

            if(next.op != OPC_LOAD_CONST) { eligible = 0; break; }
            int ci = next.a;
            if(ci < 0 || ci >= fn->const_cnt) { eligible = 0; break; }

            if(is_arr) {
                /* 数组下标必须是整数常量且在边界内 */
                if(fn->consts[ci].type != VAL_INT) { eligible = 0; break; }
                long long idx = fn->consts[ci].v.i;
                if(idx < 0 || idx >= cnt) { eligible = 0; break; }
            } else {
                /* map 键必须是字符串常量且匹配字面量中的某个键 */
                if(fn->consts[ci].type != VAL_STRING) { eligible = 0; break; }
                int key_match = 0;
                const char* access_key = lumyr_str_cstr(&fn->consts[ci]);
                for(int k = 0; k < cnt; k++) {
                    int kci = key_consts[k];
                    if(kci >= 0 && kci < fn->const_cnt && fn->consts[kci].type == VAL_STRING) {
                        if(strcmp(lumyr_str_cstr(&fn->consts[kci]), access_key) == 0) {
                            key_match = 1; break;
                        }
                    }
                }
                if(!key_match) { eligible = 0; break; }
            }

            /* 检查 LOAD_CONST 之后是 INDEX_GET 还是 INDEX_SET */
            if(j + 2 >= n) { eligible = 0; break; }
            Instruction next2 = fn->code[j+2];
            if(next2.op == OPC_INDEX_GET) {
                continue;  /* a[idx] 读 */
            }
            /* INDEX_SET：LOAD_VAR + LOAD_CONST(idx) + 值 + INDEX_SET，值占 1 条指令 */
            if(next2.op != OPC_INDEX_SET) {
                /* 值可能是多条指令（如表达式），找到对应的 INDEX_SET */
                /* 简化：要求值是单条指令（LOAD_CONST/LOAD_VAR/GETFUNC 等） */
                if(j + 3 >= n || fn->code[j+3].op != OPC_INDEX_SET) {
                    eligible = 0; break;
                }
            }
            /* INDEX_SET 合法 */
        }

        if(eligible && assign_count == 1) {
            g_scalar_var[v] = 1;
            g_scalar_kind[v] = is_arr ? 0 : 1;
            g_scalar_count[v] = cnt;
            if(is_map) {
                g_scalar_keys[v] = key_consts;
            } else {
                free(key_consts);
            }
        } else {
            if(key_consts) free(key_consts);
        }
    }
}

// ---------------- 函数/主函数发射 ----------------

