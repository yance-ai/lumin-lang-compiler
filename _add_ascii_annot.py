path = r'E:\doubaowork\lumin-lang-compiler\src\lex\lex.l'
with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

old = '''"<string>"         { yylval.ll = CAST_STRING; LEX_RET(TOK_TYPE_ANNOT); }'''

new = '''"<string>"         { yylval.ll = CAST_STRING; LEX_RET(TOK_TYPE_ANNOT); }
"<ascii>"          { yylval.ll = CAST_ASCII; LEX_RET(TOK_TYPE_ANNOT); }'''

if old in content:
    content = content.replace(old, new, 1)
    print('OK: added <ascii> type annotation')
else:
    print('ERROR: old <string> rule not found')

with open(path, 'w', encoding='utf-8') as f:
    f.write(content)
