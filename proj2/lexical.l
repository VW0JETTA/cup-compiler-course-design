%{
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "tree.h"
#include "syntax.tab.h"      /* Bison 生成：Token 宏 + yylval(union) */

extern int hasError;         /* 全局错误标志（main.c 中定义） */

/* 维护列号，供位置追踪 / 报错使用 */
int yycolumn = 1;

/* 致命词法错误(未闭合注释)标志，抑制其引发的连带语法错误 */
int lexFatal = 0;

/* 每次匹配后更新 yylloc 的位置信息 */
#define YY_USER_ACTION \
    yylloc.first_line = yylloc.last_line = yylineno; \
    yylloc.first_column = yycolumn; \
    yylloc.last_column = yycolumn + yyleng - 1; \
    yycolumn += yyleng;

/* 生成一个不带附加信息的词法单元节点 */
#define TOK(NAME) \
    do { yylval.node = newToken(NAME, yylineno, 0, NULL); } while (0)

/* 生成一个带附加信息(value)的词法单元节点 */
#define TOKV(NAME, VAL) \
    do { yylval.node = newToken(NAME, yylineno, 1, VAL); } while (0)
%}

%option noyywrap
%option yylineno
%option nounput
%option noinput
%option never-interactive

DIGIT    [0-9]
LETTER   [a-zA-Z]
HEXDIGIT [0-9a-fA-F]
OCTDIGIT [0-7]
WS       [ \t\r]+

%x BLOCK_COMMENT

%%

    /* ---------- 空白 ---------- */
{WS}        { /* 丢弃 */ }
\n          { yycolumn = 1; }

    /* ---------- 块注释（跨行，检测未闭合） ---------- */
"/*"                     { BEGIN(BLOCK_COMMENT); }
<BLOCK_COMMENT>"*/"      { BEGIN(INITIAL); }
<BLOCK_COMMENT>\n        { yycolumn = 1; }
<BLOCK_COMMENT>.         { /* 丢弃注释内容 */ }
<BLOCK_COMMENT><<EOF>>   {
    printf("Error type A at Line %d: unclosed block comment.\n", yylineno);
    hasError = 1;
    lexFatal = 1;
    yyterminate();
}

    /* ---------- 单行注释 ---------- */
"//".*      { /* 丢弃 */ }

    /* ---------- 关键字（须在 ID 之前） ---------- */
"int"       { TOKV("TYPE", yytext); return TYPE; }
"float"     { TOKV("TYPE", yytext); return TYPE; }
"if"        { TOK("IF");     return IF; }
"else"      { TOK("ELSE");   return ELSE; }
"while"     { TOK("WHILE");  return WHILE; }
"return"    { TOK("RETURN"); return RETURN; }
"struct"    { TOK("STRUCT"); return STRUCT; }

    /* ---------- 标识符 ---------- */
[a-zA-Z_][a-zA-Z0-9_]* { TOKV("ID", yytext); return ID; }

    /* ---------- 十六进制整数 0x3F ---------- */
0[xX]{HEXDIGIT}+ {
    char buf[64];
    sprintf(buf, "%ld", strtol(yytext, NULL, 0));
    TOKV("INT", buf);
    return INT;
}
    /* ---------- 非法十六进制 0x3G ---------- */
0[xX]{HEXDIGIT}*[g-zG-Z_][0-9a-zA-Z_]* {
    printf("Error type A at Line %d: illegal hexadecimal number '%s'.\n", yylineno, yytext);
    hasError = 1;
    TOKV("INT", "0");        /* 占位返回，保持语法完整，避免同行连带语法错误 */
    return INT;
}
    /* ---------- 畸形十六进制 0x（缺少有效数位） ---------- */
0[xX] {
    printf("Error type A at Line %d: illegal hexadecimal number '%s'.\n", yylineno, yytext);
    hasError = 1;
    TOKV("INT", "0");
    return INT;
}

    /* ---------- 八进制整数 0123 ---------- */
0{OCTDIGIT}+ {
    char buf[64];
    sprintf(buf, "%ld", strtol(yytext, NULL, 0));
    TOKV("INT", buf);
    return INT;
}
    /* ---------- 非法八进制 09 ---------- */
0{DIGIT}+ {
    printf("Error type A at Line %d: illegal octal number '%s'.\n", yylineno, yytext);
    hasError = 1;
    TOKV("INT", "0");        /* 占位返回，保持语法完整，避免同行连带语法错误 */
    return INT;
}

    /* ---------- 浮点数（基数必须含小数点，符合附录A补充说明） ---------- */
    /* 指数形式：基数含小数点，如 01.23E12、43.e-4、.5E03 */
{DIGIT}+"."{DIGIT}*[eE][+-]?{DIGIT}+ |
{DIGIT}*"."{DIGIT}+[eE][+-]?{DIGIT}+ |
    /* 基本形式：小数点前后均须有数字，如 12.43 */
{DIGIT}+"."{DIGIT}+ {
    TOKV("FLOAT", yytext);
    return FLOAT;
}

    /* ---------- 畸形指数浮点（含 e/E 但不是合法浮点）报 A 类（选做2.2）---------- */
    /* 如 1.5e、1.5e+、1.5E-、1e5(基数无小数点)、12e- 等                          */
({DIGIT}+"."{DIGIT}*|{DIGIT}*"."{DIGIT}+|{DIGIT}+)[eE][+-]?{DIGIT}* {
    printf("Error type A at Line %d: malformed floating-point number '%s'.\n", yylineno, yytext);
    hasError = 1;
    TOKV("FLOAT", "0");      /* 占位返回，保持语法完整，避免同行连带语法错误 */
    return FLOAT;
}

    /* ---------- 十进制整数 ---------- */
{DIGIT}+ {
    char buf[64];
    sprintf(buf, "%ld", strtol(yytext, NULL, 10));
    TOKV("INT", buf);
    return INT;
}

    /* ---------- 运算符 ---------- */
"=="    { TOK("RELOP");    return RELOP; }
"!="    { TOK("RELOP");    return RELOP; }
"<="    { TOK("RELOP");    return RELOP; }
">="    { TOK("RELOP");    return RELOP; }
"<"     { TOK("RELOP");    return RELOP; }
">"     { TOK("RELOP");    return RELOP; }
"&&"    { TOK("AND");      return AND; }
"||"    { TOK("OR");       return OR; }
"="     { TOK("ASSIGNOP"); return ASSIGNOP; }
"+"     { TOK("PLUS");     return PLUS; }
"-"     { TOK("MINUS");    return MINUS; }
"*"     { TOK("STAR");     return STAR; }
"/"     { TOK("DIV");      return DIV; }
"!"     { TOK("NOT");      return NOT; }

    /* ---------- 分隔符 ---------- */
"("     { TOK("LP");    return LP; }
")"     { TOK("RP");    return RP; }
"["     { TOK("LB");    return LB; }
"]"     { TOK("RB");    return RB; }
"{"     { TOK("LC");    return LC; }
"}"     { TOK("RC");    return RC; }
","     { TOK("COMMA"); return COMMA; }
";"     { TOK("SEMI");  return SEMI; }
"."     { TOK("DOT");   return DOT; }

    /* ---------- 非法字符（兜底） ---------- */
. {
    printf("Error type A at Line %d: illegal character '%s'.\n", yylineno, yytext);
    hasError = 1;
}

%%
