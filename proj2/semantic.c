#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "semantic.h"

/* ==================== FieldList 操作 ==================== */
FieldList fieldListNew(const char* name, Type type, FieldList tail) {
    FieldList fl = (FieldList)malloc(sizeof(struct FieldList_));
    fl->name = name ? strdup(name) : NULL;
    fl->type = type;
    fl->tail = tail;
    return fl;
}

void fieldListFree(FieldList fl) {
    while (fl) {
        FieldList n = fl->tail;
        free(fl->name);
        /* 类型所有权由外部管理，这里不释放 type */
        free(fl);
        fl = n;
    }
}

/* ==================== 错误收集 ==================== */
#define MAX_ERRORS 256
typedef struct { int type, lineno; char msg[256]; } SemError;
static SemError errors[MAX_ERRORS];
static int errCount;

static void semError(int type, int lineno, const char* msg) {
    if (errCount >= MAX_ERRORS) return;
    errors[errCount].type = type;
    errors[errCount].lineno = lineno;
    strncpy(errors[errCount].msg, msg, 255);
    errors[errCount].msg[255] = 0;
    errCount++;
}
static int cmpError(const void* a, const void* b) {
    const SemError *ea = (const SemError*)a, *eb = (const SemError*)b;
    if (ea->lineno != eb->lineno) return ea->lineno - eb->lineno;
    return eb->type - ea->type;
}
static void printErrors(void) {
    int i, last = -1;
    if (!errCount) return;
    qsort(errors, errCount, sizeof(SemError), cmpError);
    for (i = 0; i < errCount; i++) {
        if (errors[i].lineno == last) continue;
        printf("Error type %d at Line %d: %s.\n", errors[i].type, errors[i].lineno, errors[i].msg);
        last = errors[i].lineno;
    }
}

/* ==================== PJW 哈希 ==================== */
static unsigned hashPJW(const char* s) {
    unsigned h = 0, g;
    for (; *s; s++) {
        h = (h << 4) + (unsigned char)*s;
        if ((g = h & 0xF0000000)) { h ^= (g >> 24); h ^= g; }
    }
    return h % HASH_SIZE;
}

/* ==================== 符号表 ==================== */
SymTab* symtabNew(SymTab* outer) {
    SymTab* st = (SymTab*)calloc(1, sizeof(SymTab));
    st->outer = outer;
    return st;
}
void symtabFree(SymTab* st) {
    int i; Symbol s, n;
    if (!st) return;
    for (i = 0; i < HASH_SIZE; i++)
        for (s = st->table[i]; s; s = n) {
            n = s->next;
            paramFree(s->params);
            free(s->name);
            if (s->type) {
                if (s->kind == SYM_STRUCT && s->type->kind == STRUCTURE)
                    fieldListFree(s->type->structure);
                free(s->type);
            }
            free(s);
        }
    free(st);
}
Symbol symtabLookup(SymTab* st, const char* name) {
    unsigned h = hashPJW(name);
    for (Symbol s = st->table[h]; s; s = s->next)
        if (!strcmp(s->name, name)) return s;
    return NULL;
}
Symbol symtabLookupAll(SymTab* st, const char* name) {
    for (SymTab* cur = st; cur; cur = cur->outer) {
        Symbol s = symtabLookup(cur, name);
        if (s) return s;
    }
    return NULL;
}
int symtabInsert(SymTab* st, Symbol sym) {
    if (symtabLookup(st, sym->name)) return 0;
    unsigned h = hashPJW(sym->name);
    sym->next = st->table[h];
    st->table[h] = sym;
    return 1;
}

/* ==================== 类型操作 ==================== */
Type typeNew(int k) {
    Type t = (Type)calloc(1, sizeof(struct Type_));
    t->kind = k;
    return t;
}
Type typeBasic(int bt) { Type t = typeNew(BASIC); t->basic = bt; return t; }
Type typeArray(Type elem, int sz) { Type t = typeNew(ARRAY); t->array.elem = elem; t->array.size = sz; return t; }
Type typeStruct(FieldList fields) {
    Type t = typeNew(STRUCTURE);
    t->structure = fields;
    return t;
}
static Type typeCopy(Type src) {
    if (!src) return NULL;
    Type t = (Type)malloc(sizeof(struct Type_));
    *t = *src;
    if (src->kind == ARRAY) t->array.elem = typeCopy(src->array.elem);
    /* STRUCTURE: 共享 FieldList，不深拷贝 */
    return t;
}

/* 名等价：同一 FieldList 指针即同一结构体 */
/* 结构等价（3.3）：递归比较域类型，忽略域名 */
static int fieldListEq(FieldList a, FieldList b) {
    while (a && b) {
        if (!typeEqual(a->type, b->type)) return 0;
        a = a->tail; b = b->tail;
    }
    return a == NULL && b == NULL;
}

