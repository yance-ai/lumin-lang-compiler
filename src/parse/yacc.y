%code requires {
    typedef struct AstNode AstNode;
}
%{
#define YYERROR_VERBOSE
#include "ast/ast.h"
#include "ast/func_compile.h"
#include <stdio.h>
extern int yylineno;
AstNode* new_cast_node(int cast_type, AstNode* child);
void yyerror(const char* s);
int yylex(void);
AstNode* root;
// AST 构造辅助：报错定位用（节点行号 = 当前 lookahead 行）
static inline AstNode* l_set_line(AstNode* __n) { if(__n) __n->line = yylineno; return __n; }
#define L(n) l_set_line(n)
%}

%union {
    double d;
    long long ll;
    char ch;
    char* s;
    AstNode* node;
}

%token PRINT ID NUMBER INTEGER PLUS MINUS MUL DIV ASSIGN SEMI LPAREN RPAREN
%token TRUE FALSE STRING_LIT
%token IF ELSEIF ELSE
%token GE LE EQ NE GT LT
%token LBRACE RBRACE
%token WHILE FOR
%token TOK_CHAR_LIT
%token TOK_INT TOK_DOUBLE TOK_CHAR TOK_STRING TOK_BOOL TOK_ASCII
%token PLUSPLUS MINUSMINUS
%token QMARK COLON
%token SWITCH CASE DEFAULT BREAK RETURN
%token CONTINUE
%token FUNC ELLIPSIS
%token COMMA
%token AND OR NOT MOD
%token PLUSEQ MINUSEQ MULEQ DIVEQ
%token LBRACKET RBRACKET
%token ARRAY_OPEN
%token ERROR

%right PLUSPLUS MINUSMINUS   /*后置自增，最高优先级*/
%left PLUS MINUS
%left MUL DIV MOD
%left GT LT GE LE EQ NE
%left AND
%left OR
%right QMARK COLON   /*三元 ?: 右结合，低于比较*/
%right ASSIGN        /*赋值最低*/
%precedence ELSE

%type<node> program stmt_list closed_stmt open_stmt block_stmt
%type<node> elif_clause_list elif_clause else_part
%type<node> expr ternary_expr logic_or_expr logic_and_expr assignment_expr unary_expr postfix_expr multiplicative_expr additive_expr comparison_expr expr_opt for_init for_incr primary
%type<node> switch_stmt case_list case_item break_stmt continue_stmt const_expr return_stmt
%type<node> func_def param_list param arg_list arg
%type <ch> char_lit
%type<ll> INTEGER
%type<d> NUMBER
%type<s> ID STRING_LIT

%%

program
    : stmt_list { $$ = $1; root = $$; }
    ;

stmt_list
    : %empty                   { $$ = NULL; }
    | stmt_list closed_stmt    { $$ = ast_seq($1, $2); }
    ;

closed_stmt
    : expr SEMI                      { $$ = $1; }
    | PRINT LPAREN expr RPAREN SEMI  { $$ = ast_print($3); }
    | block_stmt                     { $$ = $1; }
    | open_stmt                      { $$ = $1; }
    | WHILE LPAREN expr RPAREN closed_stmt     { $$ = ast_while($3, $5); }
    | FOR LPAREN for_init SEMI expr_opt SEMI for_incr RPAREN closed_stmt { $$ = ast_for($3, $5, $7, $9); }
    | switch_stmt                    { $$ = $1; }
    | break_stmt                     { $$ = $1; }
    | continue_stmt                  { $$ = $1; }
    | return_stmt                    { $$ = $1; }
    | func_def                       { $$ = $1; }          /* 新增函数定义语句 */
    ;

func_def : FUNC ID LPAREN param_list RPAREN block_stmt {
          $$ = ast_func_def($2, $4, $6);
          /* 语义分析阶段：编译这个函数定义，生成RuntimeFunc，注册到全局符号 */
          RuntimeFunc* rf = compile_func_from_ast($$);
          Value func_val;
          func_val.type = VAL_FUNC;
          func_val.v.func.func_obj = rf;
          sym_set($2, func_val); /* 注册到运行时符号表，后续调用可以查到 */
        } ;

/* 参数列表：支持 a,b,...rest；可变参数只能放在最后一个 */
param_list
    : %empty                 { $$ = NULL; }
    | param                  { $$ = $1; }
    | param_list COMMA param { $$ = ast_param_append($1, $3); }
;

param
    : ID                     { $$ = ast_param($1, 0); } /*普通参数 is_ellipsis=0 */
    | ELLIPSIS ID            { $$ = ast_param($2, 1); } /* ...args 可变参数 is_ellipsis=1 */
