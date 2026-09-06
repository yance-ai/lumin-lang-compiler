%{
#include "yacc.tab.h"
#include <stdio.h>
#include <string.h>
%}

%%
"func"          { fprintf(stderr,"[LEX] FUNC\n"); return FUNC; }

"print"         { fprintf(stderr,"[LEX] PRINT\n"); return PRINT; }
"true"          { fprintf(stderr,"[LEX] TRUE\n"); return TRUE; }
"false"         { fprintf(stderr,"[LEX] FALSE\n"); return FALSE; }
"if"            { fprintf(stderr,"[LEX] IF\n"); return IF; }
"elseif"        { fprintf(stderr,"[LEX] ELSEIF\n"); return ELSEIF; }
"else"          { fprintf(stderr,"[LEX] ELSE\n"); return ELSE; }
"while"         { fprintf(stderr,"[LEX] WHILE\n"); return WHILE; }
"for"           { fprintf(stderr,"[LEX] FOR\n"); return FOR; }

"switch"        { fprintf(stderr,"[LEX] SWITCH\n");  return SWITCH; }
"case"          { fprintf(stderr,"[LEX] CASE\n");    return CASE; }
"default"       { fprintf(stderr,"[LEX] DEFAULT\n"); return DEFAULT; }
"break"         { fprintf(stderr,"[LEX] BREAK\n");   return BREAK; }
"continue"      { fprintf(stderr,"[LEX] CONTINUE\n"); return CONTINUE; }
"return"        { fprintf(stderr,"[LEX] RETURN\n");  return RETURN; }

"int"           { return TOK_INT; }
"double"        { return TOK_DOUBLE; }
"char"          { return TOK_CHAR; }
"string"        { return TOK_STRING; }
"bool"          { return TOK_BOOL; }
"ASCII"         { return TOK_ASCII; }

"..."           { fprintf(stderr,"[LEX] ELLIPSIS\n"); return ELLIPSIS; }

\"([^"\\]|\\.)*\"   {
    fprintf(stderr,"[LEX] STRING_LIT %s\n",yytext);
    yylval.s = strdup(yytext+1);
    yylval.s[strlen(yylval.s)-1] = '\0';
    return STRING_LIT;
}

'[^'\''\n]'      {
    yylval.ch = yytext[1];
    fprintf(stderr,"[LEX] TOK_CHAR_LIT %c\n", yylval.ch);
    return TOK_CHAR_LIT;
}

[a-zA-Z_][a-zA-Z0-9_]*  {
    fprintf(stderr,"[LEX] ID %s\n",yytext);
    yylval.s = strdup(yytext);
    return ID;
}

[0-9]+\.[0-9]+          {
    fprintf(stderr,"[LEX] NUMBER %s\n",yytext);
    yylval.d = atof(yytext);
    return NUMBER;
}

[0-9]+                  {
    fprintf(stderr,"[LEX] INTEGER %s\n",yytext);
    yylval.ll = atoll(yytext);
    return INTEGER;
}

"++"        { fprintf(stderr,"[LEX] PLUSPLUS\n"); return PLUSPLUS; }
"--"        { fprintf(stderr,"[LEX] MINUSMINUS\n"); return MINUSMINUS; }
">="        { fprintf(stderr,"[LEX] GE\n"); return GE; }
"<="        { fprintf(stderr,"[LEX] LE\n"); return LE; }
"=="        { fprintf(stderr,"[LEX] EQ\n"); return EQ; }
"!="        { fprintf(stderr,"[LEX] NE\n"); return NE; }
">"         { fprintf(stderr,"[LEX] GT\n"); return GT; }
"<"         { fprintf(stderr,"[LEX] LT\n"); return LT; }

"+"             { fprintf(stderr,"[LEX] PLUS\n"); return PLUS; }
"-"             { fprintf(stderr,"[LEX] MINUS\n"); return MINUS; }
"*"             { fprintf(stderr,"[LEX] MUL\n"); return MUL; }
"/"             { fprintf(stderr,"[LEX] DIV\n"); return DIV; }

"?"         { fprintf(stderr,"[LEX] QMARK\n"); return QMARK; }
":"         { fprintf(stderr,"[LEX] COLON\n"); return COLON; }

"="             { fprintf(stderr,"[LEX] ASSIGN\n"); return ASSIGN; }
";"             { fprintf(stderr,"[LEX] SEMI\n"); return SEMI; }

"("             { fprintf(stderr,"[LEX] LPAREN\n"); return LPAREN; }
")"             { fprintf(stderr,"[LEX] RPAREN\n"); return RPAREN; }
"{"             { fprintf(stderr,"[LEX] LBRACE\n"); return LBRACE; }
"}"             { fprintf(stderr,"[LEX] RBRACE\n"); return RBRACE; }

","         { fprintf(stderr,"[LEX] COMMA\n"); return COMMA; }

"//"[^\n]*           ;   /* 行注释 */
"/*"([^*]|\*+[^*/])*\*+"/" ;   /* 块注释 */



[ \t\r\n]       ;

.               {
    fprintf(stderr,"[LEX] 非法字符: %c (0x%02x)\n", *yytext, (unsigned char)*yytext);
    return ERROR;
}
%%

int yywrap(void) {
    return 1;
}
