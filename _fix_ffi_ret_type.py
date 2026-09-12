path = r'E:\doubaowork\lumin-lang-compiler\src\parse\yacc.y'
with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

old = '''        /* FFI 外部函数声明：extern func name(params): ret_type */
        | TOK_EXTERN FUNC ID LPAREN param_list RPAREN TOK_TYPE_ANNOT SEMI {
          $$ = ast_extern_func($3, $5, castkind_to_name($7), NULL);
        }
        /* FFI 外部函数声明（指定库）：extern "libname" func name(params) <ret_type> */
        | TOK_EXTERN STRING_LIT FUNC ID LPAREN param_list RPAREN TOK_TYPE_ANNOT SEMI {
          $$ = ast_extern_func($4, $6, castkind_to_name($8), $2);
        } ;'''

new = '''        /* FFI 外部函数声明：extern <ret_type> func name(params) */
        | TOK_EXTERN TOK_TYPE_ANNOT FUNC ID LPAREN param_list RPAREN SEMI {
          $$ = ast_extern_func($4, $6, castkind_to_name($2), NULL);
        }
        /* FFI 外部函数声明（指定库）：extern "libname" <ret_type> func name(params) */
        | TOK_EXTERN STRING_LIT TOK_TYPE_ANNOT FUNC ID LPAREN param_list RPAREN SEMI {
          $$ = ast_extern_func($5, $7, castkind_to_name($3), $2);
        } ;'''

if old in content:
    content = content.replace(old, new, 1)
    print('OK: FFI return type moved before func keyword')
else:
    print('ERROR: old FFI rules not found')

with open(path, 'w', encoding='utf-8') as f:
    f.write(content)
