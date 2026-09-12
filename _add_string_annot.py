path = r'E:\doubaowork\lumin-lang-compiler\src\lex\lex.l'
with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

old = '''"<int>"            { yylval.ll = CAST_INT; LEX_RET(TOK_TYPE_ANNOT); }'''

new = '''"<string>"         { yylval.ll = CAST_STRING; LEX_RET(TOK_TYPE_ANNOT); }
"<int>"            { yylval.ll = CAST_INT; LEX_RET(TOK_TYPE_ANNOT); }'''

if old in content:
    content = content.replace(old, new, 1)
    print('OK: added <string> type annotation to lex.l')
else:
    print('ERROR: old <int> rule not found')

with open(path, 'w', encoding='utf-8') as f:
    f.write(content)
