open('t2.lm','w',encoding='utf-8',newline='\n').write('''func add(<int>a, <int>b) {
    return a + b;
}
print(add(3, 4));
''')
