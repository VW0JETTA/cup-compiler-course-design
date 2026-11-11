%{
#include <stdio.h>
#include <stdlib.h>
#include "tree.h"

extern int  yylex(void);
extern int  yylineno;
extern char* yytext;
extern int  hasError;        /* 全局错误标志（main.c 定义） */
extern int  lexFatal;        /* 致命词法错误(未闭合注释)标志（lexical.l 维护） */

Node* root = NULL;           /* 语法树根节点 */

void yyerror(const char* msg);
%}

/* 语法树头文件需出现在生成的 .tab.h 中，供 Flex 使用 */
%code requires {
    #include "tree.h"
}

%locations                   /* 启用 @$ / @n 位置追踪（yylloc） */
%expect 2                    /* 预期 2 个良性移进/归约冲突（来自 error 恢复产生式） */

%union {
    Node* node;
}

/* ---------------- 终结符（词法单元） ---------------- */
%token <node> INT FLOAT ID TYPE
%token <node> SEMI COMMA ASSIGNOP RELOP PLUS MINUS STAR DIV
%token <node> AND OR DOT NOT LP RP LB RB LC RC
%token <node> STRUCT RETURN IF ELSE WHILE

/* ---------------- 非终结符（语法单元） ---------------- */
%type <node> Program ExtDefList ExtDef ExtDecList
%type <node> Specifier StructSpecifier OptTag Tag
%type <node> VarDec FunDec VarList ParamDec
%type <node> CompSt StmtList Stmt
%type <node> DefList Def DecList Dec
%type <node> Exp Args

/* ---------------- 优先级与结合性（消除二义性） ---------------- */
%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE

%right ASSIGNOP
%left  OR
%left  AND
%left  RELOP
%left  PLUS MINUS
%left  STAR DIV
%right NOT NEG
%left  LP RP LB RB DOT

%start Program

%%

/* ==================== 高层定义 ==================== */
Program
    : ExtDefList                        { root = newNonterm("Program", 1, $1); $$ = root; }
    ;

ExtDefList
    : ExtDef ExtDefList                 { $$ = newNonterm("ExtDefList", 2, $1, $2); }
    | /* empty */                       { $$ = NULL; }
    ;

ExtDef
    : Specifier ExtDecList SEMI         { $$ = newNonterm("ExtDef", 3, $1, $2, $3); }
    | Specifier SEMI                    { $$ = newNonterm("ExtDef", 2, $1, $2); }
    | Specifier FunDec CompSt           { $$ = newNonterm("ExtDef", 3, $1, $2, $3); }
    | error SEMI                        { $$ = NULL; }
    ;

ExtDecList
    : VarDec                            { $$ = newNonterm("ExtDecList", 1, $1); }
    | VarDec COMMA ExtDecList           { $$ = newNonterm("ExtDecList", 3, $1, $2, $3); }
    ;

/* ==================== 类型描述符 ==================== */
Specifier
    : TYPE                              { $$ = newNonterm("Specifier", 1, $1); }
    | StructSpecifier                   { $$ = newNonterm("Specifier", 1, $1); }
    ;

StructSpecifier
    : STRUCT OptTag LC DefList RC       { $$ = newNonterm("StructSpecifier", 5, $1, $2, $3, $4, $5); }
    | STRUCT Tag                        { $$ = newNonterm("StructSpecifier", 2, $1, $2); }
    ;

OptTag
    : ID                                { $$ = newNonterm("OptTag", 1, $1); }
    | /* empty */                       { $$ = NULL; }
    ;

Tag
    : ID                                { $$ = newNonterm("Tag", 1, $1); }
    ;

/* ==================== 声明 ==================== */
VarDec
    : ID                                { $$ = newNonterm("VarDec", 1, $1); }
    | VarDec LB INT RB                  { $$ = newNonterm("VarDec", 4, $1, $2, $3, $4); }
    ;

FunDec
    : ID LP VarList RP                  { $$ = newNonterm("FunDec", 4, $1, $2, $3, $4); }
    | ID LP RP                          { $$ = newNonterm("FunDec", 3, $1, $2, $3); }
    ;

VarList
    : ParamDec COMMA VarList            { $$ = newNonterm("VarList", 3, $1, $2, $3); }
    | ParamDec                          { $$ = newNonterm("VarList", 1, $1); }
    ;

ParamDec
    : Specifier VarDec                  { $$ = newNonterm("ParamDec", 2, $1, $2); }
    ;

