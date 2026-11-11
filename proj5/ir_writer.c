#include <stdio.h>
#include <stdlib.h>
#include "ir_opt.h"

static void print_op(FILE *out, Operand *o) {
    if (!o) { fprintf(out, "??"); return; }
    switch (o->kind) {
        case OPD_VARIABLE: fprintf(out, "v%d", o->no); break;
        case OPD_TEMP:     fprintf(out, "t%d", o->no); break;
        case OPD_CONSTANT: fprintf(out, "#%d", o->value); break;
        case OPD_ADDRESS:  fprintf(out, "&v%d", o->no); break;
        case OPD_LABEL:    fprintf(out, "label%d", o->no); break;
        case OPD_FUNCTION: fprintf(out, "%s", o->name); break;
    }
}

void output_ir(Program *prog, FILE *out) {
    for (InterCode *c = prog->ir_head; c; c = c->next) {
        switch (c->kind) {
            case IR_LABEL:
                fprintf(out, "LABEL "); print_op(out, c->arg1); fprintf(out, " :\n"); break;
            case IR_FUNCTION:
                fprintf(out, "FUNCTION "); print_op(out, c->arg1); fprintf(out, " :\n"); break;
            case IR_ASSIGN:
                print_op(out, c->result); fprintf(out, " := "); print_op(out, c->arg1); fprintf(out, "\n"); break;
            case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: {
                char op = c->kind == IR_ADD ? '+' : c->kind == IR_SUB ? '-' :
                          c->kind == IR_MUL ? '*' : '/';
                print_op(out, c->result); fprintf(out, " := ");
                print_op(out, c->arg1); fprintf(out, " %c ", op); print_op(out, c->arg2);
                fprintf(out, "\n"); break;
            }
            case IR_LOAD:
                print_op(out, c->result); fprintf(out, " := *"); print_op(out, c->arg1); fprintf(out, "\n"); break;
            case IR_STORE:
                fprintf(out, "*"); print_op(out, c->result); fprintf(out, " := "); print_op(out, c->arg1); fprintf(out, "\n"); break;
            case IR_GOTO:
                fprintf(out, "GOTO "); print_op(out, c->arg1); fprintf(out, "\n"); break;
            case IR_IF:
                fprintf(out, "IF "); print_op(out, c->arg1);
                fprintf(out, " %s ", c->relop); print_op(out, c->arg2);
                fprintf(out, " GOTO "); print_op(out, c->result); fprintf(out, "\n"); break;
            case IR_RETURN:
                fprintf(out, "RETURN "); print_op(out, c->arg1); fprintf(out, "\n"); break;
            case IR_DEC:
                fprintf(out, "DEC "); print_op(out, c->arg1); fprintf(out, " %d\n", c->size); break;
            case IR_ARG:
                fprintf(out, "ARG "); print_op(out, c->arg1); fprintf(out, "\n"); break;
            case IR_CALL:
                print_op(out, c->result); fprintf(out, " := CALL "); print_op(out, c->arg1); fprintf(out, "\n"); break;
            case IR_PARAM:
                fprintf(out, "PARAM "); print_op(out, c->arg1); fprintf(out, "\n"); break;
            case IR_READ:
                fprintf(out, "READ "); print_op(out, c->arg1); fprintf(out, "\n"); break;
            case IR_WRITE:
                fprintf(out, "WRITE "); print_op(out, c->arg1); fprintf(out, "\n"); break;
        }
    }
}
