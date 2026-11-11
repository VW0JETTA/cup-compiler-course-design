#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ir_opt.h"

char *dup_str(const char *s) {
    if (!s) return NULL;
    char *p = malloc(strlen(s) + 1);
    strcpy(p, s);
    return p;
}

static int g_op_id = 0;

static Operand *parse_operand(const char *s) {
    if (!s || !*s) return NULL;
    Operand *o = calloc(1, sizeof(Operand));
    o->id = ++g_op_id;
    if (s[0] == '#') {
        o->kind = OPD_CONSTANT;
        o->value = atoi(s + 1);
    } else if (s[0] == '&') {
        o->kind = OPD_ADDRESS;
        o->no = atoi(s + 2);
    } else if (s[0] == 'v') {
        o->kind = OPD_VARIABLE;
        o->no = atoi(s + 1);
    } else if (s[0] == 't') {
        o->kind = OPD_TEMP;
        o->no = atoi(s + 1);
    } else if (strncmp(s, "label", 5) == 0) {
        o->kind = OPD_LABEL;
        o->no = atoi(s + 5);
    } else {
        o->kind = OPD_FUNCTION;
        o->name = dup_str(s);
    }
    return o;
}

static InterCode *emit_ir(Program *prog, IRKind k) {
    InterCode *c = calloc(1, sizeof(InterCode));
    c->kind = k;
    if (!prog->ir_head) { prog->ir_head = prog->ir_tail = c; }
    else { prog->ir_tail->next = c; c->prev = prog->ir_tail; prog->ir_tail = c; }
    return c;
}

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) return s;
    char *e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) *e-- = '\0';
    return s;
}

static int parse_one_line(Program *prog, char *line) {
    line = trim(line);
    if (!*line) return 0;

    char buf[4096];
    strcpy(buf, line);

    /* Remove trailing colon for LABEL/FUNCTION */
    int len = strlen(buf);
    if (len > 0 && buf[len-1] == ':') buf[len-1] = '\0';

    char *tok[16]; int nt = 0;
    char *p = buf;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (nt >= 16) break;
        tok[nt++] = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) { *p = '\0'; p++; }
    }
    if (nt == 0) return 0;

    if (strcmp(tok[0], "LABEL") == 0 && nt >= 2) {
        InterCode *c = emit_ir(prog, IR_LABEL);
        c->arg1 = parse_operand(tok[1]);
        return 1;
    }
    if (strcmp(tok[0], "FUNCTION") == 0 && nt >= 2) {
        InterCode *c = emit_ir(prog, IR_FUNCTION);
        c->arg1 = parse_operand(tok[1]);
        return 1;
    }
    if (strcmp(tok[0], "GOTO") == 0 && nt >= 2) {
        InterCode *c = emit_ir(prog, IR_GOTO);
        c->arg1 = parse_operand(tok[1]);
        return 1;
    }
    if (strcmp(tok[0], "RETURN") == 0 && nt >= 2) {
        InterCode *c = emit_ir(prog, IR_RETURN);
        c->arg1 = parse_operand(tok[1]);
        return 1;
    }
    if (strcmp(tok[0], "READ") == 0 && nt >= 2) {
        InterCode *c = emit_ir(prog, IR_READ);
        c->arg1 = parse_operand(tok[1]);
        return 1;
    }
    if (strcmp(tok[0], "WRITE") == 0 && nt >= 2) {
        InterCode *c = emit_ir(prog, IR_WRITE);
        c->arg1 = parse_operand(tok[1]);
        return 1;
    }
    if (strcmp(tok[0], "ARG") == 0 && nt >= 2) {
        InterCode *c = emit_ir(prog, IR_ARG);
        c->arg1 = parse_operand(tok[1]);
        return 1;
    }
    if (strcmp(tok[0], "PARAM") == 0 && nt >= 2) {
        InterCode *c = emit_ir(prog, IR_PARAM);
        c->arg1 = parse_operand(tok[1]);
        return 1;
    }
    if (strcmp(tok[0], "DEC") == 0 && nt >= 3) {
        InterCode *c = emit_ir(prog, IR_DEC);
        c->arg1 = parse_operand(tok[1]);
        c->size = atoi(tok[2]);
        return 1;
    }
    if (strcmp(tok[0], "IF") == 0 && nt >= 5) {
        InterCode *c = emit_ir(prog, IR_IF);
        c->arg1 = parse_operand(tok[1]);
        c->relop = dup_str(tok[2]);
        c->arg2 = parse_operand(tok[3]);
        if (strcmp(tok[4], "GOTO") == 0 && nt >= 6)
            c->result = parse_operand(tok[5]);
        return 1;
    }
    if (nt >= 3 && strcmp(tok[1], ":=") == 0) {
        /* STORE: *x := y  (tok[0] is "*x") — must be checked before the
         * generic assignment cases, otherwise "*x" would be mistaken for a
         * plain destination operand. */
        if (tok[0][0] == '*') {
            InterCode *c = emit_ir(prog, IR_STORE);
            c->result = parse_operand(tok[0] + 1);
            c->arg1 = parse_operand(tok[2]);
            return 1;
        }
        /* LOAD: x := *y  (tok[2] is "*y") */
        if (tok[2][0] == '*' && nt >= 3) {
            InterCode *c = emit_ir(prog, IR_LOAD);
            c->result = parse_operand(tok[0]);
            c->arg1 = parse_operand(tok[2] + 1);
            return 1;
        }
        if (strcmp(tok[2], "CALL") == 0 && nt >= 4) {
            InterCode *c = emit_ir(prog, IR_CALL);
            c->result = parse_operand(tok[0]);
            c->arg1 = parse_operand(tok[3]);
            return 1;
        }
        /* simple assignment */
        if (nt == 3) {
            InterCode *c = emit_ir(prog, IR_ASSIGN);
            c->result = parse_operand(tok[0]);
            c->arg1 = parse_operand(tok[2]);
            return 1;
        }
        /* arithmetic */
        if (nt == 5) {
            IRKind k;
            if (strcmp(tok[3], "+") == 0) k = IR_ADD;
            else if (strcmp(tok[3], "-") == 0) k = IR_SUB;
            else if (strcmp(tok[3], "*") == 0) k = IR_MUL;
            else if (strcmp(tok[3], "/") == 0) k = IR_DIV;
            else return 0;
            InterCode *c = emit_ir(prog, k);
            c->result = parse_operand(tok[0]);
            c->arg1 = parse_operand(tok[2]);
            c->arg2 = parse_operand(tok[4]);
            return 1;
        }
    }
    return 0;
}

Program *read_ir(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) { perror(filename); return NULL; }
    Program *prog = calloc(1, sizeof(Program));
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        parse_one_line(prog, line);
    }
    fclose(f);
    return prog;
}
