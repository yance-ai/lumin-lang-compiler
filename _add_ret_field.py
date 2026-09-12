path = r'E:\doubaowork\lumin-lang-compiler\src\ast\ast_node_type.h'
with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

old = '''            AstNode* generic_params;  // 泛型参数列表（AST_PARAM链表，NULL=非泛型函数）
            int is_generator;  // generator function (gen func)
        } func_def;'''

new = '''            AstNode* generic_params;  // 泛型参数列表（AST_PARAM链表，NULL=非泛型函数）
            int is_generator;  // generator function (gen func)
            char* ret_type_name;  // 返回值类型名（如 "int","double","string"），NULL=无类型声明
        } func_def;'''

if old in content:
    content = content.replace(old, new, 1)
    print('OK: added ret_type_name field to func_def')
else:
    print('ERROR: old func_def not found')

with open(path, 'w', encoding='utf-8') as f:
    f.write(content)
