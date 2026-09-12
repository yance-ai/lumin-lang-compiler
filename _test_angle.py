open('t.lm','w',encoding='utf-8',newline='\n').write('''func add(<int>a, <int>b) <int> {
    return a + b;
}
func double_val(<double>x) <double> {
    return x * 2.0;
}
print(add(3, 4));
print(double_val(3.14));
''')
