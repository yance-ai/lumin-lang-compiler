path = r'E:\doubaowork\lumin-lang-compiler\tests\type_full_test.lm'
with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

# 批量替换 FFI 声明中的冒号语法为尖括号语法
replacements = [
    # 参数类型：name: type -> <type>name
    ('s: string', '<string>s'),
    ('end: ptr', '<ptr>end'),
    ('base: int', '<int>base'),
    ('x: double', '<double>x'),
    ('c: int', '<int>c'),
    ('size: size_t', '<size_t>size'),
    ('p: ptr', '<ptr>p'),
    ('value: int', '<int>value'),
    # 返回值类型：: type; -> <type>;
    ('): int;', ') <int>;'),
    ('): long;', ') <long>;'),
    ('): long long;', ') <long long>;'),
    ('): size_t;', ') <size_t>;'),
    ('): unsigned long;', ') <unsigned long>;'),
    ('): uint64;', ') <uint64>;'),
    ('): double;', ') <double>;'),
    ('): ptr;', ') <ptr>;'),
    ('): void;', ') <void>;'),
]

for old, new in replacements:
    if old in content:
        content = content.replace(old, new)
        print(f'OK: {old} -> {new}')
    else:
        print(f'SKIP: {old} not found')

with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(content)

print('\nDone!')
