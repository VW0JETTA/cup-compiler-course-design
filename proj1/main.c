#include <stdio.h>
#include <stdlib.h>
#include "tree.h"

extern int   yyparse(void);
extern void  yyrestart(FILE*);
extern FILE* yyin;
extern Node* root;        /* 语法树根（syntax.y 定义） */

int hasError = 0;         /* 全局错误标志：词法(A)或语法(B)错误 */

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <source.cmm>\n", argv[0]);
        return 1;
    }

    FILE* f = fopen(argv[1], "r");
    if (!f) {
        fprintf(stderr, "Error: cannot open file '%s'.\n", argv[1]);
        return 1;
    }

    yyin = f;
    yyrestart(yyin);
    yyparse();
    fclose(f);

    /* 无任何词法 / 语法错误时，先序遍历打印语法树 */
    if (!hasError && root) {
        printTree(root, 0);
    }

    return hasError ? 1 : 0;
}
