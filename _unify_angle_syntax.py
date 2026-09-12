path = r'E:\doubaowork\lumin-lang-compiler\src\parse\yacc.y'
with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

# 1. 修改 FFI 返回值类型：COLON ID / COLON builtin_type_name -> TOK_TYPE_ANNOT
old_ffi = '''        | TOK_EXTERN FUNC ID LPAREN param_list RPAREN COLON ID SEMI {
          $$ = ast_extern_func($3, $5, $8, NULL);
        }
        | TOK_EXTERN FUNC ID LPAREN param_list RPAREN COLON builtin_type_name SEMI {
          $$ = ast_extern_func($3, $5, castkind_to_name($8), NULL);
        }
        /* FFI 外部函数声明（指定库）：extern "libname" func name(params): ret_type */
        | TOK_EXTERN STRING_LIT FUNC ID LPAREN param_list RPAREN COLON ID SEMI {
          $$ = ast_extern_func($4, $6, $9, $2);
        }
        | TOK_EXTERN STRING_LIT FUNC ID LPAREN param_list RPAREN COLON builtin_type_name SEMI {
          $$ = ast_extern_func($4, $6, castkind_to_name($9), $2);
        } ;'''

new_ffi = '''        | TOK_EXTERN FUNC ID LPAREN param_list RPAREN TOK_TYPE_ANNOT SEMI {
          $$ = ast_extern_func($3, $5, castkind_to_name($7), NULL);
        }
        /* FFI 外部函数声明（指定库）：extern "libname" func name(params) <ret_type> */
        | TOK_EXTERN STRING_LIT FUNC ID LPAREN param_list RPAREN TOK_TYPE_ANNOT SEMI {
          $$ = ast_extern_func($4, $6, castkind_to_name($8), $2);
        } ;'''

if old_ffi in content:
    content = content.replace(old_ffi, new_ffi, 1)
    print('OK: FFI return type changed to <int> syntax')
else:
    print('ERROR: old FFI rules not found')

# 2. 修改 param 规则：移除 ID COLON type_name_str 冒号语法
old_param = '''param
    : ID                     { $$ = ast_param($1, 0, NULL); } /*普通参数 is_ellipsis=0 */
    | ID ASSIGN expr         { $$ = ast_param($1, 0, $3); } /*带默认值的参数 */
    | ELLIPSIS ID            { $$ = ast_param($2, 1, NULL); } /* ...args 可变参数 is_ellipsis=1 */
    | TOK_TYPE_ANNOT ID      { $$ = ast_param($2, 0, NULL); $$->u.param.constraint = strdup(castkind_to_name($1)); } /*带类型标注的参数 <int>a */
    | ID COLON type_name_str { $$ = ast_param($1, 0, NULL); $$->u.param.constraint = $3; } /*带类型标注的参数 a: int（FFI 兼容）*/
;'''

new_param = '''param
    : ID                     { $$ = ast_param($1, 0, NULL); } /*普通参数 is_ellipsis=0 */
    | ID ASSIGN expr         { $$ = ast_param($1, 0, $3); } /*带默认值的参数 */
    | ELLIPSIS ID            { $$ = ast_param($2, 1, NULL); } /* ...args 可变参数 is_ellipsis=1 */
    | TOK_TYPE_ANNOT ID      { $$ = ast_param($2, 0, NULL); $$->u.param.constraint = strdup(castkind_to_name($1)); } /*带类型标注的参数 <int>a */
;'''

if old_param in content:
    content = content.replace(old_param, new_param, 1)
    print('OK: param rule removed colon syntax, only <int>a remains')
else:
    print('ERROR: old param rule not found')

with open(path, 'w', encoding='utf-8') as f:
    f.write(content)
