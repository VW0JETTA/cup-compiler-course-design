%{
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>

int yylex(void);
void yyerror(const char *s);
extern int yylineno;

Node *ast_root = NULL;
%}

%union {
    Node *node;
}

%token <node> INT FLOAT ID
%token <node> SEMI COMMA ASSIGNOP RELOP PLUS MINUS STAR DIV AND OR DOT NOT
%token <node> TYPE LP RP LB RB LC RC STRUCT RETURN IF ELSE WHILE

%type <node> Program ExtDefList ExtDef ExtDecList
%type <node> Specifier StructSpecifier OptTag Tag
%type <node> VarDec FunDec VarList ParamDec
%type <node> CompSt StmtList Stmt
%type <node> DefList Def DecList Dec
%type <node> Exp Args

%right ASSIGNOP
%left OR
%left AND
%left RELOP
%left PLUS MINUS
%left STAR DIV
%right NOT UMINUS
%left LP RP LB RB DOT

%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE

%%

Program     : ExtDefList                        { $$ = new_node("Program", 0, 1, $1); ast_root = $$; }
            ;
ExtDefList  : ExtDef ExtDefList                 { $$ = new_node("ExtDefList", 0, 2, $1, $2); }
            | /* empty */                       { $$ = new_node("ExtDefList", 0, 0); }
            ;
ExtDef      : Specifier ExtDecList SEMI         { $$ = new_node("ExtDef", 0, 3, $1, $2, $3); }
            | Specifier SEMI                    { $$ = new_node("ExtDef", 0, 2, $1, $2); }
            | Specifier FunDec CompSt           { $$ = new_node("ExtDef", 0, 3, $1, $2, $3); }
            ;
ExtDecList  : VarDec                            { $$ = new_node("ExtDecList", 0, 1, $1); }
            | VarDec COMMA ExtDecList           { $$ = new_node("ExtDecList", 0, 3, $1, $2, $3); }
            ;

Specifier   : TYPE                              { $$ = new_node("Specifier", 0, 1, $1); }
            | StructSpecifier                   { $$ = new_node("Specifier", 0, 1, $1); }
            ;
StructSpecifier : STRUCT OptTag LC DefList RC   { $$ = new_node("StructSpecifier", 0, 5, $1, $2, $3, $4, $5); }
            | STRUCT Tag                        { $$ = new_node("StructSpecifier", 0, 2, $1, $2); }
            ;
OptTag      : ID                                { $$ = new_node("OptTag", 0, 1, $1); }
            | /* empty */                       { $$ = new_node("OptTag", 0, 0); }
            ;
Tag         : ID                                { $$ = new_node("Tag", 0, 1, $1); }
            ;

VarDec      : ID                                { $$ = new_node("VarDec", 0, 1, $1); }
            | VarDec LB INT RB                  { $$ = new_node("VarDec", 0, 4, $1, $2, $3, $4); }
            ;
FunDec      : ID LP VarList RP                  { $$ = new_node("FunDec", 0, 4, $1, $2, $3, $4); }
            | ID LP RP                          { $$ = new_node("FunDec", 0, 3, $1, $2, $3); }
            ;
VarList     : ParamDec COMMA VarList            { $$ = new_node("VarList", 0, 3, $1, $2, $3); }
            | ParamDec                          { $$ = new_node("VarList", 0, 1, $1); }
            ;
ParamDec    : Specifier VarDec                  { $$ = new_node("ParamDec", 0, 2, $1, $2); }
            ;

CompSt      : LC DefList StmtList RC            { $$ = new_node("CompSt", 0, 4, $1, $2, $3, $4); }
            ;
StmtList    : Stmt StmtList                     { $$ = new_node("StmtList", 0, 2, $1, $2); }
            | /* empty */                       { $$ = new_node("StmtList", 0, 0); }
            ;
Stmt        : Exp SEMI                          { $$ = new_node("Stmt", 0, 2, $1, $2); }
            | CompSt                            { $$ = new_node("Stmt", 0, 1, $1); }
            | RETURN Exp SEMI                   { $$ = new_node("Stmt", 0, 3, $1, $2, $3); }
            | IF LP Exp RP Stmt %prec LOWER_THAN_ELSE
                                                { $$ = new_node("Stmt", 0, 5, $1, $2, $3, $4, $5); }
            | IF LP Exp RP Stmt ELSE Stmt       { $$ = new_node("Stmt", 0, 7, $1, $2, $3, $4, $5, $6, $7); }
            | WHILE LP Exp RP Stmt              { $$ = new_node("Stmt", 0, 5, $1, $2, $3, $4, $5); }
            ;

DefList     : Def DefList                       { $$ = new_node("DefList", 0, 2, $1, $2); }
            | /* empty */                       { $$ = new_node("DefList", 0, 0); }
            ;
Def         : Specifier DecList SEMI            { $$ = new_node("Def", 0, 3, $1, $2, $3); }
            ;
DecList     : Dec                               { $$ = new_node("DecList", 0, 1, $1); }
            | Dec COMMA DecList                 { $$ = new_node("DecList", 0, 3, $1, $2, $3); }
            ;
Dec         : VarDec                            { $$ = new_node("Dec", 0, 1, $1); }
            | VarDec ASSIGNOP Exp               { $$ = new_node("Dec", 0, 3, $1, $2, $3); }
            ;

Exp         : Exp ASSIGNOP Exp                  { $$ = new_node("Exp", 0, 3, $1, $2, $3); }
            | Exp AND Exp                       { $$ = new_node("Exp", 0, 3, $1, $2, $3); }
            | Exp OR Exp                        { $$ = new_node("Exp", 0, 3, $1, $2, $3); }
            | Exp RELOP Exp                     { $$ = new_node("Exp", 0, 3, $1, $2, $3); }
            | Exp PLUS Exp                      { $$ = new_node("Exp", 0, 3, $1, $2, $3); }
            | Exp MINUS Exp                     { $$ = new_node("Exp", 0, 3, $1, $2, $3); }
            | Exp STAR Exp                      { $$ = new_node("Exp", 0, 3, $1, $2, $3); }
            | Exp DIV Exp                       { $$ = new_node("Exp", 0, 3, $1, $2, $3); }
            | LP Exp RP                         { $$ = new_node("Exp", 0, 3, $1, $2, $3); }
            | MINUS Exp %prec UMINUS            { $$ = new_node("Exp", 0, 2, $1, $2); }
            | NOT Exp                           { $$ = new_node("Exp", 0, 2, $1, $2); }
            | ID LP Args RP                     { $$ = new_node("Exp", 0, 4, $1, $2, $3, $4); }
            | ID LP RP                          { $$ = new_node("Exp", 0, 3, $1, $2, $3); }
            | Exp LB Exp RB                     { $$ = new_node("Exp", 0, 4, $1, $2, $3, $4); }
            | Exp DOT ID                        { $$ = new_node("Exp", 0, 3, $1, $2, $3); }
            | ID                                { $$ = new_node("Exp", 0, 1, $1); }
            | INT                               { $$ = new_node("Exp", 0, 1, $1); }
            | FLOAT                             { $$ = new_node("Exp", 0, 1, $1); }
            ;
Args        : Exp COMMA Args                    { $$ = new_node("Args", 0, 3, $1, $2, $3); }
            | Exp                               { $$ = new_node("Args", 0, 1, $1); }
            ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Syntax error near line %d: %s\n", yylineno, s);
}
