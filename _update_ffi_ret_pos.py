import re
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
    
    # 替换：extern "lib" func name(params) <type>; -> extern "lib" <type> func name(params);
    content = re.sub(
        r'extern\s+("[^"]+")\s+func\s+(\w+)\s*\(([^)]*)\)\s*<(\w+(?:\s+\w+)?)>\s*;',
        r'extern \1 <\4> func \2(\3);',
        content
    )
    
    # 替换：extern func name(params) <type>; -> extern <type> func name(params);
    content = re.sub(
        r'extern\s+func\s+(\w+)\s*\(([^)]*)\)\s*<(\w+(?:\s+\w+)?)>\s*;',
        r'extern <\3> func \1(\2);',
        content
    )
    
    if content != original:
        with open(filepath, 'w', encoding='utf-8', newline='\n') as f:
            f.write(content)
        print(f'OK: updated {os.path.basename(filepath)}')
    else:
        print(f'NO CHANGE: {os.path.basename(filepath)}')

print('\nDone!')
