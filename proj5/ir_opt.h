#ifndef IR_OPT_H
#define IR_OPT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- Operand kinds ---------------- */
typedef enum {
    OPD_VARIABLE,
    OPD_TEMP,
    OPD_CONSTANT,
    OPD_ADDRESS,
    OPD_LABEL,
    OPD_FUNCTION
} OperandKind;

typedef struct Operand {
    OperandKind kind;
    int no;
    int value;
    char *name;
    int id; /* unique id for dataflow */
} Operand;

/* ---------------- IR kinds ---------------- */
typedef enum {
    IR_LABEL,
    IR_FUNCTION,
    IR_ASSIGN,
    IR_ADD,
    IR_SUB,
    IR_MUL,
    IR_DIV,
    IR_LOAD,
    IR_STORE,
    IR_GOTO,
    IR_IF,
    IR_RETURN,
    IR_DEC,
    IR_ARG,
    IR_CALL,
    IR_PARAM,
    IR_READ,
    IR_WRITE
} IRKind;

typedef struct InterCode {
    IRKind kind;
    Operand *result;
    Operand *arg1;
    Operand *arg2;
    char *relop;
    int size;
    struct InterCode *prev, *next;
} InterCode;

/* ---------------- Basic Block ---------------- */
typedef struct BasicBlock {
    int id;
    InterCode *first;
    InterCode *last;
    int succ_count;
    struct BasicBlock *succ[2];
    int pred_count;
    struct BasicBlock *pred[10];
    int visited;
    int in_loop;
    int loop_depth;
} BasicBlock;

/* ---------------- Loop ---------------- */
typedef struct Loop {
    BasicBlock *header;
    BasicBlock **blocks;
    int count;
    int cap;
    struct Loop *outer;
    BasicBlock *preheader;
} Loop;

/* ---------------- Function ---------------- */
typedef struct Function {
    char *name;
    BasicBlock *entry;
    int bb_count;
    BasicBlock **bbs;
    Loop *loops;
    int loop_count;
    struct Function *next;
} Function;

/* ---------------- Program ---------------- */
typedef struct {
    InterCode *ir_head, *ir_tail;
    int op_count;
    Function *funcs;
    int func_count;
} Program;

/* ---------------- helper ---------------- */
char *dup_str(const char *s);

#endif