int typeEqual(Type t1, Type t2) {
    if (!t1 || !t2) return 0;
    if (t1->kind != t2->kind) return 0;
    if (t1->kind == BASIC) return t1->basic == t2->basic;
    if (t1->kind == ARRAY)
        return typeEqual(t1->array.elem, t2->array.elem);
    if (t1->kind == STRUCTURE)
        return fieldListEq(t1->structure, t2->structure);
    return 0;
}
static int typeIsInt(Type t)  { return t && t->kind == BASIC && t->basic == 0; }
static int typeIsArith(Type t) { return t && t->kind == BASIC; }
static int typeIsStruct(Type t) { return t && t->kind == STRUCTURE; }
static int typeIsArray(Type t) { return t && t->kind == ARRAY; }

/* ==================== 参数 ==================== */
Param paramNew(const char* name, Type type) {
    Param p = (Param)malloc(sizeof(struct Param_));
    p->name = name ? strdup(name) : NULL;
    p->type = typeCopy(type);
    p->next = NULL;
    return p;
}
void paramFree(Param p) {
    while (p) { Param n = p->next; free(p->name); if (p->type) free(p->type); free(p); p = n; }
}
static int paramsEqual(Param p1, Param p2) {
    while (p1 && p2) {
        if (!typeEqual(p1->type, p2->type)) return 0;
        p1 = p1->next; p2 = p2->next;
    }
    return !p1 && !p2;
}

/* ==================== 符号创建 ==================== */
Symbol symNewVar(const char* name, Type type, int lineno) {
    Symbol s = (Symbol)calloc(1, sizeof(struct Symbol_));
    s->name = strdup(name); s->kind = SYM_VAR; s->type = typeCopy(type); s->lineno = lineno; s->defined = 1;
    return s;
}
Symbol symNewFunc(const char* name, Type retType, int lineno) {
    Symbol s = (Symbol)calloc(1, sizeof(struct Symbol_));
    s->name = strdup(name); s->kind = SYM_FUNC; s->type = typeCopy(retType); s->lineno = lineno; s->defined = 0;
    return s;
}
Symbol symNewStruct(const char* name, Type type, int lineno) {
    Symbol s = (Symbol)calloc(1, sizeof(struct Symbol_));
    s->name = name ? strdup(name) : NULL;
    s->kind = SYM_STRUCT;
    s->type = type;       /* 持有 FieldList 的所有权 */
    s->lineno = lineno;
    s->defined = 1;
    return s;
}

/* ==================== AST 辅助 ==================== */
static const char* getName(Node* n) {
    return (n && n->isToken && !strcmp(n->name, "ID")) ? n->value : NULL;
}
static int getInt(Node* n) {
    return (n && n->isToken && !strcmp(n->name, "INT")) ? atoi(n->value) : 0;
}
static int isIntType(Node* n) {
    return n && n->isToken && !strcmp(n->name, "TYPE") && n->value && !strcmp(n->value, "int");
}
static const char* expText(Node* n) {
    if (!n) return "?";
    if (n->isToken && n->value) return n->value;
    if (!n->isToken && n->firstChild && n->firstChild->isToken && n->firstChild->value) return n->firstChild->value;
    return n->isToken ? n->name : "?";
}
static Node* getVarList(Node* funDec) {
    if (!funDec) return NULL;
    Node *id = funDec->firstChild, *lp = id ? id->nextSibling : NULL;
    return lp ? lp->nextSibling : NULL;
}

/* ==================== VarDec 解析 ==================== */
static Type resolveVarDec(Node* n, Type base, char** outName) {
    if (!n || !n->firstChild) return NULL;
    Node* f = n->firstChild;
    if (f->isToken && !strcmp(f->name, "ID")) {
        if (outName) *outName = strdup(f->value);
        return typeCopy(base);
    }
    if (!strcmp(f->name, "VarDec")) {
        Type et = resolveVarDec(f, base, outName);
        Node *lb = f->nextSibling, *in = lb ? lb->nextSibling : NULL;
        if (in) return typeArray(et, getInt(in));
    }
    return NULL;
}

/* ==================== 前向声明 ==================== */
static Type   resolveSpecifier(Node* n, SymTab* st, int reg);
static Type   resolveStruct(Node* n, SymTab* st, int reg);
static FieldList collectFieldList(Node* defList, SymTab* st);
static void   collectFieldDecList(Node* dl, Type bt, SymTab* st, FieldList* phead, FieldList* ptail);
static void   collectFieldListRec(Node* defList, SymTab* st, FieldList* phead, FieldList* ptail);
static void   registerVars(Node* dl, Type base, SymTab* st);
static void   collectExtDefList(Node* n);
static void   collectExtDef(Node* n);
static void   handleFuncExtDef(Node* spec, Node* funDec, Node* third);
static void   checkExtDefs(Node* n, SymTab* st);
static void   checkExtDef(Node* n, SymTab* st);
static void   checkDecInit(Node* n, Type bt, SymTab* st);
static void   checkFnBody(Node* funDec, Type rt, Node* compSt);
static void   checkDefs(Node* n, SymTab* st);
static void   checkDecList(Node* n, Type bt, SymTab* st);
static int    checkStmts(Node* n, SymTab* st, Type rt);
static int    checkStmt(Node* n, SymTab* st, Type rt);
static Type   checkExp(Node* n, SymTab* st, int* isLval);
static void   checkArgs(Node* n, SymTab* st, Param exp, int lineno, const char* fn);
static int    countArgs(Node* n);
static void   collectArgs(Node* n, SymTab* st, Type* at, int* al, int* idx);
static void   checkAllDefined(void);

