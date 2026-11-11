#include <stdio.h>
#include <stdlib.h>
#include "tree.h"
#include "semantic.h"

extern int   yyparse(void);
extern void  yyrestart(FILE*);
extern FILE* yyin;
extern Node* root;
extern int   lexFatal;

int hasError = 0;

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

    if (hasError) return 1;

    /* 语义分析 */
    if (root) {
        int semErrs = semanticCheck(root);
        return semErrs > 0 ? 1 : 0;
    }

    return 0;
}
