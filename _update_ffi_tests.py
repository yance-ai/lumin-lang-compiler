import re
import os

files = [
    r'E:\doubaowork\lumin-lang-compiler\tests\ffi_test.lm',
    r'E:\doubaowork\lumin-lang-compiler\tests\ffi_comprehensive_test.lm',
    r'E:\doubaowork\lumin-lang-compiler\tests\ffi_extended_types_test.lm',
    r'E:\doubaowork\lumin-lang-compiler\tests\ffi_float_test.lm',
]

for filepath in files:
    if not os.path.exists(filepath):
        print(f'SKIP: {filepath} not found')
        continue
    
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    
    original = content
    
    # 替换返回值类型：): type; -> ) <type>;
    # 支持多词类型如 long long, unsigned long
    content = re.sub(r'\):\s*(\w+(?:\s+\w+)?)\s*;', r') <\1>;', content)
    
    # 替换参数类型：name: type -> <type>name
    # 只在括号内替换，避免误匹配
    def replace_params(match):
        params_str = match.group(1)
        # 分割参数
        params = params_str.split(',')
        new_params = []
        for p in params:
            p = p.strip()
            # 匹配 name: type
            m = re.match(r'(\w+)\s*:\s*(\w+(?:\s+\w+)?)', p)
            if m:
                name = m.group(1)
                ptype = m.group(2)
                new_params.append(f'<{ptype}>{name}')
            else:
                new_params.append(p)
        return '(' + ', '.join(new_params) + ')'
    
    content = re.sub(r'\(([^)]*)\)', replace_params, content)
    
    if content != original:
        with open(filepath, 'w', encoding='utf-8', newline='\n') as f:
            f.write(content)
        print(f'OK: updated {os.path.basename(filepath)}')
    else:
        print(f'NO CHANGE: {os.path.basename(filepath)}')

print('\nDone!')
