import re

# 读取文件
with open('src/ir/ir_cgen_emit.c', 'r', encoding='utf-8') as f:
    content = f.read()

# 查找 OPC_LOAD_FIELD 中生成 _struct_access 的代码
old_load_access = '''                    /* 区分函数参数（Value 类型，指针存在 v.i 中）和 struct 局部变量（指针类型） */
                    const char* _struct_access = NULL;
                    char _struct_access_buf[256];
                    if(in.a < fn->param_cnt) {
                        /* 函数参数：Value 类型，指针存在 v.i 中 */
                        snprintf(_struct_access_buf, sizeof(_struct_access_buf), "((lumyr_struct_%s*)%s.v.struct_ptr)", sname, cvar_rw(vname));
                        _struct_access = _struct_access_buf;
                    } else {
                        /* struct 局部变量：VAL_STRUCT_PTR，通过 v.struct_ptr 访问 */
                        snprintf(_struct_access_buf, sizeof(_struct_access_buf), "((lumyr_struct_%s*)%s.v.struct_ptr)", sname, cvar_rw(vname));
                        _struct_access = _struct_access_buf;
                    }'''

new_load_access = '''                    /* 区分函数参数（Value 类型，指针存在 v.i 中）和 struct 局部变量（指针类型） */
                    const char* _struct_access = NULL;
                    char _struct_access_buf[256];
                    /* ref 参数：cvar_rw 返回 *lmloc_xxx，需要加括号避免优先级问题 */
                    int _is_ref_param = (in.a < fn->param_cnt && fn->param_is_ref && fn->param_is_ref[in.a]);
                    const char* _var_access = cvar_rw(vname);
                    if(_is_ref_param) {
                        snprintf(_struct_access_buf, sizeof(_struct_access_buf), "((lumyr_struct_%s*)(%s).v.struct_ptr)", sname, _var_access);
                    } else {
                        snprintf(_struct_access_buf, sizeof(_struct_access_buf), "((lumyr_struct_%s*)%s.v.struct_ptr)", sname, _var_access);
                    }
                    _struct_access = _struct_access_buf;'''

if old_load_access in content:
    content = content.replace(old_load_access, new_load_access)
    print("修改 OPC_LOAD_FIELD _struct_access 成功")
else:
    print("未找到 OPC_LOAD_FIELD _struct_access 代码")

# 查找 OPC_STORE_FIELD 中生成 _store_access 的代码
old_store_access = '''                    /* 区分函数参数（Value 类型，指针存在 v.i 中）和 struct 局部变量（指针类型） */
                    const char* _store_access = NULL;
                    char _store_access_buf[256];
                    if(in.a < fn->param_cnt) {
                        snprintf(_store_access_buf, sizeof(_store_access_buf), "((lumyr_struct_%s*)%s.v.struct_ptr)", sname, cvar_rw(vname));
                        _store_access = _store_access_buf;
                    } else {
                        /* struct 局部变量：VAL_STRUCT_PTR，通过 v.struct_ptr 访问 */
                        snprintf(_store_access_buf, sizeof(_store_access_buf), "((lumyr_struct_%s*)%s.v.struct_ptr)", sname, cvar_rw(vname));
                        _store_access = _store_access_buf;
                    }'''

new_store_access = '''                    /* 区分函数参数（Value 类型，指针存在 v.i 中）和 struct 局部变量（指针类型） */
                    const char* _store_access = NULL;
                    char _store_access_buf[256];
                    /* ref 参数：cvar_rw 返回 *lmloc_xxx，需要加括号避免优先级问题 */
                    int _is_ref_param = (in.a < fn->param_cnt && fn->param_is_ref && fn->param_is_ref[in.a]);
                    const char* _var_access = cvar_rw(vname);
                    if(_is_ref_param) {
                        snprintf(_store_access_buf, sizeof(_store_access_buf), "((lumyr_struct_%s*)(%s).v.struct_ptr)", sname, _var_access);
                    } else {
                        snprintf(_store_access_buf, sizeof(_store_access_buf), "((lumyr_struct_%s*)%s.v.struct_ptr)", sname, _var_access);
                    }
                    _store_access = _store_access_buf;'''

if old_store_access in content:
    content = content.replace(old_store_access, new_store_access)
    print("修改 OPC_STORE_FIELD _store_access 成功")
else:
    print("未找到 OPC_STORE_FIELD _store_access 代码")

# 写入文件
with open('src/ir/ir_cgen_emit.c', 'w', encoding='utf-8') as f:
    f.write(content)
