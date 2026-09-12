open('t3.lm','w',encoding='utf-8',newline='\n').write('''func <int> add(<int>a, <int>b) {
    return a + b;
}
func <double> double_val(<double>x) {
    return x * 2.0;
}
print(add(3, 4));
print(double_val(3.14));
''')
