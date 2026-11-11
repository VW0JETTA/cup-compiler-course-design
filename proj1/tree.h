#ifndef TREE_H
#define TREE_H

/* ============================================================
 * 语法树节点定义（first-child / next-sibling 多叉树）
 * 词法单元（终结符）为叶子节点，由 Flex 创建；
 * 语法单元（非终结符）为内部节点，由 Bison 归约时创建。
 * ============================================================ */
typedef struct Node {
    char* name;              /* 语法单元名 或 词法单元名 */
    int   lineno;            /* 该单元第一个词素出现的行号 */
    int   isToken;           /* 1=词法单元(终结符)  0=语法单元(非终结符) */
    int   hasValue;          /* 1=需要额外打印 value 文本 */
    char* value;             /* 附加信息：ID词素 / int|float / 十进制数值 */
    struct Node* firstChild; /* 第一个子节点 */
    struct Node* nextSibling;/* 下一个兄弟节点 */
} Node;

/* 创建词法单元（叶子）节点 */
Node* newToken(const char* name, int lineno, int hasValue, const char* value);

/* 创建语法单元（内部）节点。
 * numChildren 之后跟若干 Node* 子节点（按从左到右顺序）。
 * 会自动跳过 NULL 子节点（对应 ε 产生式）；
 * 若全部子节点为 NULL，则返回 NULL（表示该单元产生了 ε，不打印）。
 * 行号取第一个非空子节点的行号。 */
Node* newNonterm(const char* name, int numChildren, ...);

/* 先序遍历打印语法树，每深一层缩进 2 个空格 */
void printTree(Node* root, int depth);

#endif
