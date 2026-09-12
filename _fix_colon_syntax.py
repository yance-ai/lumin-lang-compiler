path = r'E:\doubaowork\lumin-lang-compiler\src\parse\yacc.y'
with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

# 1. 修改 type_name_str：只保留 ID，移除 builtin_type_name，避免归约冲突
old_tns = '''/* 类型名字符串（用于 FFI 参数类型标注，直接返回原始字符串，避免 ValueType 枚举冲突） */
type_name_str
    : ID                         { $$ = $1; }
    | builtin_type_name          { $$ = castkind_to_name($1); }
;'''

new_tns = '''/* 类型名字符串（仅用于自定义类型名；内置类型由 builtin_type_name 直接处理，避免与 type_name 归约冲突） */
type_name_str
    : ID                         { $$ = $1; }
;'''

if old_tns in content:
    content = content.replace(old_tns, new_tns, 1)
    print('OK: type_name_str now only supports ID')
else:
    print('ERROR: old type_name_str not found')

# 2. 修改函数定义规则：把返回值类型从函数名前面改回参数列表后面的冒号语法
old_func = '''func_def : FUNC ID LPAREN param_list RPAREN block_stmt {
          $$ = ast_func_def($2, $4, $6);
          $$->u.func_def.annotations = NULL;
          $$->u.func_def.ret_type_name = NULL;
          /* 语义分析阶段：编译这个函数定义，生成RuntimeFunc，注册到全局符号 */
          RuntimeFunc* rf = compile_func_from_ast($$);
          Value func_val = {0};
          func_val.type = VAL_FUNC;
          func_val.v.func.func_obj = rf;
          func_val.v.func.ffi_func = NULL;
          func_val.v.func.is_ffi = 0;
          sym_set($2, func_val); /* 注册到运行时符号表，后续调用可以查到 */
        }
        | FUNC TOK_TYPE_ANNOT ID LPAREN param_list RPAREN block_stmt {
          $$ = ast_func_def($3, $5, $7);
          $$->u.func_def.annotations = NULL;
          $$->u.func_def.ret_type_name = strdup(castkind_to_name($2));
          /* 语义分析阶段：编译这个函数定义，生成RuntimeFunc，注册到全局符号 */
          RuntimeFunc* rf = compile_func_from_ast($$);
          Value func_val = {0};
          func_val.type = VAL_FUNC;
          func_val.v.func.func_obj = rf;
          func_val.v.func.ffi_func = NULL;
          func_val.v.func.is_ffi = 0;
          sym_set($3, func_val); /* 注册到运行时符号表，后续调用可以查到 */
        }'''

new_func = '''func_def : FUNC ID LPAREN param_list RPAREN block_stmt {
          $$ = ast_func_def($2, $4, $6);
          $$->u.func_def.annotations = NULL;
          $$->u.func_def.ret_type_name = NULL;
          /* 语义分析阶段：编译这个函数定义，生成RuntimeFunc，注册到全局符号 */
          RuntimeFunc* rf = compile_func_from_ast($$);
          Value func_val = {0};
          func_val.type = VAL_FUNC;
          func_val.v.func.func_obj = rf;
          func_val.v.func.ffi_func = NULL;
          func_val.v.func.is_ffi = 0;
          sym_set($2, func_val); /* 注册到运行时符号表，后续调用可以查到 */
        }
        | FUNC ID LPAREN param_list RPAREN COLON type_name block_stmt {
          $$ = ast_func_def($2, $4, $8);
          $$->u.func_def.annotations = NULL;
          $$->u.func_def.ret_type_name = strdup(valtype_to_name($7));
          /* 语义分析阶段：编译这个函数定义，生成RuntimeFunc，注册到全局符号 */
          RuntimeFunc* rf = compile_func_from_ast($$);
          Value func_val = {0};
          func_val.type = VAL_FUNC;
          func_val.v.func.func_obj = rf;
          func_val.v.func.ffi_func = NULL;
          func_val.v.func.is_ffi = 0;
          sym_set($2, func_val); /* 注册到运行时符号表，后续调用可以查到 */
        }'''

if old_func in content:
    content = content.replace(old_func, new_func, 1)
    print('OK: func_def changed back to colon syntax')
else:
    print('ERROR: old func_def not found')

with open(path, 'w', encoding='utf-8') as f:
    f.write(content)
