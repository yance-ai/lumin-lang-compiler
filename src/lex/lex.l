/* 编译期模块：prev_token/in_case_expr 为单线程词法状态，未来并发编译需实例化。 */
%{
#include "yacc.tab.h"
#include <stdio.h>
#include <string.h>

// 前一个 token：用于区分 `[` 是下标（表达式后）还是数组字面量（表达式开始）
static int prev_token = 0;

// 表达式结束类 token（其后 `[` 是下标）
static int in_case_expr = 0;   /* case 值后的冒号 → CASE_COLON（与字典/三元冒号区分） */

static int is_expr_end(int t)
{
    switch(t) {
        case ID: case INTEGER: case NUMBER: case STRING_LIT: case TOK_CHAR_LIT:
        case TRUE: case FALSE: case RBRACKET: case RPAREN:
        case PLUSPLUS: case MINUSMINUS:
        case ELSE: case ELSEIF:
        case CASE_COLON:          /* case 10: { → 块 */
        case TRY:                 /* try { → 块 */
        case FINALLY:             /* finally { → 块 */
        case TOK_DO:              /* do { → 块 */
            return 1;
        default:
            return 0;
    }
}

#define LEX_RET(t) do { prev_token = (t); return (t); } while(0)
%}

%option yylineno

%%
"type"          { LEX_RET(TOK_TYPE); }
"enum"          { LEX_RET(TOK_ENUM); }
"interface"     { LEX_RET(TOK_INTERFACE); }
"implements"    { LEX_RET(TOK_IMPLEMENTS); }
"extends"       { LEX_RET(TOK_EXTENDS); }
"extend"        { LEX_RET(TOK_EXTEND); }
"unpack"        { LEX_RET(TOK_UNPACK); }
"func"          { LEX_RET(FUNC); }
"gen"           { LEX_RET(TOK_GEN); }
"yield"         { LEX_RET(TOK_YIELD); }
"const"         { LEX_RET(CONST); }
"macro"         { LEX_RET(MACRO); }

"print"         { LEX_RET(PRINT); }
"true"          { LEX_RET(TRUE); }
"false"         { LEX_RET(FALSE); }
"null"          { LEX_RET(NULL_LIT); }
"if"            { LEX_RET(IF); }
"elseif"        { LEX_RET(ELSEIF); }
"else"          { LEX_RET(ELSE); }
"while"         { LEX_RET(WHILE); }
"do"            { LEX_RET(TOK_DO); }
"for"           { LEX_RET(FOR); }
"@"             { LEX_RET(TOK_AT); }
"in"            { LEX_RET(TOK_IN); }
"iter"          { LEX_RET(TOK_ITER); }

"switch"        {  LEX_RET(SWITCH); }
"match"         {  LEX_RET(SWITCH); }  /* match as alias for pattern matching */
"case"          { in_case_expr = 1;    LEX_RET(CASE); }
"default"       { in_case_expr = 1; LEX_RET(DEFAULT); }
"try"           {    LEX_RET(TRY); }
"throw"         {  LEX_RET(THROW); }
"catch"         {  LEX_RET(CATCH); }
"finally"       { LEX_RET(FINALLY); }
"break"         {   LEX_RET(BREAK); }
"continue"      { LEX_RET(CONTINUE); }
"return"        {  LEX_RET(RETURN); }

"int"           { LEX_RET(TOK_INT); }
"double"        { LEX_RET(TOK_DOUBLE); }
"char"          { LEX_RET(TOK_CHAR); }
"string"        { LEX_RET(TOK_STRING); }
"bool"          { LEX_RET(TOK_BOOL); }
"ascii"         { LEX_RET(TOK_ASCII); }
"byte"          { LEX_RET(TOK_BYTE); }
"int8"          { LEX_RET(TOK_INT8); }
"int16"         { LEX_RET(TOK_INT16); }
"int32"         { LEX_RET(TOK_INT32); }
"int64"         { LEX_RET(TOK_INT64); }
"uint8"         { LEX_RET(TOK_UINT8); }
"uint16"        { LEX_RET(TOK_UINT16); }
"uint32"        { LEX_RET(TOK_UINT32); }
"uint64"        { LEX_RET(TOK_UINT64); }
"uint"          { LEX_RET(TOK_UINT); }
"long long"     { LEX_RET(TOK_LONGLONG); }
"long"          { LEX_RET(TOK_LONG); }
"float"         { LEX_RET(TOK_FLOAT); }