/* ==================== 参数处理 ==================== */
static int countParams(Node* vl) {
    int c = 0; Node* pn;
    if (!vl) return 0;
    for (pn = vl->firstChild; pn; pn = pn->nextSibling) {
        if (!strcmp(pn->name, "ParamDec")) c++;
        else if (!strcmp(pn->name, "VarList")) c += countParams(pn);
    }
    return c;
}
static Param buildParams(Node* vl, SymTab* st) {
    Param head = NULL, tail = NULL;
    if (!vl) return NULL;
    for (Node* pn = vl->firstChild; pn; pn = pn->nextSibling) {
        if (!strcmp(pn->name, "VarList")) {
            Param sub = buildParams(pn, st);
            if (sub) { if (head) tail->next = sub; else head = sub; while (tail && tail->next) tail = tail->next; if (!tail) tail = sub; }
            continue;
        }
        if (strcmp(pn->name, "ParamDec")) continue;
        Node *ps = pn->firstChild, *pv = ps ? ps->nextSibling : NULL;
        if (!ps || !pv) continue;
        Type pt = resolveSpecifier(ps, st, 1);
        char* pn2 = NULL; Type pft = resolveVarDec(pv, pt, &pn2);
        if (!pn2) { if (pft) free(pft); continue; }
        Param pp = paramNew(pn2, pft);
        if (pft) free(pft);
        free(pn2);
        if (!head) head = pp; else tail->next = pp;
        tail = pp;
    }
    return head;
}

/* ==================== 全局状态 ==================== */
static SymTab* globalST = NULL;

/* ==================== 注册全局变量 ==================== */
static void registerVars(Node* dl, Type base, SymTab* st) {
    if (!dl) return;
    for (Node* n = dl->firstChild; n; n = n->nextSibling) {
        if (!strcmp(n->name, "ExtDecList")) { registerVars(n, base, st); continue; }
        if (strcmp(n->name, "VarDec")) continue;
        char* vn = NULL; Type vt = resolveVarDec(n, base, &vn);
        if (!vn) { if (vt) free(vt); continue; }
        Symbol x = symtabLookupAll(st, vn);
        if (x) {
            char m[256]; snprintf(m, sizeof(m), "Redefined variable \"%s\"", vn);
            semError(3, n->lineno, m);
        } else {
            Symbol sv = symNewVar(vn, vt, n->lineno);
            if (vt) free(vt);
            symtabInsert(st, sv);
        }
        free(vn);
    }
}

/* ==================== Pass 1 ==================== */
static void handleFuncExtDef(Node* spec, Node* funDec, Node* third);

static void collectExtDefList(Node* n) {
    if (!n) return;
    for (Node* c = n->firstChild; c; c = c->nextSibling) {
        if (!strcmp(c->name, "ExtDef")) collectExtDef(c);
        else if (!strcmp(c->name, "ExtDefList")) collectExtDefList(c);
    }
}

static void collectExtDef(Node* n) {
    if (!n || !n->firstChild) return;
    Node *spec = n->firstChild, *s2 = spec->nextSibling;
    if (!s2) return;
    Node* s3 = s2->nextSibling;

    if (!strcmp(s2->name, "ExtDecList")) {
        Type t = resolveSpecifier(spec, globalST, 1);
        if (t) registerVars(s2, t, globalST);
    } else if (!strcmp(s2->name, "SEMI")) {
        resolveSpecifier(spec, globalST, 1);
    } else if (!strcmp(s2->name, "FunDec")) {
        handleFuncExtDef(spec, s2, s3);
    }
}

