import os
import re

files = [
    r'E:\doubaowork\lumin-lang-compiler\tests\ffi_comprehensive_test.lm',
    r'E:\doubaowork\lumin-lang-compiler\tests\ffi_extended_types_test.lm',
    r'E:\doubaowork\lumin-lang-compiler\tests\type_full_test.lm',
]

for filepath in files:
    if not os.path.exists(filepath):
        print(f'SKIP: {filepath} not found')
        continue
    
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    
    original = content
    
    # 修复 <>buf -> <ptr>buf（类型名被误删）
    content = content.replace('<>buf', '<ptr>buf')
    content = re.sub(r'<>\s*(\w+)', r'<ptr>\1', content)
    
    # 把参数名 ptr 改成 p（只改参数名，不改类型名）
    # 在函数声明中，参数名是 <type>name 格式中的 name
    def fix_param_name(match):
        params_str = match.group(1)
        params = params_str.split(',')
        new_params = []
        for p in params:
            p = p.strip()
            # 匹配 <type>name 格式
            m2 = re.match(r'<(\w+(?:\s+\w+)?)>\s*(\w+)', p)
            if m2:
                ptype = m2.group(1)
                pname = m2.group(2)
                if pname == 'ptr':
                    pname = 'p'
                new_params.append(f'<{ptype}>{pname}')
            else:
                # 普通参数名
                if p == 'ptr':
                    p = 'p'
                new_params.append(p)
        return '(' + ', '.join(new_params) + ')'
    
    content = re.sub(r'\(([^)]*)\)', fix_param_name, content)
    
    if content != original:
        with open(filepath, 'w', encoding='utf-8', newline='\n') as f:
            f.write(content)
        print(f'OK: fixed {os.path.basename(filepath)}')
    else:
        print(f'NO CHANGE: {os.path.basename(filepath)}')

print('\nDone!')