;

/* 调用实参列表 */
arg_list
    : %empty               { $$ = NULL; }
    | arg                  { $$ = $1; }
    | arg_list COMMA arg   { $$ = ast_arg_append($1, $3); }
;

arg
    : expr                 { $$ = $1; }
;


open_stmt
    : IF LPAREN expr RPAREN closed_stmt elif_clause_list else_part         { $$ = ast_if_chain($3, $5, $6, $7); }
    ;

block_stmt
    : LBRACE stmt_list RBRACE      { $$ = ast_block($2); }
    | LBRACE RBRACE                { $$ = ast_block(NULL); }
    ;

break_stmt
    : BREAK SEMI { $$ = ast_break(); }
    ;

continue_stmt
    : CONTINUE SEMI { $$ = ast_continue(); }
    ;

return_stmt    : RETURN SEMI              { $$ = ast_return(NULL); }
               | RETURN expr SEMI         { $$ = ast_return($2); }
;

switch_stmt
    : SWITCH LPAREN expr RPAREN LBRACE case_list RBRACE {
        $$ = ast_switch($3, $6);
    }
    ;

case_list
    : %empty                 { $$ = NULL; }
    | case_list case_item    { $$ = ast_case_append($1, $2); }
    ;

case_item
    : CASE const_expr COLON stmt_list {
        $$ = ast_case($2, $4, 0);
    }
    | DEFAULT COLON stmt_list {
        $$ = ast_case(NULL, $3, 1);
    }
    ;

/* case后面只能是编译期常量：数字、整数、char字面量、字符串字面量 */
const_expr
    : NUMBER                  { $$ = ast_num($1); }
    | INTEGER                 { $$ = ast_int($1); }
    | char_lit                { $$ = ast_new_char($1); }
    | STRING_LIT              { $$ = ast_string($1); free($1); }
    ;

elif_clause_list
    : %empty                       { $$ = NULL; }
    | elif_clause_list elif_clause { $$ = ast_elif_append($1, $2); }
    ;

elif_clause
    : ELSEIF LPAREN expr RPAREN closed_stmt { $$ = ast_elif($3, $5); }
    ;

else_part
    : %empty           { $$ = NULL; }
    | ELSE closed_stmt { $$ = $2; }
    ;

expr_opt
    : %empty { $$ = NULL; }
    | expr   { $$ = $1; }
    ;

for_init
    : %empty                { $$ = NULL; }
    | expr                  { $$ = $1; }
    ;

for_incr
    : %empty                { $$ = NULL; }
    | expr                  { $$ = $1; }
    ;

char_lit
    : TOK_CHAR_LIT { $$ = $<ch>1; }
    ;

primary
    : NUMBER                  { $$ = ast_num($1); }
    | INTEGER                 { $$ = ast_int($1); }
    | TRUE                    { $$ = ast_bool(1); }
    | FALSE                   { $$ = ast_bool(0); }
    | STRING_LIT              { $$ = ast_string($1); free($1); }
    | char_lit                { $$ = ast_new_char($1); }
    | ID                      { $$ = L(ast_var($1)); }
    | ID LPAREN arg_list RPAREN { $$ = L(ast_call($1, $3)); }  /* 函数调用 foo(a,b,c) */
    | ARRAY_OPEN arg_list RBRACKET { $$ = ast_array_lit($2); }  /* 数组字面量 [1,2,3] / []（lexer 按上下文消歧） */
    | LPAREN expr RPAREN      { $$ = $2; }
    | LPAREN TOK_INT RPAREN primary        { $$ = new_cast_node(CAST_INT, $4); }
    | LPAREN TOK_DOUBLE RPAREN primary     { $$ = new_cast_node(CAST_DOUBLE, $4); }
    | LPAREN TOK_STRING RPAREN primary     { $$ = new_cast_node(CAST_STRING, $4); }
    | LPAREN TOK_BOOL RPAREN primary       { $$ = new_cast_node(CAST_BOOL, $4); }
    | LPAREN TOK_ASCII RPAREN primary      { $$ = new_cast_node(CAST_ASCII, $4); }
    | LPAREN TOK_CHAR RPAREN primary       { $$ = new_cast_node(CAST_CHAR, $4); }
    ;

postfix_expr
    : primary
    | postfix_expr LBRACKET expr RBRACKET  { $$ = L(ast_index($1, $3)); }  /* 数组下标 a[i] */
    | postfix_expr PLUSPLUS   { $$ = ast_unary(OP_POST_INC, $1); }
    | postfix_expr MINUSMINUS { $$ = ast_unary(OP_POST_DEC, $1); }
    ;