static void handleFuncExtDef(Node* spec, Node* funDec, Node* third) {
    const char* fn = getName(funDec->firstChild);
    if (!fn) return;
    Type retType = resolveSpecifier(spec, globalST, 1);
    if (!retType) return;

    int hasBody = (third && !strcmp(third->name, "CompSt"));
    Node* vl = getVarList(funDec);
    int nParams = countParams(vl);
    Param newParams = buildParams(vl, globalST);

    Symbol exist = symtabLookupAll(globalST, fn);
    if (exist && exist->kind == SYM_FUNC) {
        if (exist->defined && hasBody) {
            char m[256]; snprintf(m, sizeof(m), "Redefined function \"%s\"", fn);
            semError(4, funDec->firstChild->lineno, m);
            paramFree(newParams); return;
        }
        if (hasBody && !exist->defined) {
            if (!typeEqual(exist->type, retType) ||
                exist->paramCount != nParams ||
                !paramsEqual(exist->params, newParams)) {
                char m[256]; snprintf(m, sizeof(m), "Inconsistent declaration of function \"%s\"", fn);
                semError(19, funDec->firstChild->lineno, m);
            }
            exist->defined = 1;
            paramFree(exist->params);
            exist->params = newParams;
            exist->paramCount = nParams;
            return;
        }
        if (!hasBody) {
            if (!typeEqual(exist->type, retType) ||
                exist->paramCount != nParams ||
                !paramsEqual(exist->params, newParams)) {
                char m[256]; snprintf(m, sizeof(m), "Inconsistent declaration of function \"%s\"", fn);
                semError(19, funDec->firstChild->lineno, m);
            }
            paramFree(newParams); return;
        }
    }

    if (exist) {
        char m[256]; snprintf(m, sizeof(m), "Redefined function \"%s\"", fn);
        semError(4, funDec->firstChild->lineno, m);
        paramFree(newParams); return;
    }

    Symbol func = symNewFunc(fn, retType, funDec->firstChild->lineno);
    if (hasBody) func->defined = 1;
    func->params = newParams;
    func->paramCount = nParams;
    symtabInsert(globalST, func);
}

/* ==================== 类型解析 ==================== */
static Type resolveSpecifier(Node* n, SymTab* st, int reg) {
    Node* c = n ? n->firstChild : NULL;
    if (!c) return NULL;
    if (c->isToken && !strcmp(c->name, "TYPE"))
        return typeBasic(isIntType(c) ? 0 : 1);
    if (!strcmp(c->name, "StructSpecifier"))
        return resolveStruct(c, st, reg);
    return NULL;
}

static Type resolveStruct(Node* n, SymTab* st, int reg) {
    if (!n || !n->firstChild) return NULL;
    Node *sk = n->firstChild, *o = sk->nextSibling;
    if (!o) return NULL;
    Node* t3 = o->nextSibling;

    if ((t3 && !strcmp(t3->name, "LC")) || !strcmp(o->name, "LC")) {
        /* 定义体 */
        Node* defList;
        const char* tn = NULL; Node* tnode = NULL;
        if (!strcmp(o->name, "LC")) { defList = t3; }
        else {
            if (!strcmp(o->name, "OptTag") && o->firstChild) { tnode = o->firstChild; tn = getName(tnode); }
            defList = t3->nextSibling;
        }

        FieldList fields = NULL;
        if (defList && !strcmp(defList->name, "DefList"))
            fields = collectFieldList(defList, st);

        Type structType = typeStruct(fields);

        if (tn) {
            if (reg) {
                Symbol x = symtabLookupAll(st, tn);
                if (x && x->kind == SYM_STRUCT) {
                    char m[256]; snprintf(m, sizeof(m), "Duplicated name \"%s\"", tn);
                    semError(16, tnode->lineno, m);
                    fieldListFree(fields);
                    free(structType);
                    return typeStruct(NULL);
                }
            }
            Symbol sd = symNewStruct(tn, structType, tnode ? tnode->lineno : n->lineno);
            if (reg) symtabInsert(st, sd);
            return typeCopy(structType);
        } else {
            /* 匿名结构体不注册 */
            return structType;
        }
    } else {
        /* 引用已定义结构体 */
        const char* tn = NULL; Node* tnode = NULL;
        if (!strcmp(o->name, "Tag")) { tnode = o->firstChild; tn = getName(tnode); }
        else if (!strcmp(o->name, "OptTag")) { tnode = o->firstChild; tn = getName(tnode); }
        if (tn) {
            Symbol sd = symtabLookupAll(st, tn);
            if (!sd || sd->kind != SYM_STRUCT) {
                char m[256]; snprintf(m, sizeof(m), "Undefined structure \"%s\"", tn);
                semError(17, tnode->lineno, m);
                return NULL;
            }
            return typeCopy(sd->type);
        }
        semError(17, n->lineno, "Undefined structure \"\"");
        return NULL;
    }
}

/* ==================== FieldList 构建 ==================== */
static void collectFieldListRec(Node* defList, SymTab* st, FieldList* phead, FieldList* ptail) {
    if (!defList) return;
    Node* def;
    /* 倒序收集，保持语法顺序 */
    for (def = defList->firstChild; def; def = def->nextSibling) {
        if (!strcmp(def->name, "DefList")) { collectFieldListRec(def, st, phead, ptail); continue; }
        if (strcmp(def->name, "Def")) continue;
        Node *spec = def->firstChild, *dl = spec ? spec->nextSibling : NULL;
        if (!dl) continue;
        Type bt = resolveSpecifier(spec, st, 1);
        if (!bt) continue;
        collectFieldDecList(dl, bt, st, phead, ptail);
    }
}

