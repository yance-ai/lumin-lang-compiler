path = r'E:\doubaowork\lumin-lang-compiler\src\lex\lex.l'
with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

old = '''"<uint32>"         { yylval.ll = CAST_UINT32; LEX_RET(TOK_TYPE_ANNOT); }'''

new = '''"<uint32>"         { yylval.ll = CAST_UINT32; LEX_RET(TOK_TYPE_ANNOT); }
"<uint>"           { yylval.ll = CAST_UINT32; LEX_RET(TOK_TYPE_ANNOT); }'''

if old in content:
    content = content.replace(old, new, 1)
    print('OK: added <uint> type annotation')
else:
    print('ERROR: old <uint32> rule not found')

with open(path, 'w', encoding='utf-8') as f:
    f.write(content)
