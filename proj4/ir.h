#ifndef IR_H
#define IR_H

#include <stdio.h>
#include "ast.h"

/* ---------------- Operands ---------------- */
typedef enum {
    OPD_VARIABLE,   /* v<no>  : a named user variable / its IR temp */
    OPD_TEMP,       /* t<no>  : a compiler generated temporary */
    OPD_CONSTANT,   /* #value : an immediate integer */
    OPD_ADDRESS,    /* &v<no> : the address of a variable */
    OPD_LABEL,      /* label<no> */
    OPD_FUNCTION    /* function name */
} OperandKind;

typedef struct Operand {
    OperandKind kind;
    int no;         /* number for VARIABLE / TEMP / ADDRESS / LABEL */
    int value;      /* value for CONSTANT */
    char *name;     /* name for FUNCTION */
} Operand;

/* ---------------- Intermediate codes ---------------- */
typedef enum {
    IR_LABEL,       /* LABEL x : */
    IR_FUNCTION,    /* FUNCTION f : */
    IR_ASSIGN,      /* x := y */
    IR_ADD,         /* x := y + z */
    IR_SUB,         /* x := y - z */
    IR_MUL,         /* x := y * z */
    IR_DIV,         /* x := y / z */
    IR_LOAD,        /* x := *y */
    IR_STORE,       /* *x := y */
    IR_GOTO,        /* GOTO x */
    IR_IF,          /* IF x [relop] y GOTO z */
    IR_RETURN,      /* RETURN x */
    IR_DEC,         /* DEC x [size] */
    IR_ARG,         /* ARG x */
    IR_CALL,        /* x := CALL f */
    IR_PARAM,       /* PARAM x */
    IR_READ,        /* READ x */
    IR_WRITE        /* WRITE x */
} IRKind;

typedef struct InterCode {
    IRKind kind;
    Operand *result;
    Operand *arg1;
    Operand *arg2;
    char *relop;        /* for IR_IF */
    int size;           /* for IR_DEC */
    struct InterCode *prev, *next;
} InterCode;

/* ---------------- Operand constructors ---------------- */
Operand *new_temp(void);
Operand *new_label_op(void);
Operand *new_var_op(void);
Operand *const_op(int value);
Operand *addr_op(Operand *var);     /* &var */
Operand *func_op(const char *name);

/* ---------------- Driver ---------------- */
void translate_program(Node *root);
void ir_output(FILE *out);
void ir_optimize(void);
InterCode *ir_get_head(void);

#endif
