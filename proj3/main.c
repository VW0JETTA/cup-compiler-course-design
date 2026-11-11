#include <stdio.h>
#include "ast.h"
#include "ir.h"

extern int yyparse(void);
extern FILE *yyin;
extern Node *ast_root;

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input> [output.ir]\n", argv[0]);
        return 1;
    }

    FILE *fin = fopen(argv[1], "r");
    if (!fin) {
        perror(argv[1]);
        return 1;
    }
    yyin = fin;

    if (yyparse() != 0 || ast_root == NULL) {
        fclose(fin);
        return 1;
    }
    fclose(fin);

    translate_program(ast_root);

    FILE *fout = (argc >= 3) ? fopen(argv[2], "w") : stdout;
    if (!fout) {
        perror(argv[2]);
        return 1;
    }
    ir_output(fout);
    if (fout != stdout) fclose(fout);

    return 0;
}
