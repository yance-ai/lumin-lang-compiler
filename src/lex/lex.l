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
            return 1;
        default:
            return 0;
    }
}

#define LEX_RET(t) do { prev_token = (t); return (t); } while(0)
%}

%option yylineno

%%
"func"          { fprintf(stderr,"[LEX] FUNC\n"); LEX_RET(FUNC); }

"print"         { fprintf(stderr,"[LEX] PRINT\n"); LEX_RET(PRINT); }
"true"          { fprintf(stderr,"[LEX] TRUE\n"); LEX_RET(TRUE); }
"false"         { fprintf(stderr,"[LEX] FALSE\n"); LEX_RET(FALSE); }
"null"          { fprintf(stderr,"[LEX] NULL\n"); LEX_RET(NULL_LIT); }
"if"            { fprintf(stderr,"[LEX] IF\n"); LEX_RET(IF); }
"elseif"        { fprintf(stderr,"[LEX] ELSEIF\n"); LEX_RET(ELSEIF); }
"else"          { fprintf(stderr,"[LEX] ELSE\n"); LEX_RET(ELSE); }
"while"         { fprintf(stderr,"[LEX] WHILE\n"); LEX_RET(WHILE); }
"for"           { fprintf(stderr,"[LEX] FOR\n"); LEX_RET(FOR); }

"switch"        { fprintf(stderr,"[LEX] SWITCH\n");  LEX_RET(SWITCH); }
"case"          { in_case_expr = 1; fprintf(stderr,"[LEX] CASE\n");    LEX_RET(CASE); }
"default"       { in_case_expr = 1; fprintf(stderr,"[LEX] DEFAULT\n"); LEX_RET(DEFAULT); }
"try"           { fprintf(stderr,"[LEX] TRY\n");    LEX_RET(TRY); }
"throw"         { fprintf(stderr,"[LEX] THROW\n");  LEX_RET(THROW); }
"catch"         { fprintf(stderr,"[LEX] CATCH\n");  LEX_RET(CATCH); }
"finally"       { fprintf(stderr,"[LEX] FINALLY\n"); LEX_RET(FINALLY); }
"break"         { fprintf(stderr,"[LEX] BREAK\n");   LEX_RET(BREAK); }
"continue"      { fprintf(stderr,"[LEX] CONTINUE\n"); LEX_RET(CONTINUE); }
"return"        { fprintf(stderr,"[LEX] RETURN\n");  LEX_RET(RETURN); }

"int"           { LEX_RET(TOK_INT); }
"double"        { LEX_RET(TOK_DOUBLE); }
"char"          { LEX_RET(TOK_CHAR); }
"string"        { LEX_RET(TOK_STRING); }
"bool"          { LEX_RET(TOK_BOOL); }
"ASCII"         { LEX_RET(TOK_ASCII); }

"..."           { fprintf(stderr,"[LEX] ELLIPSIS\n"); LEX_RET(ELLIPSIS); }

\"([^"\\]|\\.)*\"   {
    fprintf(stderr,"[LEX] STRING_LIT %s\n",yytext);
    yylval.s = strdup(yytext+1);
    yylval.s[strlen(yylval.s)-1] = '\0';
    LEX_RET(STRING_LIT);
}

'[^'\''\n]'      {
    yylval.ch = yytext[1];
    fprintf(stderr,"[LEX] TOK_CHAR_LIT %c\n", yylval.ch);
    LEX_RET(TOK_CHAR_LIT);
}

"."                     {
    fprintf(stderr,"[LEX] DOT\n");
    LEX_RET(DOT);
}

[a-zA-Z_][a-zA-Z0-9_]*  {
    if(strcmp(yytext, "read") == 0) { fprintf(stderr,"[LEX] READ\n"); LEX_RET(READ); }
    if(strcmp(yytext, "write") == 0) { fprintf(stderr,"[LEX] WRITE\n"); LEX_RET(WRITE); }
    fprintf(stderr,"[LEX] ID %s\n",yytext);
    yylval.s = strdup(yytext);
    LEX_RET(ID);
}

[0-9]+\.[0-9]+          {
    fprintf(stderr,"[LEX] NUMBER %s\n",yytext);
    yylval.d = atof(yytext);
    LEX_RET(NUMBER);
}

[0-9]+                  {
    fprintf(stderr,"[LEX] INTEGER %s\n",yytext);
    yylval.ll = atoll(yytext);
    LEX_RET(INTEGER);
}

