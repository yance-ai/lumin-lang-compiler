path = r'E:\doubaowork\lumin-lang-compiler\src\parse\yacc.y'
with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

# 修改函数定义规则：返回值类型放在函数名后面 func <int> add(a, b)
old = '''func_def : FUNC ID LPAREN param_list RPAREN block_stmt {
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
        | FUNC ID LPAREN param_list RPAREN TOK_TYPE_ANNOT block_stmt {
          $$ = ast_func_def($2, $4, $7);
          $$->u.func_def.annotations = NULL;
          $$->u.func_def.ret_type_name = strdup(castkind_to_name($6));
          /* 语义分析阶段：编译这个函数定义，生成RuntimeFunc，注册到全局符号 */
          RuntimeFunc* rf = compile_func_from_ast($$);
          Value func_val = {0};
          func_val.type = VAL_FUNC;
          func_val.v.func.func_obj = rf;
          func_val.v.func.ffi_func = NULL;
          func_val.v.func.is_ffi = 0;
          sym_set($2, func_val); /* 注册到运行时符号表，后续调用可以查到 */
        }'''

new = '''func_def : FUNC ID LPAREN param_list RPAREN block_stmt {
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

if old in content:
    content = content.replace(old, new, 1)
    print('OK: return type moved to before function name')
else:
    print('ERROR: old func_def rules not found')

with open(path, 'w', encoding='utf-8') as f:
    f.write(content)