unary_expr
    : postfix_expr
    | PLUSPLUS unary_expr     { $$ = ast_unary(OP_PRE_INC, $2); }
    | MINUSMINUS unary_expr   { $$ = ast_unary(OP_PRE_DEC, $2); }
    | PLUS unary_expr         { $$ = ast_unary(OP_UNARY_PLUS, $2); }
    | MINUS unary_expr        { $$ = ast_unary(OP_UNARY_MINUS, $2); }
    | NOT unary_expr          { $$ = ast_unary(OP_LOGIC_NOT, $2); }
    ;

multiplicative_expr
    : unary_expr
    | multiplicative_expr MUL unary_expr  { $$ = ast_binop(OP_MUL, $1, $3); }
    | multiplicative_expr DIV unary_expr  { $$ = ast_binop(OP_DIV, $1, $3); }
    | multiplicative_expr MOD unary_expr  { $$ = ast_binop(OP_MOD, $1, $3); }
    ;

additive_expr
    : multiplicative_expr
    | additive_expr PLUS multiplicative_expr  { $$ = ast_binop(OP_ADD, $1, $3); }
    | additive_expr MINUS multiplicative_expr { $$ = ast_binop(OP_SUB, $1, $3); }
    ;

comparison_expr
    : additive_expr
    | comparison_expr GT additive_expr    { $$ = ast_binop(OP_GT, $1, $3); }
    | comparison_expr LT additive_expr    { $$ = ast_binop(OP_LT, $1, $3); }
    | comparison_expr GE additive_expr    { $$ = ast_binop(OP_GE, $1, $3); }
    | comparison_expr LE additive_expr    { $$ = ast_binop(OP_LE, $1, $3); }
    | comparison_expr EQ additive_expr    { $$ = ast_binop(OP_EQ, $1, $3); }
    | comparison_expr NE additive_expr    { $$ = ast_binop(OP_NE, $1, $3); }
    ;

logic_and_expr
    : comparison_expr
    | logic_and_expr AND comparison_expr  { $$ = ast_binop(OP_LOGIC_AND, $1, $3); }
    ;

logic_or_expr
    : logic_and_expr
    | logic_or_expr OR logic_and_expr     { $$ = ast_binop(OP_LOGIC_OR, $1, $3); }
    ;

ternary_expr
    : logic_or_expr
    | logic_or_expr QMARK expr COLON ternary_expr  { $$ = ast_ternary($1, $3, $5); }
    ;

assignment_expr
    : ternary_expr
    | ID ASSIGN assignment_expr  { $$ = ast_assign($1, $3); }
    | ID PLUSEQ assignment_expr  { $$ = ast_assign($1, ast_binop(OP_ADD, ast_var($1), $3)); }
    | ID MINUSEQ assignment_expr { $$ = ast_assign($1, ast_binop(OP_SUB, ast_var($1), $3)); }
    | ID MULEQ assignment_expr   { $$ = ast_assign($1, ast_binop(OP_MUL, ast_var($1), $3)); }
    | ID DIVEQ assignment_expr   { $$ = ast_assign($1, ast_binop(OP_DIV, ast_var($1), $3)); }
    | postfix_expr LBRACKET expr RBRACKET ASSIGN assignment_expr
        { $$ = ast_index_assign($1, $3, $6); }
    | postfix_expr LBRACKET expr RBRACKET PLUSEQ assignment_expr
        { $$ = ast_index_assign($1, $3, ast_binop(OP_ADD, ast_index(ast_clone_node($1), ast_clone_node($3)), $6)); }
    | postfix_expr LBRACKET expr RBRACKET MINUSEQ assignment_expr
        { $$ = ast_index_assign($1, $3, ast_binop(OP_SUB, ast_index(ast_clone_node($1), ast_clone_node($3)), $6)); }
    | postfix_expr LBRACKET expr RBRACKET MULEQ assignment_expr
        { $$ = ast_index_assign($1, $3, ast_binop(OP_MUL, ast_index(ast_clone_node($1), ast_clone_node($3)), $6)); }
    | postfix_expr LBRACKET expr RBRACKET DIVEQ assignment_expr
        { $$ = ast_index_assign($1, $3, ast_binop(OP_DIV, ast_index(ast_clone_node($1), ast_clone_node($3)), $6)); }
    ;

expr
    : assignment_expr
    ;

%%

void yyerror(const char* s){
    fprintf(stderr,"语法错误(第%d行): %s\n", yylineno, s);
}
