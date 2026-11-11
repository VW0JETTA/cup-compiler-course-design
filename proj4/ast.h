#ifndef AST_H
#define AST_H

typedef struct Node Node;

struct Node {
    char *name;       /* grammar symbol name, e.g. "Exp", "ID", "INT" */
    int line;         /* source line number */
    int is_token;     /* 1 if this node is a terminal (lexical token) */
    char *text;       /* lexeme text for ID / TYPE / RELOP, etc. */
    int ival;         /* integer value for INT tokens */
    Node *child;      /* first child */
    Node *next;       /* next sibling */
};

/* Create a non-terminal node with `count` children (variadic). */
Node *new_node(const char *name, int line, int count, ...);

/* Create a terminal (token) node carrying its lexeme. */
Node *new_token(const char *name, const char *text, int line, int ival);

/* Navigation helpers. */
Node *nth_child(Node *n, int k);          /* 0-indexed child, NULL if absent */
int   num_children(Node *n);
Node *child_named(Node *n, const char *name); /* first direct child with name */

#endif
