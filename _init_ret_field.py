path = r'E:\doubaowork\lumin-lang-compiler\src\ast\ast_node.c'
with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

old = '''    n->u.func_def.generic_params = NULL;
    n->u.func_def.is_generator = 0;
    return n;
}'''

new = '''    n->u.func_def.generic_params = NULL;
    n->u.func_def.is_generator = 0;
    n->u.func_def.ret_type_name = NULL;
    return n;
}'''

if old in content:
    content = content.replace(old, new, 1)
    print('OK: ast_func_def initialized ret_type_name')
else:
    print('ERROR: old ast_func_def not found')

with open(path, 'w', encoding='utf-8') as f:
    f.write(content)
