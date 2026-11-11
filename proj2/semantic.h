#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "tree.h"

typedef struct Type_* Type;
typedef struct FieldList_* FieldList;
typedef struct Symbol_* Symbol;
typedef struct Param_* Param;

/* ---------- 结构体域链表（教材 3.2.4） ---------- */
struct FieldList_ {
    char* name;          /* 域的名字 */
    Type type;           /* 域的类型 */
    FieldList tail;      /* 下一个域 */
};

/* ---------- 类型系统（教材 3.2.4） ---------- */
enum { BASIC, ARRAY, STRUCTURE };

struct Type_ {
    int kind;
    union {
        int basic;                              /* BASIC: 0=int, 1=float */
        struct { Type elem; int size; } array;  /* ARRAY: 元素类型 + 数组大小 */
        FieldList structure;                    /* STRUCTURE: 域链表 */
    };
};

/* ---------- 函数参数 ---------- */
struct Param_ {
    char* name;
    Type type;
    Param next;
};

/* ---------- 符号种类 ---------- */
enum { SYM_VAR, SYM_FUNC, SYM_STRUCT };

/* ---------- 符号表项 ---------- */
struct Symbol_ {
    char* name;
    int kind;           /* SYM_VAR / SYM_FUNC / SYM_STRUCT */
    Type type;          /* 变量类型 / 函数返回类型 / 结构体类型 */
    int lineno;
    int defined;        /* 函数是否已定义 */

    /* 函数特有 */
    Param params;       /* 形参链表 */
    int paramCount;

    Symbol next;        /* 哈希冲突链 */
};

/* ---------- 符号表 ---------- */
#define HASH_SIZE 256

typedef struct SymTab_ {
    Symbol table[HASH_SIZE];
    struct SymTab_* outer;  /* 外层作用域（NULL=全局） */
} SymTab;

/* ---------- FieldList 操作 ---------- */
FieldList fieldListNew(const char* name, Type type, FieldList tail);
void      fieldListFree(FieldList fl);

/* ---------- 符号表操作 ---------- */
SymTab* symtabNew(SymTab* outer);
void    symtabFree(SymTab* st);
Symbol  symtabLookup(SymTab* st, const char* name);
Symbol  symtabLookupAll(SymTab* st, const char* name);  /* 穿透所有层 */
int     symtabInsert(SymTab* st, Symbol sym);

/* ---------- 类型操作 ---------- */
Type    typeNew(int kind);
Type    typeBasic(int bt);
Type    typeArray(Type elem, int size);
Type    typeStruct(FieldList fields);
int     typeEqual(Type t1, Type t2);

/* ---------- 参数操作 ---------- */
Param   paramNew(const char* name, Type type);
void    paramFree(Param p);

/* ---------- 符号创建 ---------- */
Symbol  symNewVar(const char* name, Type type, int lineno);
Symbol  symNewFunc(const char* name, Type retType, int lineno);
Symbol  symNewStruct(const char* name, Type type, int lineno);

/* ---------- 语义检查主入口 ---------- */
int semanticCheck(Node* root);

#endif
