#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "ast.h"

static char *dup_str(const char *s) {
    if (!s) return NULL;
    char *p = malloc(strlen(s) + 1);
    strcpy(p, s);
    return p;
}

Node *new_node(const char *name, int line, int count, ...) {
    Node *n = (Node *)calloc(1, sizeof(Node));
    n->name = dup_str(name);
    n->line = line;
    n->is_token = 0;

    va_list ap;
    va_start(ap, count);
    Node *prev = NULL;
    for (int i = 0; i < count; i++) {
        Node *c = va_arg(ap, Node *);
        if (c == NULL) continue;
        if (n->child == NULL) {
            n->child = c;
            if (n->line == 0) n->line = c->line;
        } else {
            prev->next = c;
        }
        prev = c;
    }
    va_end(ap);
    return n;
}

Node *new_token(const char *name, const char *text, int line, int ival) {
    Node *n = (Node *)calloc(1, sizeof(Node));
    n->name = dup_str(name);
    n->text = dup_str(text);
    n->line = line;
    n->ival = ival;
    n->is_token = 1;
    return n;
}

Node *nth_child(Node *n, int k) {
    if (!n) return NULL;
    Node *c = n->child;
    while (c && k > 0) {
        c = c->next;
        k--;
    }
    return c;
}

int num_children(Node *n) {
    int cnt = 0;
    for (Node *c = n ? n->child : NULL; c; c = c->next) cnt++;
    return cnt;
}

Node *child_named(Node *n, const char *name) {
    for (Node *c = n ? n->child : NULL; c; c = c->next)
        if (strcmp(c->name, name) == 0) return c;
    return NULL;
}
