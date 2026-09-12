path = r'E:\doubaowork\lumin-lang-compiler\src\parse\yacc.y'
with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

# 修复 type_name_str：只保留 ID，移除 builtin_type_name，避免与 type_name 归约冲突
old = '''/* 类型名字符串（用于 FFI 参数类型标注，直接返回原始字符串，避免 ValueType 枚举冲突） */
type_name_str
    : ID                         { $$ = $1; }
    | builtin_type_name          { $$ = castkind_to_name($1); }
;'''

new = '''/* 类型名字符串（用于自定义类型名；内置类型由 builtin_type_name 直接处理，避免与 type_name 归约冲突） */
type_name_str
    : ID                         { $$ = $1; }
;'''

if old in content:
    content = content.replace(old, new, 1)
    print('OK: type_name_str now only supports ID')
else:
    print('ERROR: old type_name_str not found')

with open(path, 'w', encoding='utf-8') as f:
    f.write(content)
