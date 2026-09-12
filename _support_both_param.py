path = r'E:\doubaowork\lumin-lang-compiler\src\parse\yacc.y'
with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

old_param = '''param
    : ID                     { $$ = ast_param($1, 0, NULL); } /*普通参数 is_ellipsis=0 */
    | ID ASSIGN expr         { $$ = ast_param($1, 0, $3); } /*带默认值的参数 */
    | ELLIPSIS ID            { $$ = ast_param($2, 1, NULL); } /* ...args 可变参数 is_ellipsis=1 */
    | TOK_TYPE_ANNOT ID      { $$ = ast_param($2, 0, NULL); $$->u.param.constraint = strdup(castkind_to_name($1)); } /*带类型标注的参数 <int>a */
;'''

new_param = '''param
    : ID                     { $$ = ast_param($1, 0, NULL); } /*普通参数 is_ellipsis=0 */
    | ID ASSIGN expr         { $$ = ast_param($1, 0, $3); } /*带默认值的参数 */
    | ELLIPSIS ID            { $$ = ast_param($2, 1, NULL); } /* ...args 可变参数 is_ellipsis=1 */
    | TOK_TYPE_ANNOT ID      { $$ = ast_param($2, 0, NULL); $$->u.param.constraint = strdup(castkind_to_name($1)); } /*带类型标注的参数 <int>a */
    | ID COLON type_name_str { $$ = ast_param($1, 0, NULL); $$->u.param.constraint = $3; } /*带类型标注的参数 a: int（FFI 兼容）*/
;'''

if old_param in content:
    content = content.replace(old_param, new_param, 1)
    print('OK: param rule supports both <int>a and a: int')
else:
    print('ERROR: old param rule not found')

with open(path, 'w', encoding='utf-8') as f:
    f.write(content)
