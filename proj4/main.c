#include <stdio.h>
#include "ast.h"
#include "ir.h"
#include "codegen.h"

extern int yyparse(void);
extern FILE *yyin;
extern Node *ast_root;

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input.cmm> <output.s>\n", argv[0]);
        return 1;
    }

    FILE *fin = fopen(argv[1], "r");
    if (!fin) { perror(argv[1]); return 1; }
    yyin = fin;

    if (yyparse() != 0 || ast_root == NULL) {
        fclose(fin);
        return 1;
    }
    fclose(fin);

    translate_program(ast_root);
    ir_optimize();

    FILE *fout = fopen(argv[2], "w");
    if (!fout) { perror(argv[2]); return 1; }

    generate_mips(fout);

    fclose(fout);
    return 0;
}
