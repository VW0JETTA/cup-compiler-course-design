#include <stdio.h>
#include <stdlib.h>
#include "ir_opt.h"

Program *read_ir(const char *filename);
void optimize_program(Program *prog);
void output_ir(Program *prog, FILE *out);

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.ir> [output.ir]\n", argv[0]);
        return 1;
    }

    Program *prog = read_ir(argv[1]);
    if (!prog) return 1;

    optimize_program(prog);

    FILE *fout = (argc >= 3) ? fopen(argv[2], "w") : stdout;
    if (!fout) { perror(argv[2]); return 1; }
    output_ir(prog, fout);
    if (fout != stdout) fclose(fout);

    return 0;
}
