%{
#include "ast.h"
#include "syntax.tab.h"
#include <stdlib.h>
#include <string.h>

int yycolumn = 1;
#define MK(name) (yylval.node = new_token(name, yytext, yylineno, 0))
#define MKINT(name, v) (yylval.node = new_token(name, yytext, yylineno, v))
%}

%option yylineno
%option noyywrap

digit       [0-9]
letter      [a-zA-Z]
ID          ({letter}|_)({letter}|{digit}|_)*
INT         0|[1-9]{digit}*
FLOAT       {digit}+\.{digit}+

%%

"//".*                  { /* single line comment */ }
"/*"([^*]|\*+[^*/])*\*+\/   { /* multi line comment */ }

[ \t\r\n]+              { /* whitespace */ }

"int"|"float"           { MK("TYPE");   return TYPE; }
"struct"                { MK("STRUCT"); return STRUCT; }
"return"                { MK("RETURN"); return RETURN; }
"if"                    { MK("IF");     return IF; }
"else"                  { MK("ELSE");   return ELSE; }
"while"                 { MK("WHILE");  return WHILE; }

{INT}                   { MKINT("INT", atoi(yytext)); return INT; }
{FLOAT}                 { MK("FLOAT");  return FLOAT; }
{ID}                    { MK("ID");     return ID; }

";"                     { MK("SEMI");     return SEMI; }
","                     { MK("COMMA");    return COMMA; }
">="|"<="|"=="|"!="|">"|"<"   { MK("RELOP"); return RELOP; }
"="                     { MK("ASSIGNOP"); return ASSIGNOP; }
"+"                     { MK("PLUS");     return PLUS; }
"-"                     { MK("MINUS");    return MINUS; }
"*"                     { MK("STAR");     return STAR; }
"/"                     { MK("DIV");      return DIV; }
"&&"                    { MK("AND");      return AND; }
"||"                    { MK("OR");       return OR; }
"."                     { MK("DOT");      return DOT; }
"!"                     { MK("NOT");      return NOT; }
"("                     { MK("LP");       return LP; }
")"                     { MK("RP");       return RP; }
"["                     { MK("LB");       return LB; }
"]"                     { MK("RB");       return RB; }
"{"                     { MK("LC");       return LC; }
"}"                     { MK("RC");       return RC; }

.                       { /* ignore unknown char (input is assumed error-free) */ }

%%
