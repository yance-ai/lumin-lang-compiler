import os

files = [
    r'E:\doubaowork\lumin-lang-compiler\tests\ffi_test.lm',
    r'E:\doubaowork\lumin-lang-compiler\tests\ffi_comprehensive_test.lm',
    r'E:\doubaowork\lumin-lang-compiler\tests\ffi_extended_types_test.lm',
    r'E:\doubaowork\lumin-lang-compiler\tests\ffi_float_test.lm',
    r'E:\doubaowork\lumin-lang-compiler\tests\type_full_test.lm',
]

for filepath in files:
    if not os.path.exists(filepath):
        print(f'SKIP: {filepath} not found')
        continue
    
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    
    original = content
    
    # 把参数名 ptr 改成 p（只在函数声明/定义中替换）
    # 替换 <ptr>ptr -> <ptr>p
    content = content.replace('<ptr>ptr', '<ptr>p')
    content = content.replace('<pointer>ptr', '<pointer>p')
    content = content.replace('<handle>ptr', '<handle>p')
    
    # 替换普通参数 ptr -> p（在括号内）
    import re
    content = re.sub(r'\(([^)]*)\bptr\b([^)]*)\)', lambda m: '(' + m.group(1).replace('ptr', 'p') + m.group(2) + ')', content)
    
    if content != original:
        with open(filepath, 'w', encoding='utf-8', newline='\n') as f:
            f.write(content)
        print(f'OK: updated {os.path.basename(filepath)}')
    else:
        print(f'NO CHANGE: {os.path.basename(filepath)}')

print('\nDone!')