"..."           { LEX_RET(ELLIPSIS); }

\"([^"\\]|\\.)*\"   {
    yylval.s = strdup(yytext+1);
    yylval.s[strlen(yylval.s)-1] = '\0';
    LEX_RET(STRING_LIT);
}

f\"([^"\\]|\\.)*\"   {
    yylval.s = strdup(yytext+2);   /* 跳过 f 和开头引号 */
    yylval.s[strlen(yylval.s)-1] = '\0';
    LEX_RET(FSTRING_LIT);
}

'[^'\''\n]'      {
    yylval.ch = yytext[1];
    LEX_RET(TOK_CHAR_LIT);
}

"."                     {
    LEX_RET(DOT);
}

[a-zA-Z_][a-zA-Z0-9_]*  {
    if(strcmp(yytext, "read") == 0) { LEX_RET(READ); }
    if(strcmp(yytext, "write") == 0) { LEX_RET(WRITE); }
    yylval.s = strdup(yytext);
    LEX_RET(ID);
}

[0-9]+\.[0-9]+          {
    yylval.d = atof(yytext);
    LEX_RET(NUMBER);
}

[0-9]+                  {
    yylval.ll = atoll(yytext);
    LEX_RET(INTEGER);
}

"++"        { LEX_RET(PLUSPLUS); }
"--"        { LEX_RET(MINUSMINUS); }
"?."        { LEX_RET(SAFE_CALL); }
"??"        { LEX_RET(NULL_COALESCE); }
">="        { LEX_RET(GE); }
"<="        { LEX_RET(LE); }
"=="        { LEX_RET(EQ); }
"!="        { LEX_RET(NE); }
">"         { LEX_RET(GT); }
"<"         { LEX_RET(LT); }
"&&"        { LEX_RET(AND); }
"||"        { LEX_RET(OR); }
"!"         { LEX_RET(NOT); }
"%"         { LEX_RET(MOD); }

"+"             { LEX_RET(PLUS); }
"-"             { LEX_RET(MINUS); }
"*"             { LEX_RET(MUL); }
"/"             { LEX_RET(DIV); }

"+="        { LEX_RET(PLUSEQ); }
"-="        { LEX_RET(MINUSEQ); }
"*="        { LEX_RET(MULEQ); }
"/="        { LEX_RET(DIVEQ); }

"?"         { LEX_RET(QMARK); }
":"         { if(in_case_expr) { in_case_expr = 0; LEX_RET(CASE_COLON); }
             LEX_RET(COLON); }

"="             { LEX_RET(ASSIGN); }
";"             { LEX_RET(SEMI); }

"("             { LEX_RET(LPAREN); }
")"             { LEX_RET(RPAREN); }
"{"             {
                  if(is_expr_end(prev_token)) LEX_RET(LBRACE);
                  else { prev_token = MAP_OPEN; return MAP_OPEN; } }
"}"             { LEX_RET(RBRACE); }
"["             {
                  if(is_expr_end(prev_token)) LEX_RET(LBRACKET);
                  else { prev_token = ARRAY_OPEN; return ARRAY_OPEN; } }
"]"             { LEX_RET(RBRACKET); }

","         { LEX_RET(COMMA); }

"//"[^\n]*           ;   /* 行注释 */
"/*"([^*]|\*+[^*/])*\*+"/" ;   /* 块注释 */



[ \t\r\n]       ;

.               {
    LEX_RET(ERROR);
}
%%

int yywrap(void) {
    return 1;
}
