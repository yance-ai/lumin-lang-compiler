import re

# 读取文件
with open('src/ir/ir_cgen_emit.c', 'r', encoding='utf-8') as f:
    content = f.read()

# 在 OPC_LOAD_VAR 后面添加 OPC_LOAD_VAR_REF
old_load_var = '''            case OPC_LOAD_VAR: {
                const char* _sname = emit_get_var_struct_name(fn, nm);
                int _vidx = bf_sym(fn, nm);
                /* struct 局部变量转换成 Value(map)，包含所有字段，用于无类型标注的参数传递；
                   OPC_LOAD_FIELD 高性能路径不受影响（直接用 lmvar_p->x，不经过栈） */
                if(_sname && _vidx >= fn->param_cnt) {
                    /* struct 局部变量转换成 Value(map)，VAL_STRUCT_PTR 通过 v.struct_ptr 访问 */
                    char _load_buf[256];
                    snprintf(_load_buf, sizeof(_load_buf), "((lumyr_struct_%s*)%s.v.struct_ptr)", _sname, cvar_rw(nm));
                    emit_struct_to_value(_sname, _load_buf);
                } else if(_sname) {
                    /* struct 参数（self）：直接传递 Value */
                    fprintf(out, "    __stk[__sp++] = %s;\\n", cvar_rw(nm));
                } else {
                    int _tag = emit_get_var_tag(fn, nm);
                    if(_tag >= 0 && emit_tag_to_ctype(_tag)) {
                        fprintf(out, "    __stk[__sp++] = %s;\\n", emit_precise_load(_tag, cvar_rw(nm)));
                    } else {
                        fprintf(out, "    __stk[__sp++] = %s;\\n", cvar_rw(nm));
                    }
                }
                break;
            }'''

new_load_var = '''            case OPC_LOAD_VAR: {
                const char* _sname = emit_get_var_struct_name(fn, nm);
                int _vidx = bf_sym(fn, nm);
                /* struct 局部变量转换成 Value(map)，包含所有字段，用于无类型标注的参数传递；
                   OPC_LOAD_FIELD 高性能路径不受影响（直接用 lmvar_p->x，不经过栈） */
                if(_sname && _vidx >= fn->param_cnt) {
                    /* struct 局部变量转换成 Value(map)，VAL_STRUCT_PTR 通过 v.struct_ptr 访问 */
                    char _load_buf[256];
                    snprintf(_load_buf, sizeof(_load_buf), "((lumyr_struct_%s*)%s.v.struct_ptr)", _sname, cvar_rw(nm));
                    emit_struct_to_value(_sname, _load_buf);
                } else if(_sname) {
                    /* struct 参数（self）：直接传递 Value */
                    fprintf(out, "    __stk[__sp++] = %s;\\n", cvar_rw(nm));
                } else {
                    int _tag = emit_get_var_tag(fn, nm);
                    if(_tag >= 0 && emit_tag_to_ctype(_tag)) {
                        fprintf(out, "    __stk[__sp++] = %s;\\n", emit_precise_load(_tag, cvar_rw(nm)));
                    } else {
                        fprintf(out, "    __stk[__sp++] = %s;\\n", cvar_rw(nm));
                    }
                }
                break;
            }
            case OPC_LOAD_VAR_REF: {
                /* ref 参数：直接传递 Value（struct 不转 Map，保持 VAL_STRUCT_PTR） */
                fprintf(out, "    __stk[__sp++] = %s;\\n", cvar_rw(nm));
                break;
            }'''

if old_load_var in content:
    content = content.replace(old_load_var, new_load_var)
    print("添加 OPC_LOAD_VAR_REF CC 模式实现成功")
else:
    print("未找到 OPC_LOAD_VAR CC 模式实现")

# 写入文件
with open('src/ir/ir_cgen_emit.c', 'w', encoding='utf-8') as f:
    f.write(content)