static void collectFieldDecList(Node* dl, Type bt, SymTab* st, FieldList* phead, FieldList* ptail) {
    if (!dl) return;
    for (Node* dec = dl->firstChild; dec; dec = dec->nextSibling) {
        if (!strcmp(dec->name, "DecList")) {
            collectFieldDecList(dec, bt, st, phead, ptail);
            continue;
        }
        if (strcmp(dec->name, "Dec")) continue;
        Node *vd = dec->firstChild;
        if (!vd) continue;
        if (vd->nextSibling && !strcmp(vd->nextSibling->name, "ASSIGNOP")) {
            semError(15, vd->lineno, "Redefined field \"\"");
            continue;
        }
        char* vn = NULL; Type vt = resolveVarDec(vd, bt, &vn);
        if (!vn) { if (vt) free(vt); continue; }

        /* 检查域重复 */
        for (FieldList f = *phead; f; f = f->tail) {
            if (!strcmp(f->name, vn)) {
                char m[256]; snprintf(m, sizeof(m), "Redefined field \"%s\"", vn);
                semError(15, vd->lineno, m);
                free(vn); vn = NULL; if (vt) free(vt); break;
            }
        }
        if (!vn) continue;

        FieldList node = fieldListNew(vn, vt, NULL);
        free(vn);
        if (!*phead) *phead = node;
        else (*ptail)->tail = node;
        *ptail = node;
    }
}

static FieldList collectFieldList(Node* defList, SymTab* st) {
    FieldList head = NULL, tail = NULL;
    collectFieldListRec(defList, st, &head, &tail);
    return head;
}

/* ==================== Pass 2 ==================== */
static void checkExtDefs(Node* n, SymTab* st) {
    if (!n) return;
    for (Node* c = n->firstChild; c; c = c->nextSibling) {
        if (!strcmp(c->name, "ExtDef")) checkExtDef(c, st);
        else if (!strcmp(c->name, "ExtDefList")) checkExtDefs(c, st);
    }
}
static void checkExtDef(Node* n, SymTab* st) {
    if (!n || !n->firstChild) return;
    Node *spec = n->firstChild, *s2 = spec->nextSibling;
    if (!s2) return;
    Node* s3 = s2->nextSibling;
    if (!strcmp(s2->name, "ExtDecList")) {
        Type bt = resolveSpecifier(spec, st, 0);
        if (bt) checkDecInit(s2, bt, st);
    } else if (!strcmp(s2->name, "FunDec")) {
        if (s3 && !strcmp(s3->name, "CompSt")) {
            Type rt = resolveSpecifier(spec, st, 0);
            if (rt) checkFnBody(s2, rt, s3);
        }
    }
}
static void checkDecInit(Node* n, Type bt, SymTab* st) {
    if (!n) return;
    for (Node* dec = n->firstChild; dec; dec = dec->nextSibling) {
        if (!strcmp(dec->name, "DecList")) { checkDecInit(dec, bt, st); continue; }
        if (strcmp(dec->name, "Dec")) continue;
        Node *vd = dec->firstChild;
        if (!vd) continue;
        Node* ao = vd->nextSibling;
        if (!ao || strcmp(ao->name, "ASSIGNOP")) continue;
        Node* ex = ao->nextSibling;
        if (!ex) continue;
        char* vn = NULL; Type vt = resolveVarDec(vd, bt, &vn);
        int il; Type et = checkExp(ex, st, &il);
        if (vt && et && !typeEqual(vt, et))
            semError(5, ex->lineno, "Type mismatched for assignment");
        free(vn);
    }
}

/* ==================== 函数体检查 (3.2) ==================== */
static void checkFnBody(Node* funDec, Type retType, Node* compSt) {
    if (!funDec || !funDec->firstChild) return;
    const char* fn = getName(funDec->firstChild);
    if (!fn) return;
    Symbol fs = symtabLookup(globalST, fn);
    if (!fs || fs->kind != SYM_FUNC) return;

    SymTab* fst = symtabNew(globalST);
    for (Param p = fs->params; p; p = p->next) {
        Symbol sv = symNewVar(p->name, p->type, funDec->firstChild->lineno);
        symtabInsert(fst, sv);
    }

    if (!compSt) { symtabFree(fst); return; }
    Node *lc = compSt->firstChild, *dl = lc ? lc->nextSibling : NULL, *sl;
    if (dl && !strcmp(dl->name, "DefList")) { sl = dl->nextSibling; checkDefs(dl, fst); }
    else { sl = dl; }
    if (sl && !strcmp(sl->name, "StmtList")) checkStmts(sl, fst, retType);
    symtabFree(fst);
}

