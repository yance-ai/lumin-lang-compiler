with open('src/ir/vm.c', 'r', encoding='utf-8') as f:
    content = f.read()

# 1. 在 GeneratorObject 中添加 send_value 字段
old_gen = '''    Value yield_value;       /* yield 的值 */
    EvalCtx* ctx;            /* 求值上下文 */'''
new_gen = '''    Value yield_value;       /* yield 的值 */
    Value send_value;        /* send() 发送的值（作为 yield 表达式的返回值） */
    int has_send_value;      /* 是否有 send_value（第一次 next() 没有） */
    EvalCtx* ctx;            /* 求值上下文 */'''
content = content.replace(old_gen, new_gen)

# 2. 修改 generator_resume，接受 send_value 参数
old_resume_sig = '''static int generator_resume(GeneratorObject* gen, Value* result)
{
    if(gen->finished) { *result = val_none(); return 0; }

    /* setjmp 恢复点：vm_run 中遇到 OPC_YIELD 时 longjmp 到这里 */
    if(setjmp(gen->resume_point) == 0) {
        /* 第一次进入或从 next 恢复：调用 vm_run 执行字节码 */
        s_current_gen = gen;
        EvalCtx ctx;
        memset(&ctx, 0, sizeof(ctx));
        gen->ctx = &ctx;
        Value ret = vm_run(gen->bf, gen->frame, &ctx);
        /* vm_run 正常返回：生成器结束 */
        s_current_gen = NULL;
        gen->finished = 1;
        *result = ret;
        return 0;
    } else {
        /* 从 yield longjmp 回来：返回 yield 的值 */
        s_current_gen = NULL;
        *result = s_gen_yield_result;
        return 1;
    }
}'''
new_resume_sig = '''static int generator_resume(GeneratorObject* gen, Value* result, Value* send_val)
{
    if(gen->finished) { *result = val_none(); return 0; }

    /* 设置 send_value（如果有） */
    if(send_val) {
        gen->send_value = *send_val;
        gen->has_send_value = 1;
    } else {
        gen->has_send_value = 0;
    }

    /* setjmp 恢复点：vm_run 中遇到 OPC_YIELD 时 longjmp 到这里 */
    if(setjmp(gen->resume_point) == 0) {
        /* 第一次进入或从 next 恢复：调用 vm_run 执行字节码 */
        s_current_gen = gen;
        EvalCtx ctx;
        memset(&ctx, 0, sizeof(ctx));
        gen->ctx = &ctx;
        Value ret = vm_run(gen->bf, gen->frame, &ctx);
        /* vm_run 正常返回：生成器结束 */
        s_current_gen = NULL;
        gen->finished = 1;
        *result = ret;
        return 0;
    } else {
        /* 从 yield longjmp 回来：返回 yield 的值 */
        s_current_gen = NULL;
        *result = s_gen_yield_result;
        return 1;
    }
}'''
content = content.replace(old_resume_sig, new_resume_sig)

# 3. 修改 OPC_YIELD 处理，把 send_value 作为 yield 表达式的返回值
old_yield = '''            case OPC_YIELD: {
                /* 生成器 yield：保存状态，longjmp 返回到 generator_resume */
                if(!is_generator) {
                    fprintf(stderr, "Runtime Error: yield 只能在生成器函数中使用\\n");
                    exit(EXIT_FAILURE);
                }
                Value v = stack[--sp];
                /* 保存生成器状态 */
                gen_ctx->pc = pc;
                gen_ctx->sp = sp;
                gen_ctx->started = 1;
                /* 恢复外层 VM 状态 */
                vm_depth = saved_depth;
                g_err_jmp = saved_gj;
                vm_fin_n = saved_fin;
                gc_set_roots(old_gc_stack, old_gc_sp, old_gc_frame);
                tls_vm_run_depth--;
                if (!tls_skip_vm_unregister) gc_unregister_thread();
                /* 设置 yield 结果并 longjmp 返回到 generator_resume */
                s_gen_yield_result = v;
                longjmp(gen_ctx->resume_point, 1);
            }'''
