/* 编译期模块：g_lambda_seq 为单线程语法状态，未来并发编译需实例化。 */
%code requires {
    typedef struct AstNode AstNode;
}
%{
#define YYERROR_VERBOSE
#include "ast/ast.h"
#include "ast/func_compile.h"
#include "ast/ast_types.h"
#include "parse/macro.h"
#include <stdio.h>
#include <string.h>

/* type 声明属性收集器（yacc 动作顺序填充，声明语句动作消费后清空） */
static char** g_prop_names = NULL;
static ValueType* g_prop_types = NULL;
static int g_prop_n = 0, g_prop_cap = 0;
static void type_prop_push(char* name, ValueType vt)
{
    if(g_prop_n >= g_prop_cap) {
        int nc = g_prop_cap > 0 ? g_prop_cap * 2 : 8;
        g_prop_names = (char**)realloc(g_prop_names, (size_t)nc * sizeof(char*));
        g_prop_types = (ValueType*)realloc(g_prop_types, (size_t)nc * sizeof(ValueType));
        g_prop_cap = nc;
    }
    g_prop_names[g_prop_n] = name;
    g_prop_types[g_prop_n] = vt;
    g_prop_n++;
}
static void type_prop_clear(void)
{
    for(int i = 0; i < g_prop_n; i++) free(g_prop_names[i]);
    free(g_prop_names); free(g_prop_types);
    g_prop_names = NULL; g_prop_types = NULL;
    g_prop_n = 0; g_prop_cap = 0;
}
/* 泛型 <Person>[e1,e2] → 每个元素包 Person(e) 构造调用（遍历 ast_seq 链） */
static AstNode* wrap_type_list(const char* tname, AstNode* chain)
{
    if(!chain) return NULL;
    if(chain->type == AST_SEQ) {
        chain->u.seq.first = wrap_type_list(tname, chain->u.seq.first);
        chain->u.seq.second = wrap_type_list(tname, chain->u.seq.second);
        return chain;
    }
    return ast_call(strdup(tname), chain);
}
/* catch 子句辅助：创建单个 catch 子句节点（用 AST_SEQ 包装，first=type_string, second=var_body_seq） */
static AstNode* make_catch_clause(char* type_name, char* var_name, AstNode* body)
{
    /* 用一个特殊节点存储 catch 子句：AST_SEQ(first=type_string, second=AST_SEQ(first=var_name_string, second=body))
       无类型时用空字符串占位，避免 first=NULL 导致其他遍历逻辑崩溃 */
    AstNode* type_node = ast_string(type_name ? type_name : "");
    AstNode* var_node = ast_string(var_name);
    AstNode* var_body = ast_seq(var_node, body);
    return ast_seq(type_node, var_body);
}

/* 递归展平左结合的 AST_SEQ 链，收集所有叶子节点（catch_clause） */
static void flatten_catch_seq(AstNode* node, AstNode*** arr, int* count, int* cap)
{
    if(!node) return;
    /* catch_clause 本身也是 AST_SEQ，但它的 first 是 AST_STRING（type 或 var），
       而 catch_clause_list 的 AST_SEQ 的 first 不是 AST_STRING，以此区分 */
    if(node->type == AST_SEQ && node->u.seq.first && node->u.seq.first->type != AST_STRING) {
        flatten_catch_seq(node->u.seq.first, arr, count, cap);
        flatten_catch_seq(node->u.seq.second, arr, count, cap);
    } else {
        if(*count >= *cap) {
            *cap = *cap > 0 ? *cap * 2 : 8;
            *arr = (AstNode**)realloc(*arr, sizeof(AstNode*) * (*cap));
        }
        (*arr)[(*count)++] = node;
    }
}

/* 判断是否是单个无类型 catch（用于回退到旧的 ast_try） */
static int is_single_untagged_catch(AstNode* catch_chain)
{
    if(!catch_chain || catch_chain->type != AST_SEQ) return 0;
    /* catch_clause 结构：AST_SEQ(first=type_string, second=AST_SEQ(first=var_string, second=body))
       如果 first 是 AST_STRING 且为空字符串，说明是无类型 catch */
    if(catch_chain->u.seq.first && catch_chain->u.seq.first->type == AST_STRING) {
        const char* s = catch_chain->u.seq.first->u.sval;
        if(s && s[0] == '\0') return 1;
    }
    return 0;
}

/* 从单个无类型 catch_clause 中提取 var_name 和 body */
static void extract_single_catch(AstNode* clause, char** var_name, AstNode** body)
{
    if(clause && clause->type == AST_SEQ) {
        AstNode* var_body = clause->u.seq.second;
        if(var_body && var_body->type == AST_SEQ) {
            AstNode* var_node = var_body->u.seq.first;
            *var_name = var_node ? strdup(var_node->u.sval) : NULL;
            *body = var_body->u.seq.second;
        }
    }
}

/* 从 AST_SEQ 链中收集 catch 子句，构建 ast_try_multi */
static AstNode* build_try_multi(AstNode* body, AstNode* catch_chain, AstNode* finally_body)
{
    AstNode** clauses = NULL;
    int count = 0, cap = 0;
    flatten_catch_seq(catch_chain, &clauses, &count, &cap);

    CatchClause* catches = (CatchClause*)calloc((size_t)count, sizeof(CatchClause));
    for(int i = 0; i < count; i++) {
        AstNode* clause = clauses[i];
        /* clause = AST_SEQ(first=type_string, second=AST_SEQ(first=var_string, second=body)) */
        if(clause && clause->type == AST_SEQ) {
            AstNode* type_node = clause->u.seq.first;
            AstNode* var_body = clause->u.seq.second;
            /* 空字符串表示无类型（捕获所有异常），NULL 表示有类型 */
            if(type_node && type_node->u.sval && type_node->u.sval[0] != '\0') {
                catches[i].type = strdup(type_node->u.sval);
            } else {
                catches[i].type = NULL;
            }
            if(var_body && var_body->type == AST_SEQ) {
                AstNode* var_node = var_body->u.seq.first;
                catches[i].var = var_node ? strdup(var_node->u.sval) : NULL;
                catches[i].body = var_body->u.seq.second;
            }
        }
    }

    free(clauses);
    return ast_try_multi(body, catches, count, finally_body);
}

extern int yylineno;
AstNode* new_cast_node(int cast_type, AstNode* child);
AstNode* maybe_template(const char* s);      // 字符串模板拆解（parse/tmpl.c）
void yyerror(const char* s);
static int g_lambda_seq = 0;                 // 匿名函数内部名 _lambda_N
int yylex(void);
AstNode* root;
/* 模块系统（第一阶段）：判断一个标识符是否为 import 别名命名空间 */
int lm_is_module_alias(const char* name);
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
%token TRUE FALSE NULL_LIT STRING_LIT FSTRING_LIT MAP_OPEN
%token IF ELSEIF ELSE
%token GE LE EQ NE GT LT
%token LBRACE RBRACE
%token WHILE FOR TOK_DO TOK_IN
%token TOK_CHAR_LIT
%token TOK_INT TOK_DOUBLE TOK_CHAR TOK_STRING TOK_BOOL TOK_ASCII TOK_BYTE
%token TOK_INT8 TOK_INT16 TOK_INT32 TOK_INT64 TOK_UINT8 TOK_UINT16 TOK_UINT32 TOK_UINT64 TOK_UINT TOK_LONG TOK_LONGLONG TOK_FLOAT
%token TOK_TYPE TOK_ENUM TOK_INTERFACE
%token PLUSPLUS MINUSMINUS
%token QMARK COLON CASE_COLON
%token SWITCH CASE DEFAULT BREAK RETURN TRY CATCH THROW FINALLY
%token CONTINUE
%token FUNC ELLIPSIS TOK_AT SAFE_CALL NULL_COALESCE CONST MACRO
%token READ WRITE
%token COMMA
%token AND OR NOT MOD
%token PLUSEQ MINUSEQ MULEQ DIVEQ
%token LBRACKET RBRACKET
%token ARRAY_OPEN
%token DOT
%token ERROR

%right PLUSPLUS MINUSMINUS   /*后置自增，最高优先级*/
%left GT LT GE LE EQ NE       /*比较运算符，优先级低于加法（与C语言一致）*/
%left PLUS MINUS              /*加法*/
%left MUL DIV MOD             /*乘法*/
%left AND
%left OR
%left NULL_COALESCE
%right QMARK COLON   /*三元 ?: 右结合，低于比较*/
%right ASSIGN        /*赋值最低*/
%precedence ELSE

%type<node> program stmt_list closed_stmt open_stmt block_stmt try_stmt
%type<node> elif_clause_list elif_clause else_part
%type<node> expr ternary_expr logic_or_expr logic_and_expr assignment_expr unary_expr postfix_expr multiplicative_expr additive_expr comparison_expr expr_opt for_init for_incr primary map_items map_item
%type<node> switch_stmt case_list case_item break_stmt continue_stmt const_expr return_stmt
%type<node> catch_clause_list catch_clause
%type<s> opt_catch_type
%type<node> func_def param_list param arg_list arg destruct_lhs type_prop_list type_prop enum_members enum_member annotation annotation_list macro_def generic_param_list generic_param_items opt_generic_param_list interface_methods interface_method
%type<ll> type_name builtin_type_name type_keyword
%type <ch> char_lit
%type<ll> INTEGER
%type<d> NUMBER
%type<s> ID STRING_LIT FSTRING_LIT

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
    | destruct_lhs ASSIGN expr SEMI {
        char** names = NULL; int cnt = 0;
        ast_collect_varnames($1, &names, &cnt);
        $$ = ast_destruct(names, cnt, $3);
    }
    | PRINT LPAREN expr RPAREN SEMI  { $$ = ast_print($3); }
    | block_stmt                     { $$ = $1; }
    /* 宏调用作为语句：如果是宏，则展开为语句列表；否则作为表达式语句 */
    | ID LPAREN arg_list RPAREN SEMI {
          if(macro_is_defined($1)) {
              AstNode* mdef = macro_lookup($1);
              $$ = macro_expand(mdef, $3);
          } else {
              $$ = L(ast_call($1, $3));
          }
      }
    | open_stmt                      { $$ = $1; }
    | WHILE LPAREN expr RPAREN closed_stmt     { $$ = ast_while($3, $5); }
    | FOR LPAREN for_init SEMI expr_opt SEMI for_incr RPAREN closed_stmt { $$ = ast_for($3, $5, $7, $9); }
    /* for-each 循环：for x in arr / for k,v in map，转换为普通 for 循环 */
    | FOR ID TOK_IN expr closed_stmt {
        static int fe_counter = 0;
        char iname[64]; snprintf(iname, sizeof(iname), "__fe_i_%d", fe_counter++);
        AstNode* iter_copy = ast_clone_node($4);
        AstNode* init = ast_assign(strdup(iname), ast_int(0));
        AstNode* cond = ast_binop(OP_LT, ast_var(strdup(iname)),
            ast_call(strdup("len"), ast_seq(ast_clone_node($4), NULL)));
        AstNode* update = ast_unary(OP_POST_INC, ast_var(strdup(iname)));
        AstNode* assign = ast_assign(strdup($2), ast_index(iter_copy, ast_var(strdup(iname))));
        AstNode* body = ast_block(ast_seq(assign, $5));
        $$ = ast_for(init, cond, update, body);
    }
    | FOR ID COMMA ID TOK_IN expr closed_stmt {
        static int fe_counter2 = 0;
        char kname[64], iname[64];
        snprintf(kname, sizeof(kname), "__fe_keys_%d", fe_counter2);
        snprintf(iname, sizeof(iname), "__fe_i_%d", fe_counter2++);
        AstNode* iter_copy = ast_clone_node($6);
        AstNode* keys_init = ast_assign(strdup(kname),
            ast_call(strdup("keys"), ast_seq(ast_clone_node($6), NULL)));
        AstNode* init = ast_assign(strdup(iname), ast_int(0));
        AstNode* cond = ast_binop(OP_LT, ast_var(strdup(iname)),
            ast_call(strdup("len"), ast_seq(ast_var(strdup(kname)), NULL)));
        AstNode* update = ast_unary(OP_POST_INC, ast_var(strdup(iname)));
        AstNode* k_assign = ast_assign(strdup($2), ast_index(ast_var(strdup(kname)), ast_var(strdup(iname))));
        AstNode* v_assign = ast_assign(strdup($4), ast_index(iter_copy, ast_var(strdup($2))));
        AstNode* body = ast_block(ast_seq(k_assign, ast_seq(v_assign, $7)));
        AstNode* for_stmt = ast_for(init, cond, update, body);
        $$ = ast_block(ast_seq(keys_init, for_stmt));
    }

    | TOK_DO closed_stmt WHILE LPAREN expr RPAREN SEMI { $$ = ast_do_while($5, $2); }
    | switch_stmt                    { $$ = $1; }
    | break_stmt                     { $$ = $1; }
    | continue_stmt                  { $$ = $1; }
    | return_stmt                    { $$ = $1; }
    | func_def                       { $$ = $1; }          /* 新增函数定义语句 */
    | macro_def                      { $$ = $1; }          /* 宏定义语句 */
    | WRITE STRING_LIT expr SEMI {
          /* write "path" value → write_file(path, value)；普通路径不内插 */
          AstNode* p = ast_string($2);
          free($2);
          $$ = L(ast_call(strdup("write_file"), ast_seq(p, $3)));
      }
    | WRITE FSTRING_LIT expr SEMI {
          /* write f"path" value → write_file(path, value)；f 前缀路径支持模板内插 */
          AstNode* p = L(maybe_template($2));
          free($2);
          $$ = L(ast_call(strdup("write_file"), ast_seq(p, $3)));
      }
    | try_stmt { $$ = $1; }
    | THROW expr SEMI {
          /* throw expr：显式抛错；值在运行时包装成错误对象 */
          $$ = L(ast_throw($2));
      }
    | TOK_TYPE opt_generic_param_list ID LBRACE type_prop_list RBRACE {
          /* type Person { ... } 或 type <T> Box { ... }：编译期注册形状 */
          char** gnames = NULL;
          int gcnt = 0;
          AstNode* gp = $2;
          while(gp) { gcnt++; gp = gp->u.param.next; }
          if(gcnt > 0) {
              gnames = (char**)malloc((size_t)gcnt * sizeof(char*));
              int gi = 0;
              gp = $2;
              while(gp) { gnames[gi++] = strdup(gp->u.param.name); gp = gp->u.param.next; }
          }
          type_register($3, g_prop_names, g_prop_types, g_prop_n, gnames, gcnt);
          if(gnames) { for(int gi = 0; gi < gcnt; gi++) free(gnames[gi]); free(gnames); }
          type_prop_clear();
          free($3);
          $$ = L(ast_none());
      }
    | TOK_ENUM ID LBRACE enum_members RBRACE {
          /* enum Color { RED, GREEN } → Color = {"RED":"RED","GREEN":"GREEN"} */
          $$ = L(ast_assign($2, ast_map_lit($4)));
      }
    | TOK_INTERFACE ID LBRACE interface_methods RBRACE {
          /* interface Printable { func to_string(): string }：注册接口到符号表 */
          interface_register($2, $4);
          free($2);
          $$ = L(ast_none());
      }
    ;

/* 接口方法签名列表 */
interface_methods : interface_method { $$ = $1; }
                  | interface_methods interface_method { $$ = ast_param_append($1, $2); }
                  ;

/* 接口方法签名：func name(params): return_type */
interface_method : FUNC ID LPAREN param_list RPAREN COLON type_name SEMI {
                      $$ = ast_param($2, 0, NULL);
                      /* 用 constraint 字段存储返回类型，简化实现 */
                      $$->u.param.constraint = valtype_to_name($7);
                  }
                  | FUNC ID LPAREN param_list RPAREN SEMI {
                      $$ = ast_param($2, 0, NULL);
                  }
                  ;

func_def : FUNC ID LPAREN param_list RPAREN block_stmt {
          $$ = ast_func_def($2, $4, $6);
          $$->u.func_def.annotations = NULL;
          /* 语义分析阶段：编译这个函数定义，生成RuntimeFunc，注册到全局符号 */
          RuntimeFunc* rf = compile_func_from_ast($$);
          Value func_val;
          func_val.type = VAL_FUNC;
          func_val.v.func.func_obj = rf;
          sym_set($2, func_val); /* 注册到运行时符号表，后续调用可以查到 */
        }
        | annotation_list FUNC ID LPAREN param_list RPAREN block_stmt {
          $$ = ast_func_def($3, $5, $7);
          $$->u.func_def.annotations = $1;
          /* 语义分析阶段：编译这个函数定义，生成RuntimeFunc，注册到全局符号 */
          RuntimeFunc* rf = compile_func_from_ast($$);
          Value func_val;
          func_val.type = VAL_FUNC;
          func_val.v.func.func_obj = rf;
          sym_set($3, func_val); /* 注册到运行时符号表，后续调用可以查到 */
        }
        | CONST FUNC ID LPAREN param_list RPAREN block_stmt {
          $$ = ast_func_def($3, $5, $7);
          $$->u.func_def.annotations = NULL;
          $$->u.func_def.is_const = 1;
          /* 语义分析阶段：编译这个函数定义，生成RuntimeFunc，注册到全局符号 */
          RuntimeFunc* rf = compile_func_from_ast($$);
          Value func_val;
          func_val.type = VAL_FUNC;
          func_val.v.func.func_obj = rf;
          sym_set($3, func_val); /* 注册到运行时符号表，后续调用可以查到 */
        }
        /* 泛型函数：func<T> name(params) { body } */
        | FUNC generic_param_list ID LPAREN param_list RPAREN block_stmt {
          $$ = ast_func_def($3, $5, $7);
          $$->u.func_def.annotations = NULL;
          $$->u.func_def.generic_params = $2;
          RuntimeFunc* rf = compile_func_from_ast($$);
          Value func_val;
          func_val.type = VAL_FUNC;
          func_val.v.func.func_obj = rf;
          sym_set($3, func_val);
        } ;

/* 泛型参数列表：<T> / <T, U>（用 param.next 链接，与函数参数一致） */
generic_param_list : LT generic_param_items GT { $$ = $2; }
                   ;

opt_generic_param_list : %empty { $$ = NULL; }
                       | generic_param_list { $$ = $1; }
                       ;

generic_param_items : ID { $$ = ast_param($1, 0, NULL); }
                    | ID COLON ID { $$ = ast_param_constraint($1, $3); }
                    | generic_param_items COMMA ID { $$ = ast_param_append($1, ast_param($3, 0, NULL)); }
                    | generic_param_items COMMA ID COLON ID { $$ = ast_param_append($1, ast_param_constraint($3, $5)); }
                    ;

/* 宏定义：macro name(params) { body } */
macro_def : MACRO ID LPAREN param_list RPAREN block_stmt {
          $$ = ast_macro_def($2, $4, $6);
          /* 注册到宏表，供后续宏展开使用 */
          macro_register($2, $4, $6);
        } ;

/* 参数列表：支持 a,b,...rest；可变参数只能放在最后一个 */
param_list
    : %empty                 { $$ = NULL; }
    | param                  { $$ = $1; }
    | param_list COMMA param { $$ = ast_param_append($1, $3); }
;

param
    : ID                     { $$ = ast_param($1, 0, NULL); } /*普通参数 is_ellipsis=0 */
    | ID ASSIGN expr         { $$ = ast_param($1, 0, $3); } /*带默认值的参数 */
    | ELLIPSIS ID            { $$ = ast_param($2, 1, NULL); } /* ...args 可变参数 is_ellipsis=1 */
;

/* 注解：@name 或 @name(args) */
annotation
    : TOK_AT ID                        { $$ = ast_annotation($2, NULL); }
    | TOK_AT ID LPAREN arg_list RPAREN { $$ = ast_annotation($2, $4); }
;

/* 注解列表：一个或多个注解 */
annotation_list
    : annotation                 { $$ = $1; }
    | annotation_list annotation { $$ = ast_seq_append($1, $2); }
;

/* 调用实参列表 */
arg_list
    : %empty               { $$ = NULL; }
    | arg                  { $$ = $1; }
    | arg_list COMMA arg   { $$ = ast_arg_append($1, $3); }
;

arg
    : expr                 { $$ = $1; }
    | ELLIPSIS unary_expr  { $$ = ast_spread($2); }
;

/* 解构赋值左边：a,b,c 标识符列表（AST_SEQ 链的 AST_VAR） */
destruct_lhs
    : ID COMMA ID                  { $$ = ast_seq(ast_var($1), ast_seq(ast_var($3), NULL)); }
    | destruct_lhs COMMA ID        { $$ = ast_seq_append($1, ast_var($3)); }
;


open_stmt
    : IF LPAREN expr RPAREN closed_stmt elif_clause_list else_part         { $$ = ast_if_chain($3, $5, $6, $7); }
    ;

/* try { body } catch (e) { handler } [finally { body }]；catch/finally 至少其一 */
try_stmt
    : TRY block_stmt catch_clause_list {
          if(is_single_untagged_catch($3)) {
              char* var_name = NULL;
              AstNode* catch_body = NULL;
              extract_single_catch($3, &var_name, &catch_body);
              $$ = ast_try($2, var_name, catch_body, NULL);
          } else {
              $$ = build_try_multi($2, $3, NULL);
          }
      }
    | TRY block_stmt catch_clause_list FINALLY block_stmt {
          if(is_single_untagged_catch($3)) {
              char* var_name = NULL;
              AstNode* catch_body = NULL;
              extract_single_catch($3, &var_name, &catch_body);
              $$ = ast_try($2, var_name, catch_body, $5);
          } else {
              $$ = build_try_multi($2, $3, $5);
          }
      }
    | TRY block_stmt FINALLY block_stmt {
          $$ = ast_try($2, NULL, NULL, $4);
      }
    ;

/* 多个 catch 子句列表（用 AST_SEQ 链接） */
catch_clause_list
    : catch_clause {
          $$ = $1;
      }
    | catch_clause_list catch_clause {
          $$ = ast_seq($1, $2);
      }
    ;

/* 单个 catch 子句：catch (e) 或 catch (Type e)
   使用 opt_catch_type 避免移进/归约冲突 */
catch_clause
    : CATCH LPAREN ID opt_catch_type RPAREN block_stmt {
          if($4) {
              $$ = make_catch_clause(strdup($3), strdup($4), $6);
          } else {
              $$ = make_catch_clause(NULL, strdup($3), $6);
          }
      }
    ;

/* 可选的 catch 类型名：有类型或空 */
opt_catch_type
    : ID { $$ = $1; }
    | %empty { $$ = NULL; }
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
    : CASE const_expr CASE_COLON stmt_list {
        $$ = ast_case($2, $4, 0);
    }
    | CASE type_keyword CASE_COLON stmt_list {
        $$ = ast_case_type($2, $4);
    }
    | CASE ID CASE_COLON stmt_list {
        $$ = ast_case_guard($2, NULL, $4);
    }
    | CASE ID IF expr CASE_COLON stmt_list {
        $$ = ast_case_guard($2, $4, $6);
    }
    | DEFAULT CASE_COLON stmt_list {
        $$ = ast_case(NULL, $3, 1);
    }
    ;

/* type keyword for pattern matching: case int: / case string: etc */
type_keyword
    : TOK_INT      { $$ = VAL_INT; }
    | TOK_DOUBLE   { $$ = VAL_DOUBLE; }
    | TOK_STRING   { $$ = VAL_STRING; }
    | TOK_BOOL     { $$ = VAL_BOOL; }
    | TOK_CHAR     { $$ = VAL_CHAR; }
    ;

/* case后面只能是编译期常量：数字、整数、char字面量、字符串字面量 */
const_expr
    : NUMBER                  { $$ = ast_num($1); }
    | INTEGER                 { $$ = ast_int($1); }
    | char_lit                { $$ = ast_new_char($1); }
    | STRING_LIT              { $$ = ast_string($1); free($1); }
    | TRUE                    { $$ = ast_bool(1); }
    | FALSE                   { $$ = ast_bool(0); }
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
    | NULL_LIT                { $$ = ast_none(); }
    | STRING_LIT              { $$ = ast_string($1); free($1); }
    | FSTRING_LIT             { $$ = L(maybe_template($1)); free($1); }
    | char_lit                { $$ = ast_new_char($1); }
    | ID                      { $$ = L(ast_var($1)); }
    | ID LPAREN arg_list RPAREN {
          /* 宏调用：如果是已注册的宏，则展开；否则作为普通函数调用 */
          if(macro_is_defined($1)) {
              AstNode* mdef = macro_lookup($1);
              $$ = L(macro_expand(mdef, $3));
          } else {
              $$ = L(ast_call($1, $3));
          }
      }  /* 函数调用 foo(a,b,c) 或宏调用 */
    | ARRAY_OPEN arg_list RBRACKET { $$ = ast_array_lit($2); }  /* 数组字面量 [1,2,3] / []（lexer 按上下文消歧） */
    | MAP_OPEN map_items RBRACE   { $$ = ast_map_lit($2); }    /* 字典字面量 {"k": v, name: 1} / {}（lexer 上下文消歧：表达式位置） */
    | LPAREN expr RPAREN      { $$ = $2; }
    /* 强转 (int)x 接 postfix_expr：C 语义，(int)a[0] = (int)(a[0])（cast 作用于整个后缀表达式） */
    | LPAREN TOK_INT RPAREN postfix_expr   { $$ = new_cast_node(CAST_INT, $4); }
    | LPAREN TOK_DOUBLE RPAREN postfix_expr { $$ = new_cast_node(CAST_DOUBLE, $4); }
    | LPAREN TOK_STRING RPAREN postfix_expr { $$ = new_cast_node(CAST_STRING, $4); }
    | LPAREN TOK_BOOL RPAREN postfix_expr   { $$ = new_cast_node(CAST_BOOL, $4); }
    | LPAREN TOK_ASCII RPAREN postfix_expr  { $$ = new_cast_node(CAST_ASCII, $4); }
    | LPAREN TOK_CHAR RPAREN postfix_expr   { $$ = new_cast_node(CAST_CHAR, $4); }
    | LPAREN TOK_BYTE RPAREN postfix_expr   { $$ = new_cast_node(CAST_BYTE, $4); }
    | LPAREN TOK_INT8 RPAREN postfix_expr   { $$ = new_cast_node(CAST_INT8, $4); }
    | LPAREN TOK_INT16 RPAREN postfix_expr  { $$ = new_cast_node(CAST_INT16, $4); }
    | LPAREN TOK_INT32 RPAREN postfix_expr  { $$ = new_cast_node(CAST_INT32, $4); }
    | LPAREN TOK_INT64 RPAREN postfix_expr  { $$ = new_cast_node(CAST_INT64, $4); }
    | LPAREN TOK_UINT8 RPAREN postfix_expr  { $$ = new_cast_node(CAST_UINT8, $4); }
    | LPAREN TOK_UINT16 RPAREN postfix_expr { $$ = new_cast_node(CAST_UINT16, $4); }
    | LPAREN TOK_UINT32 RPAREN postfix_expr { $$ = new_cast_node(CAST_UINT32, $4); }
    | LPAREN TOK_UINT64 RPAREN postfix_expr { $$ = new_cast_node(CAST_UINT64, $4); }
    | LPAREN TOK_UINT RPAREN postfix_expr   { $$ = new_cast_node(CAST_UINT64, $4); }
    | LPAREN TOK_LONG RPAREN postfix_expr   { $$ = new_cast_node(CAST_LONG, $4); }
    | LPAREN TOK_LONGLONG RPAREN postfix_expr { $$ = new_cast_node(CAST_LONGLONG, $4); }
    | LPAREN TOK_FLOAT RPAREN postfix_expr    { $$ = new_cast_node(CAST_FLOAT, $4); }
    /* 泛型容器字面量：(byte)[1,2,3] 逐元素强转 / (byte){"a":1} 逐值强转
       （lexer 上下文消歧后 cast 后接 LBRACKET/LBRACE，按容器字面量解释） */
    | LPAREN TOK_INT RPAREN LBRACKET arg_list RBRACKET
        { $$ = new_cast_node(CAST_INT, ast_array_lit($5)); }
    | LPAREN TOK_INT RPAREN LBRACE map_items RBRACE
        { $$ = new_cast_node(CAST_INT, ast_map_lit($5)); }
    | LPAREN TOK_DOUBLE RPAREN LBRACKET arg_list RBRACKET
        { $$ = new_cast_node(CAST_DOUBLE, ast_array_lit($5)); }
    | LPAREN TOK_DOUBLE RPAREN LBRACE map_items RBRACE
        { $$ = new_cast_node(CAST_DOUBLE, ast_map_lit($5)); }
    | LPAREN TOK_STRING RPAREN LBRACKET arg_list RBRACKET
        { $$ = new_cast_node(CAST_STRING, ast_array_lit($5)); }
    | LPAREN TOK_STRING RPAREN LBRACE map_items RBRACE
        { $$ = new_cast_node(CAST_STRING, ast_map_lit($5)); }
    | LPAREN TOK_BOOL RPAREN LBRACKET arg_list RBRACKET
        { $$ = new_cast_node(CAST_BOOL, ast_array_lit($5)); }
    | LPAREN TOK_BOOL RPAREN LBRACE map_items RBRACE
        { $$ = new_cast_node(CAST_BOOL, ast_map_lit($5)); }
    | LPAREN TOK_ASCII RPAREN LBRACKET arg_list RBRACKET
        { $$ = new_cast_node(CAST_ASCII, ast_array_lit($5)); }
    | LPAREN TOK_ASCII RPAREN LBRACE map_items RBRACE
        { $$ = new_cast_node(CAST_ASCII, ast_map_lit($5)); }
    | LPAREN TOK_CHAR RPAREN LBRACKET arg_list RBRACKET
        { $$ = new_cast_node(CAST_CHAR, ast_array_lit($5)); }
    | LPAREN TOK_CHAR RPAREN LBRACE map_items RBRACE
        { $$ = new_cast_node(CAST_CHAR, ast_map_lit($5)); }
    | LPAREN TOK_BYTE RPAREN LBRACKET arg_list RBRACKET
        { $$ = new_cast_node(CAST_BYTE, ast_array_lit($5)); }
    | LPAREN TOK_BYTE RPAREN LBRACE map_items RBRACE
        { $$ = new_cast_node(CAST_BYTE, ast_map_lit($5)); }
    /* 泛型字面量：<T>value 单值/数组/map 统一走 unary_expr（强转函数已支持数组/map 递归）
       <string,V>{k:v} map 值强转（键固定 string） */
    | LT builtin_type_name GT unary_expr
        { $$ = new_cast_node($2, $4); }
    | LT type_name COMMA type_name GT MAP_OPEN map_items RBRACE
        { $$ = new_cast_node($4, ast_map_lit($7)); }
    | LT ID GT ARRAY_OPEN arg_list RBRACKET {
          /* 泛型自定义类型：<Person>[e1,e2] → [Person(e1), Person(e2)]（形状构造） */
          if(type_lookup($2) >= 0) {
              $$ = L(ast_array_lit(wrap_type_list($2, $5)));
              free($2);
          } else {
              yyerror("未定义类型");
          }
      }
    | TOK_TYPE LPAREN expr RPAREN {
          /* type 关键字兼作内置函数：type(x) → 类型名字符串 */
          $$ = L(ast_call(strdup("type"), $3));
      }
    | FUNC LPAREN param_list RPAREN block_stmt {
          /* 匿名函数表达式：生成内部名 _lambda_N，与具名同路注册（VM sym + IR 函数表） */
          char nm[64];
          snprintf(nm, sizeof nm, "_lambda_%d", g_lambda_seq++);
          $$ = L(ast_func_def(nm, $3, $5));
          RuntimeFunc* rf = compile_func_from_ast($$);
          Value func_val;
          func_val.type = VAL_FUNC;
          func_val.v.func.func_obj = rf;
          sym_set(nm, func_val);
      }
    | READ STRING_LIT {
          /* read "path" → 文件内容；普通路径不内插；结果可后续缀（.len() 等） */
          AstNode* p = ast_string($2);
          free($2);
          $$ = L(ast_call(strdup("read_file"), p));
      }
    | READ FSTRING_LIT {
          /* read f"path" → 文件内容；f 前缀路径支持模板内插；结果可后续缀 */
          AstNode* p = L(maybe_template($2));
          free($2);
          $$ = L(ast_call(strdup("read_file"), p));
      }
    ;

postfix_expr
    : primary
    | postfix_expr LBRACKET expr RBRACKET  { $$ = L(ast_index($1, $3)); }  /* 数组下标 a[i] */
    | postfix_expr PLUSPLUS   { $$ = ast_unary(OP_POST_INC, $1); }
    | postfix_expr MINUSMINUS { $$ = ast_unary(OP_POST_DEC, $1); }
    /* 调用链 f(1)(2)：callee 为表达式（函数值），动态调用 */
    | postfix_expr LPAREN arg_list RPAREN { $$ = L(ast_dyn_call($1, $3)); }
    /* 方法链 a.b(x,y) → b(a,x,y)（语法糖，接收者作为首参） */
    | postfix_expr DOT ID LPAREN arg_list RPAREN {
          AstNode* recv = $1;
          AstNode* margs = $5;
          /* requests.get/post/put/delete/head/patch：内置 HTTP 命名空间，
             接收者 requests 不进参数（url 是第一个实参） */
          if(recv->type == AST_VAR && strcmp(recv->u.varname, "requests") == 0 &&
             (strcmp($3, "get") == 0 || strcmp($3, "post") == 0 || strcmp($3, "put") == 0 ||
              strcmp($3, "delete") == 0 || strcmp($3, "head") == 0 || strcmp($3, "patch") == 0)) {
              $$ = L(ast_call($3, margs));
          } else if(strcmp($3, "get") == 0) {
              /* arr.get(i)：数组/容器安全取（内部名 arr_get，与 requests.get 区分） */
              $$ = L(ast_call("arr_get", margs ? ast_seq_front(margs, recv) : recv));
          } else if(recv->type == AST_VAR && lm_is_module_alias(recv->u.varname)) {
              /* 模块命名空间 m.add(1,2) -> m["add"](1,2)：map 取值后动态调用，不把接收者当前参 */
              AstNode* fn = L(ast_index(recv, ast_string(strdup($3))));
              free($3);
              $$ = L(ast_dyn_call(fn, margs));
          } else {
              $$ = L(ast_call($3, margs ? ast_seq_front(margs, recv) : recv));
          }
      }
    /* 属性访问 a.b → a["b"]（map 点属性；无参方法链语法不再保留） */
    | postfix_expr DOT ID { $$ = L(ast_index($1, ast_string($3))); }
    /* 安全方法调用 a?.b(x,y)：a 为 null 时返回 null */
    | postfix_expr SAFE_CALL ID LPAREN arg_list RPAREN {
          $$ = L(ast_safe_call($1, $3, $5));
      }
    /* 安全属性访问 a?.b → a 为 null 时返回 null */
    | postfix_expr SAFE_CALL ID { $$ = L(ast_safe_call($1, $3, NULL)); }
    ;

/* 字典字面量 {"k": v, name: 1, ...}；键为字符串字面量（支持模板）或标识符 */
map_items
    : %empty          { $$ = NULL; }
    | map_item        { $$ = $1; }
    | map_items COMMA map_item { $$ = ast_seq($1, $3); }
    ;

map_item
    : STRING_LIT COLON expr {
          AstNode* k = ast_string($1);
          free($1);
          $$ = ast_map_entry(k, $3);
      }
    | FSTRING_LIT COLON expr {
          AstNode* k = L(maybe_template($1));
          free($1);
          $$ = ast_map_entry(k, $3);
      }
    | ID COLON expr {
          $$ = ast_map_entry(ast_string(strdup($1)), $3);
      }
    | const_expr COLON expr {
          $$ = ast_map_entry($1, $3);
      }
    | ARRAY_OPEN expr RBRACKET COLON expr {
          $$ = ast_map_entry($2, $5);
      }
    | ELLIPSIS unary_expr {
          $$ = ast_spread($2);
      }
    ;

/* type 声明属性清单 */
type_prop_list
    : %empty                     { $$ = NULL; }
    | type_prop                  { $$ = $1; }
    | type_prop_list COMMA type_prop { $$ = ast_seq($1, $3); }
    ;
type_prop
    : ID COLON type_name         { type_prop_push($1, $3); $$ = ast_none(); }
    ;
builtin_type_name
    : TOK_STRING                 { $$ = CAST_STRING; }
    | TOK_INT                    { $$ = CAST_INT; }
    | TOK_DOUBLE                 { $$ = CAST_DOUBLE; }
    | TOK_BOOL                   { $$ = CAST_BOOL; }
    | TOK_CHAR                   { $$ = CAST_CHAR; }
    | TOK_ASCII                  { $$ = CAST_ASCII; }
    | TOK_BYTE                   { $$ = CAST_BYTE; }
    | TOK_INT8                   { $$ = CAST_INT8; }
    | TOK_INT16                  { $$ = CAST_INT16; }
    | TOK_INT32                  { $$ = CAST_INT32; }
    | TOK_INT64                  { $$ = CAST_INT64; }
    | TOK_UINT8                  { $$ = CAST_UINT8; }
    | TOK_UINT16                 { $$ = CAST_UINT16; }
    | TOK_UINT32                 { $$ = CAST_UINT32; }
    | TOK_UINT64                 { $$ = CAST_UINT64; }
    | TOK_UINT                   { $$ = CAST_UINT64; }
    | TOK_LONG                   { $$ = CAST_LONG; }
    | TOK_LONGLONG               { $$ = CAST_LONGLONG; }
    | TOK_FLOAT                  { $$ = CAST_FLOAT; }
    ;
type_name
    : builtin_type_name          { $$ = castkind_to_valtype($1); }
    | ID                         { $$ = type_name_to_valtype($1); }
    ;

/* 枚举成员：值 = 成员名字符串 */
enum_members
    : %empty                     { $$ = NULL; }
    | enum_member                { $$ = $1; }
    | enum_members COMMA enum_member { $$ = ast_seq($1, $3); }
    ;
enum_member
    : ID                         { $$ = ast_map_entry(ast_string(strdup($1)), ast_string(strdup($1))); free($1); }
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
    | logic_or_expr NULL_COALESCE logic_and_expr { $$ = ast_null_coalesce($1, $3); }
    ;

ternary_expr
    : logic_or_expr
    | logic_or_expr QMARK expr COLON ternary_expr  { $$ = ast_ternary($1, $3, $5); }
    ;

assignment_expr
    : ternary_expr
    | ID ASSIGN assignment_expr  { $$ = ast_assign($1, $3); }
    /* destruct_lhs 移到 closed_stmt 层面，避免与函数参数列表的 COMMA 冲突 */
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
    /* 属性赋值 m.key = v → m["key"] = v */
    | postfix_expr DOT ID ASSIGN assignment_expr
        { $$ = ast_index_assign($1, ast_string($3), $5); }
    ;

expr
    : assignment_expr
    ;

%%

void yyerror(const char* s){
    fprintf(stderr,"语法错误(第%d行): %s\n", yylineno, s);
}