static void checkDefs(Node* n, SymTab* st) {
    if (!n) return;
    for (Node* def = n->firstChild; def; def = def->nextSibling) {
        if (!strcmp(def->name, "DefList")) { checkDefs(def, st); continue; }
        if (strcmp(def->name, "Def")) continue;
        Node *spec = def->firstChild, *dl = spec ? spec->nextSibling : NULL;
        if (!dl) continue;
        Type bt = resolveSpecifier(spec, globalST, 0);
        if (!bt) continue;
        checkDecList(dl, bt, st);
    }
}
static void checkDecList(Node* n, Type bt, SymTab* st) {
    if (!n) return;
    for (Node* dec = n->firstChild; dec; dec = dec->nextSibling) {
        if (!strcmp(dec->name, "DecList")) { checkDecList(dec, bt, st); continue; }
        if (strcmp(dec->name, "Dec")) continue;
        Node *vd = dec->firstChild;
        if (!vd) continue;
        char* vn = NULL; Type vt = resolveVarDec(vd, bt, &vn);
        if (!vn) { if (vt) free(vt); continue; }
        if (symtabLookup(st, vn)) {
            char m[256]; snprintf(m, sizeof(m), "Redefined variable \"%s\"", vn);
            semError(3, vd->lineno, m);
            free(vn); if (vt) free(vt); continue;
        }
        Symbol sv = symNewVar(vn, vt, vd->lineno);
        if (vt) free(vt);
        symtabInsert(st, sv);

        Node* ao = vd->nextSibling;
        if (ao && !strcmp(ao->name, "ASSIGNOP")) {
            Node* ex = ao->nextSibling;
            if (ex) {
                int il; Type et = checkExp(ex, st, &il);
                vt = resolveVarDec(vd, bt, NULL);
                if (vt && et && !typeEqual(vt, et))
                    semError(5, ex->lineno, "Type mismatched for assignment");
                if (vt) free(vt);
            }
        }
        free(vn);
    }
}

/* ==================== 语句 ==================== */
static int checkStmts(Node* n, SymTab* st, Type retType) {
    int hr = 0;
    if (!n) return 0;
    for (Node* s = n->firstChild; s; s = s->nextSibling) {
        if (!strcmp(s->name, "StmtList")) hr |= checkStmts(s, st, retType);
        else if (!strcmp(s->name, "Stmt") && checkStmt(s, st, retType)) hr = 1;
    }
    return hr;
}
static int checkStmt(Node* n, SymTab* st, Type retType) {
    if (!n || !n->firstChild) return 0;
    Node* f = n->firstChild;

    if (!strcmp(f->name, "RETURN")) {
        Node* ex = f->nextSibling;
        if (ex) { int il; Type et = checkExp(ex, st, &il); if (et && retType && !typeEqual(et, retType)) semError(8, ex->lineno, "Type mismatched for return"); }
        return 1;
    }
    if (!strcmp(f->name, "IF")) {
        Node *lp = f->nextSibling, *cd = lp ? lp->nextSibling : NULL;
        Node *rp = cd ? cd->nextSibling : NULL, *ts = rp ? rp->nextSibling : NULL;
        Node *ek = ts ? ts->nextSibling : NULL, *es = (ek && !strcmp(ek->name, "ELSE")) ? ek->nextSibling : NULL;
        int r1 = 0, r2 = 0;
        if (cd) { int il; Type ct = checkExp(cd, st, &il); if (ct && !typeIsInt(ct)) semError(7, cd->lineno, "Type mismatched for operands"); }
        if (ts && !strcmp(ts->name, "Stmt")) r1 = checkStmt(ts, st, retType);
        if (es && !strcmp(es->name, "Stmt")) r2 = checkStmt(es, st, retType);
        return r1 && r2;
    }
    if (!strcmp(f->name, "WHILE")) {
        Node *lp = f->nextSibling, *cd = lp ? lp->nextSibling : NULL;
        Node *rp = cd ? cd->nextSibling : NULL, *bd = rp ? rp->nextSibling : NULL;
        if (cd) { int il; Type ct = checkExp(cd, st, &il); if (ct && !typeIsInt(ct)) semError(7, cd->lineno, "Type mismatched for operands"); }
        if (bd && !strcmp(bd->name, "Stmt")) return checkStmt(bd, st, retType);
        return 0;
    }
    if (!strcmp(f->name, "CompSt")) {
        Node *lc = f->firstChild, *dl = lc ? lc->nextSibling : NULL, *sl;
        if (dl && !strcmp(dl->name, "DefList")) { sl = dl->nextSibling; } else { sl = dl; }
        SymTab* is = symtabNew(st);
        int hr = 0;
        if (dl && !strcmp(dl->name, "DefList")) checkDefs(dl, is);
        if (sl && !strcmp(sl->name, "StmtList")) hr = checkStmts(sl, is, retType);
        symtabFree(is);
        return hr;
    }
    if (!strcmp(f->name, "Exp")) { int il; checkExp(f, st, &il); return 0; }
    return 0;
}