new_yield = '''            case OPC_YIELD: {
                /* 生成器 yield：保存状态，longjmp 返回到 generator_resume
                 * 恢复时，send_value 会被压入栈顶作为 yield 表达式的返回值 */
                if(!is_generator) {
                    fprintf(stderr, "Runtime Error: yield 只能在生成器函数中使用\\n");
                    exit(EXIT_FAILURE);
                }
                Value v = stack[--sp];
                /* 保存生成器状态 */
                gen_ctx->pc = pc;
                gen_ctx->sp = sp;
                gen_ctx->started = 1;
                /* 恢复外层 VM 状态 */
                vm_depth = saved_depth;
                g_err_jmp = saved_gj;
                vm_fin_n = saved_fin;
                gc_set_roots(old_gc_stack, old_gc_sp, old_gc_frame);
                tls_vm_run_depth--;
                if (!tls_skip_vm_unregister) gc_unregister_thread();
                /* 设置 yield 结果并 longjmp 返回到 generator_resume */
                s_gen_yield_result = v;
                longjmp(gen_ctx->resume_point, 1);
            }'''
content = content.replace(old_yield, new_yield)

# 4. 在 vm_run 生成器恢复处，把 send_value 压入栈顶
old_resume_point = '''    if(is_generator) {
        /* 生成器模式：复用生成器的 stack，从保存的 pc/sp 恢复 */
        stack = gen_ctx->stack;
        sp = gen_ctx->sp;
        pc = gen_ctx->pc;
    } else {'''
new_resume_point = '''    if(is_generator) {
        /* 生成器模式：复用生成器的 stack，从保存的 pc/sp 恢复 */
        stack = gen_ctx->stack;
        sp = gen_ctx->sp;
        pc = gen_ctx->pc;
        /* 如果是从 yield 恢复（不是第一次启动），把 send_value 压入栈顶作为 yield 表达式的返回值 */
        if(gen_ctx->started && gen_ctx->has_send_value) {
            stack[sp++] = gen_ctx->send_value;
        }
    } else {'''
content = content.replace(old_resume_point, new_resume_point)

# 5. 修改 BUILTIN_NEXT 处理，调用 generator_resume 时不传 send_value
old_next = '''                    case BUILTIN_NEXT: {
                        /* next(gen)：恢复生成器执行，返回 yield 值；结束返回 null */
                        Value gen_val = stack[--sp];
                        if(gen_val.type != VAL_GENERATOR) {
                            fprintf(stderr, "Runtime Error: next() 需要生成器对象，实际类型: %d\\n", gen_val.type);
                            exit(EXIT_FAILURE);
                        }
                        GeneratorObject* gen = (GeneratorObject*)gen_val.v.generator;
                        Value result;
                        int yielded = generator_resume(gen, &result);
                        stack[sp++] = result;
                        (void)yielded;
                        break;
                    }'''
new_next = '''                    case BUILTIN_NEXT: {
                        /* next(gen)：恢复生成器执行，返回 yield 值；结束返回 null */
                        Value gen_val = stack[--sp];
                        if(gen_val.type != VAL_GENERATOR) {
                            fprintf(stderr, "Runtime Error: next() 需要生成器对象，实际类型: %d\\n", gen_val.type);
                            exit(EXIT_FAILURE);
                        }
                        GeneratorObject* gen = (GeneratorObject*)gen_val.v.generator;
                        Value result;
                        int yielded = generator_resume(gen, &result, NULL);
                        stack[sp++] = result;
                        (void)yielded;
                        break;
                    }
                    case BUILTIN_SEND: {
                        /* send(gen, val)：向生成器发送值，恢复执行，返回下一个 yield 值 */
                        Value send_val = stack[--sp];
                        Value gen_val = stack[--sp];
                        if(gen_val.type != VAL_GENERATOR) {
                            fprintf(stderr, "Runtime Error: send() 需要生成器对象，实际类型: %d\\n", gen_val.type);
                            exit(EXIT_FAILURE);
                        }
                        GeneratorObject* gen = (GeneratorObject*)gen_val.v.generator;
                        if(!gen->started) {
                            fprintf(stderr, "Runtime Error: send() 不能用于刚创建的生成器，请先调用 next()\\n");
                            exit(EXIT_FAILURE);
                        }
                        Value result;
                        int yielded = generator_resume(gen, &result, &send_val);
                        stack[sp++] = result;
                        (void)yielded;
                        break;
                    }'''
content = content.replace(old_next, new_next)

with open('src/ir/vm.c', 'w', encoding='utf-8') as f:
    f.write(content)

print('vm.c: implemented send() method for generators')
