open('t3.lm','w',encoding='utf-8',newline='\n').write('''func add(a, b) <int> {
    return a + b;
}
print(add(3, 4));
''')