/* ==================== 表达式检查 ==================== */
static Type checkExp(Node* n, SymTab* st, int* isLval) {
    if (isLval) *isLval = 0;
    if (!n || !n->firstChild) return NULL;
    Node *f = n->firstChild;

    if (f->isToken) {
        const char* nm = f->name;
        if (!strcmp(nm, "INT")) return typeBasic(0);
        if (!strcmp(nm, "FLOAT")) return typeBasic(1);
        if (!strcmp(nm, "ID")) {
            if (f->nextSibling) goto check_compound;
            const char* id = f->value;
            Symbol sy = symtabLookupAll(st, id);
            if (!sy) { char m[256]; snprintf(m, sizeof(m), "Undefined variable \"%s\"", id); semError(1, f->lineno, m); return NULL; }
            if (sy->kind == SYM_VAR) { if (isLval) *isLval = 1; return sy->type; }
            semError(1, f->lineno, "Undefined variable \"\""); return NULL;
        }
        return NULL;
    }

check_compound:
    if (!strcmp(f->name, "LP")) return checkExp(f->nextSibling, st, isLval);
    if (!strcmp(f->name, "MINUS")) { Type t = checkExp(f->nextSibling, st, NULL); if (isLval) *isLval = 0; if (t && !typeIsArith(t)) semError(7, f->lineno, "Type mismatched for operands"); return t; }
    if (!strcmp(f->name, "NOT"))   { Type t = checkExp(f->nextSibling, st, NULL); if (isLval) *isLval = 0; if (t && !typeIsInt(t)) semError(7, f->lineno, "Type mismatched for operands"); return typeBasic(0); }

    if (!strcmp(f->name, "ID")) {
        const char* id = f->value;
        if (isLval) *isLval = 0;
        Symbol sy = symtabLookupAll(st, id);
        if (!sy) { char m[256]; snprintf(m, sizeof(m), "Undefined function \"%s\"", id); semError(2, f->lineno, m); return NULL; }
        if (sy->kind != SYM_FUNC) { char m[256]; snprintf(m, sizeof(m), "\"%s\" is not a function", id); semError(11, f->lineno, m); return NULL; }
        Node *lp = f->nextSibling, *args = lp ? lp->nextSibling : NULL;
        if (args && !strcmp(args->name, "Args")) checkArgs(args, st, sy->params, f->lineno, id);
        else if (sy->paramCount > 0) { char m[256]; snprintf(m, sizeof(m), "Function \"%s()\" is not applicable for arguments \"()\"", id); semError(9, f->lineno, m); }
        return sy->type;
    }

    if (strcmp(f->name, "Exp")) return NULL;
    Node *op = f->nextSibling, *re = op ? op->nextSibling : NULL;
    if (!op || !re) return NULL;

    int ll, rl; Type lt, rt;

    if (!strcmp(op->name, "LB")) {
        lt = checkExp(f, st, &ll);
        if (!lt) return NULL;
        if (!typeIsArray(lt)) { char m[256]; snprintf(m, sizeof(m), "\"%s\" is not an array", expText(f)); semError(10, f->lineno, m); return NULL; }
        { int i2; Type it = checkExp(re, st, &i2); if (it && !typeIsInt(it)) { char m[256]; snprintf(m, sizeof(m), "\"%s\" is not an integer", expText(re)); semError(12, re->lineno, m); } }
        if (isLval) *isLval = 1;
        return lt->array.elem;
    }
    if (!strcmp(op->name, "DOT")) {
        lt = checkExp(f, st, &ll);
        if (!lt) return NULL;
        if (!typeIsStruct(lt)) { semError(13, f->lineno, "Illegal use of \".\""); return NULL; }
        for (FieldList fl = lt->structure; fl; fl = fl->tail)
            if (!strcmp(fl->name, re->value)) { if (isLval) *isLval = 1; return fl->type; }
        char m[256]; snprintf(m, sizeof(m), "Non-existent field \"%s\"", re->value);
        semError(14, re->lineno, m);
        return NULL;
    }

    if (isLval) *isLval = 0;
    lt = checkExp(f, st, &ll);
    rt = checkExp(re, st, &rl);

    if (!strcmp(op->name, "ASSIGNOP")) {
        if (lt && !ll) semError(6, f->lineno, "The left-hand side of an assignment must be a variable");
        if (lt && rt && !typeEqual(lt, rt)) semError(5, re->lineno, "Type mismatched for assignment");
        return lt;
    }
    if (!strcmp(op->name, "AND") || !strcmp(op->name, "OR")) {
        if (!typeIsInt(lt) || !typeIsInt(rt)) semError(7, (!typeIsInt(lt)?f:re)->lineno, "Type mismatched for operands");
        return typeBasic(0);
    }
    if (!strcmp(op->name, "RELOP")) {
        if (!typeIsArith(lt) || !typeIsArith(rt) || !typeEqual(lt, rt)) semError(7, re->lineno, "Type mismatched for operands");
        return typeBasic(0);
    }
    if (!strcmp(op->name, "PLUS") || !strcmp(op->name, "MINUS") || !strcmp(op->name, "STAR") || !strcmp(op->name, "DIV")) {
        if (!typeIsArith(lt) || !typeIsArith(rt)) semError(7, (!typeIsArith(lt)?f:re)->lineno, "Type mismatched for operands");
        else if (!typeEqual(lt, rt)) semError(7, re->lineno, "Type mismatched for operands");
        return lt;
    }
    return lt;
}

