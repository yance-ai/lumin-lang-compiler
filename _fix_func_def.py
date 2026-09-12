path = r'E:\doubaowork\lumin-lang-compiler\src\parse\yacc.y'
with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

# 修改函数定义规则：第一个规则添加语义分析代码，第二个规则改成尖括号语法
old = '''func_def : FUNC ID LPAREN param_list RPAREN block_stmt {
          $$ = ast_func_def($2, $4, $6);
          $$->u.func_def.annotations = NULL;
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

if old in content:
    content = content.replace(old, new, 1)
    print('OK: func_def rules fixed with angle bracket syntax and semantic analysis')
else:
    print('ERROR: old func_def rules not found')

with open(path, 'w', encoding='utf-8') as f:
    f.write(content)
