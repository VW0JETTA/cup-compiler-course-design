#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "tree.h"

static char* dupStr(const char* s) {
    if (!s) return NULL;
    char* p = (char*)malloc(strlen(s) + 1);
    strcpy(p, s);
    return p;
}

Node* newToken(const char* name, int lineno, int hasValue, const char* value) {
    Node* n = (Node*)malloc(sizeof(Node));
    n->name        = dupStr(name);
    n->lineno      = lineno;
    n->isToken     = 1;
    n->hasValue    = hasValue;
    n->value       = hasValue ? dupStr(value) : NULL;
    n->firstChild  = NULL;
    n->nextSibling = NULL;
    return n;
}

Node* newNonterm(const char* name, int numChildren, ...) {
    Node*   children[16];
    int     cnt = 0;
    va_list ap;
    va_start(ap, numChildren);
    for (int i = 0; i < numChildren; i++) {
        Node* c = va_arg(ap, Node*);
        if (c) children[cnt++] = c;   /* 跳过 ε（NULL）子节点 */
    }
    va_end(ap);

    if (cnt == 0) return NULL;        /* 整个产生式归约为 ε，不生成节点 */

    Node* n = (Node*)malloc(sizeof(Node));
    n->name        = dupStr(name);
    n->lineno      = children[0]->lineno;  /* 第一个词素的行号 */
    n->isToken     = 0;
    n->hasValue    = 0;
    n->value       = NULL;
    n->firstChild  = children[0];
    n->nextSibling = NULL;
    for (int i = 0; i + 1 < cnt; i++)
        children[i]->nextSibling = children[i + 1];
    children[cnt - 1]->nextSibling = NULL;
    return n;
}

void printTree(Node* root, int depth) {
    if (!root) return;
    for (int i = 0; i < depth; i++) printf("  ");   /* 每层缩进 2 空格 */

    if (root->isToken) {
        if (root->hasValue)
            printf("%s: %s\n", root->name, root->value);
        else
            printf("%s\n", root->name);
    } else {
        printf("%s (%d)\n", root->name, root->lineno);
    }

    for (Node* c = root->firstChild; c; c = c->nextSibling)
        printTree(c, depth + 1);
}