/* ==================== 参数检查 ==================== */
static int countArgs(Node* n) {
    int c = 0;
    if (!n) return 0;
    for (Node* p = n->firstChild; p; p = p->nextSibling) { if (!strcmp(p->name, "Exp")) c++; else if (!strcmp(p->name, "Args")) c += countArgs(p); }
    return c;
}
static void collectArgs(Node* n, SymTab* st, Type* at, int* al, int* idx) {
    if (!n) return;
    for (Node* p = n->firstChild; p; p = p->nextSibling) { if (!strcmp(p->name, "Exp")) { int d; at[*idx] = checkExp(p, st, &d); al[*idx] = p->lineno; (*idx)++; } else if (!strcmp(p->name, "Args")) collectArgs(p, st, at, al, idx); }
}
static void checkArgs(Node* n, SymTab* st, Param exp, int lineno, const char* fn) {
    if (!n) return;
    int ac = countArgs(n), pc = 0; for (Param ep = exp; ep; ep = ep->next) pc++;
    Type* at = NULL; int* al = NULL;
    if (ac > 0) { at = (Type*)malloc(ac * sizeof(Type)); al = (int*)malloc(ac * sizeof(int)); int i = 0; collectArgs(n, st, at, al, &i); }
    if (ac != pc) {
        char fp[256] = "", ap[256] = "", buf[1024];
        snprintf(fp, sizeof(fp), "%s(", fn);
        for (Param ep = exp; ep; ep = ep->next) { char t[64]; snprintf(t, sizeof(t), "%s%s", ep == exp ? "" : ", ", ep->type && ep->type->kind == BASIC ? (ep->type->basic == 0 ? "int" : "float") : "..."); strncat(fp, t, sizeof(fp) - strlen(fp) - 1); }
        strncat(fp, ")", sizeof(fp) - strlen(fp) - 1);
        snprintf(ap, sizeof(ap), "(");
        for (int i = 0; i < ac; i++) { char t[64]; const char* ts = "?"; if (at[i] && at[i]->kind == BASIC) ts = at[i]->basic == 0 ? "int" : "float"; snprintf(t, sizeof(t), "%s%s", i == 0 ? "" : ", ", ts); strncat(ap, t, sizeof(ap) - strlen(ap) - 1); }
        strncat(ap, ")", sizeof(ap) - strlen(ap) - 1);
        snprintf(buf, sizeof(buf), "Function \"%s\" is not applicable for arguments \"%s\"", fp, ap);
        semError(9, lineno, buf);
        free(at); free(al); return;
    }
    Param ep = exp;
    for (int i = 0; i < ac && ep; i++, ep = ep->next) {
        if (at[i] && ep->type && !typeEqual(at[i], ep->type)) {
            char fp[256] = "", ap[256] = "", buf[1024];
            snprintf(fp, sizeof(fp), "%s(", fn);
            for (ep = exp; ep; ep = ep->next) { char t[64]; snprintf(t, sizeof(t), "%s%s", ep == exp ? "" : ", ", ep->type && ep->type->kind == BASIC ? (ep->type->basic == 0 ? "int" : "float") : "..."); strncat(fp, t, sizeof(fp) - strlen(fp) - 1); }
            strncat(fp, ")", sizeof(fp) - strlen(fp) - 1);
            snprintf(ap, sizeof(ap), "(");
            for (int j = 0; j < ac; j++) { char t[64]; const char* ts = "?"; if (at[j] && at[j]->kind == BASIC) ts = at[j]->basic == 0 ? "int" : "float"; snprintf(t, sizeof(t), "%s%s", j == 0 ? "" : ", ", ts); strncat(ap, t, sizeof(ap) - strlen(ap) - 1); }
            strncat(ap, ")", sizeof(ap) - strlen(ap) - 1);
            snprintf(buf, sizeof(buf), "Function \"%s\" is not applicable for arguments \"%s\"", fp, ap);
            semError(9, al[i], buf);
            break;
        }
    }
    free(at); free(al);
}

/* ==================== 3.1 type 18 ==================== */
static void checkAllDefined(void) {
    for (int i = 0; i < HASH_SIZE; i++)
        for (Symbol s = globalST->table[i]; s; s = s->next)
            if (s->kind == SYM_FUNC && !s->defined) {
                char m[256]; snprintf(m, sizeof(m), "Undefined function \"%s\"", s->name);
                semError(18, s->lineno, m);
            }
}

/* ==================== 主入口 ==================== */
int semanticCheck(Node* root) {
    if (!root) return 0;
    errCount = 0;
    globalST = symtabNew(NULL);
    Node* edl = root->firstChild;
    if (!edl || strcmp(edl->name, "ExtDefList")) { symtabFree(globalST); globalST = NULL; return 0; }

    collectExtDefList(edl);
    checkExtDefs(edl, globalST);
    checkAllDefined();
    printErrors();

    symtabFree(globalST); globalST = NULL;
    return errCount;
}