/* ==================== 语句 ==================== */
CompSt
    : LC DefList StmtList RC            { $$ = newNonterm("CompSt", 4, $1, $2, $3, $4); }
    ;

StmtList
    : Stmt StmtList                     { $$ = newNonterm("StmtList", 2, $1, $2); }
    | /* empty */                       { $$ = NULL; }
    ;

Stmt
    : Exp SEMI                          { $$ = newNonterm("Stmt", 2, $1, $2); }
    | CompSt                            { $$ = newNonterm("Stmt", 1, $1); }
    | RETURN Exp SEMI                   { $$ = newNonterm("Stmt", 3, $1, $2, $3); }
    | IF LP Exp RP Stmt %prec LOWER_THAN_ELSE
                                        { $$ = newNonterm("Stmt", 5, $1, $2, $3, $4, $5); }
    | IF LP Exp RP Stmt ELSE Stmt       { $$ = newNonterm("Stmt", 7, $1, $2, $3, $4, $5, $6, $7); }
    | WHILE LP Exp RP Stmt              { $$ = newNonterm("Stmt", 5, $1, $2, $3, $4, $5); }
    | error SEMI                        { $$ = NULL; }
    ;

/* ==================== 局部定义 ==================== */
DefList
    : Def DefList                       { $$ = newNonterm("DefList", 2, $1, $2); }
    | /* empty */                       { $$ = NULL; }
    ;

Def
    : Specifier DecList SEMI            { $$ = newNonterm("Def", 3, $1, $2, $3); }
    | error SEMI                        { $$ = NULL; }
    ;

DecList
    : Dec                               { $$ = newNonterm("DecList", 1, $1); }
    | Dec COMMA DecList                 { $$ = newNonterm("DecList", 3, $1, $2, $3); }
    ;

Dec
    : VarDec                            { $$ = newNonterm("Dec", 1, $1); }
    | VarDec ASSIGNOP Exp               { $$ = newNonterm("Dec", 3, $1, $2, $3); }
    ;

/* ==================== 表达式 ==================== */
Exp
    : Exp ASSIGNOP Exp                  { $$ = newNonterm("Exp", 3, $1, $2, $3); }
    | Exp AND Exp                       { $$ = newNonterm("Exp", 3, $1, $2, $3); }
    | Exp OR Exp                        { $$ = newNonterm("Exp", 3, $1, $2, $3); }
    | Exp RELOP Exp                     { $$ = newNonterm("Exp", 3, $1, $2, $3); }
    | Exp PLUS Exp                      { $$ = newNonterm("Exp", 3, $1, $2, $3); }
    | Exp MINUS Exp                     { $$ = newNonterm("Exp", 3, $1, $2, $3); }
    | Exp STAR Exp                      { $$ = newNonterm("Exp", 3, $1, $2, $3); }
    | Exp DIV Exp                       { $$ = newNonterm("Exp", 3, $1, $2, $3); }
    | LP Exp RP                         { $$ = newNonterm("Exp", 3, $1, $2, $3); }
    | MINUS Exp %prec NEG               { $$ = newNonterm("Exp", 2, $1, $2); }
    | NOT Exp                           { $$ = newNonterm("Exp", 2, $1, $2); }
    | ID LP Args RP                     { $$ = newNonterm("Exp", 4, $1, $2, $3, $4); }
    | ID LP RP                          { $$ = newNonterm("Exp", 3, $1, $2, $3); }
    | Exp LB Exp RB                     { $$ = newNonterm("Exp", 4, $1, $2, $3, $4); }
    | Exp DOT ID                        { $$ = newNonterm("Exp", 3, $1, $2, $3); }
    | ID                                { $$ = newNonterm("Exp", 1, $1); }
    | INT                               { $$ = newNonterm("Exp", 1, $1); }
    | FLOAT                             { $$ = newNonterm("Exp", 1, $1); }
    ;

Args
    : Exp COMMA Args                    { $$ = newNonterm("Args", 3, $1, $2, $3); }
    | Exp                               { $$ = newNonterm("Args", 1, $1); }
    ;

%%

void yyerror(const char* msg) {
    static int lastLine = -1;
    hasError = 1;
    if (lexFatal) return;               /* 未闭合注释等致命词法错误后，不再报其引发的连带语法错误 */
    int line = yylloc.first_line;       /* 标准 Bison 行为：报前看(出错)记号所在行 */
    if (line == lastLine) return;       /* 同一行只报告一次 */
    lastLine = line;
    printf("Error type B at Line %d: %s.\n", line, msg);
}