"++"        { fprintf(stderr,"[LEX] PLUSPLUS\n"); LEX_RET(PLUSPLUS); }
"--"        { fprintf(stderr,"[LEX] MINUSMINUS\n"); LEX_RET(MINUSMINUS); }
">="        { fprintf(stderr,"[LEX] GE\n"); LEX_RET(GE); }
"<="        { fprintf(stderr,"[LEX] LE\n"); LEX_RET(LE); }
"=="        { fprintf(stderr,"[LEX] EQ\n"); LEX_RET(EQ); }
"!="        { fprintf(stderr,"[LEX] NE\n"); LEX_RET(NE); }
">"         { fprintf(stderr,"[LEX] GT\n"); LEX_RET(GT); }
"<"         { fprintf(stderr,"[LEX] LT\n"); LEX_RET(LT); }
"&&"        { fprintf(stderr,"[LEX] AND\n"); LEX_RET(AND); }
"||"        { fprintf(stderr,"[LEX] OR\n"); LEX_RET(OR); }
"!"         { fprintf(stderr,"[LEX] NOT\n"); LEX_RET(NOT); }
"%"         { fprintf(stderr,"[LEX] MOD\n"); LEX_RET(MOD); }

"+"             { fprintf(stderr,"[LEX] PLUS\n"); LEX_RET(PLUS); }
"-"             { fprintf(stderr,"[LEX] MINUS\n"); LEX_RET(MINUS); }
"*"             { fprintf(stderr,"[LEX] MUL\n"); LEX_RET(MUL); }
"/"             { fprintf(stderr,"[LEX] DIV\n"); LEX_RET(DIV); }

"+="        { fprintf(stderr,"[LEX] PLUSEQ\n"); LEX_RET(PLUSEQ); }
"-="        { fprintf(stderr,"[LEX] MINUSEQ\n"); LEX_RET(MINUSEQ); }
"*="        { fprintf(stderr,"[LEX] MULEQ\n"); LEX_RET(MULEQ); }
"/="        { fprintf(stderr,"[LEX] DIVEQ\n"); LEX_RET(DIVEQ); }

"?"         { fprintf(stderr,"[LEX] QMARK\n"); LEX_RET(QMARK); }
":"         { if(in_case_expr) { in_case_expr = 0; fprintf(stderr,"[LEX] CASE_COLON\n"); LEX_RET(CASE_COLON); }
             fprintf(stderr,"[LEX] COLON\n"); LEX_RET(COLON); }

"="             { fprintf(stderr,"[LEX] ASSIGN\n"); LEX_RET(ASSIGN); }
";"             { fprintf(stderr,"[LEX] SEMI\n"); LEX_RET(SEMI); }

"("             { fprintf(stderr,"[LEX] LPAREN\n"); LEX_RET(LPAREN); }
")"             { fprintf(stderr,"[LEX] RPAREN\n"); LEX_RET(RPAREN); }
"{"             { fprintf(stderr,"[LEX] %s\n", is_expr_end(prev_token) ? "LBRACE" : "MAP_OPEN");
                  if(is_expr_end(prev_token)) LEX_RET(LBRACE);
                  else { prev_token = MAP_OPEN; return MAP_OPEN; } }
"}"             { fprintf(stderr,"[LEX] RBRACE\n"); LEX_RET(RBRACE); }
"["             { fprintf(stderr,"[LEX] %s\n", is_expr_end(prev_token) ? "LBRACKET" : "ARRAY_OPEN");
                  if(is_expr_end(prev_token)) LEX_RET(LBRACKET);
                  else { prev_token = ARRAY_OPEN; return ARRAY_OPEN; } }
"]"             { fprintf(stderr,"[LEX] RBRACKET\n"); LEX_RET(RBRACKET); }

","         { fprintf(stderr,"[LEX] COMMA\n"); LEX_RET(COMMA); }

"//"[^\n]*           ;   /* 行注释 */
"/*"([^*]|\*+[^*/])*\*+"/" ;   /* 块注释 */



[ \t\r\n]       ;

.               {
    fprintf(stderr,"[LEX] 第%d行 非法字符: %c (0x%02x)\n", yylineno, *yytext, (unsigned char)*yytext);
    LEX_RET(ERROR);
}
%%

int yywrap(void) {
    return 1;
}
