#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ir.h"

static char *dup_str(const char *s) {
    if (!s) return NULL;
    char *p = malloc(strlen(s) + 1);
    strcpy(p, s);
    return p;
}

/* ============================================================
 *  Operand constructors
 * ============================================================ */
static int temp_no = 0, label_no = 0, var_no = 0;

Operand *new_temp(void) {
    Operand *o = calloc(1, sizeof(Operand));
    o->kind = OPD_TEMP; o->no = ++temp_no; return o;
}
Operand *new_label_op(void) {
    Operand *o = calloc(1, sizeof(Operand));
    o->kind = OPD_LABEL; o->no = ++label_no; return o;
}
Operand *new_var_op(void) {
    Operand *o = calloc(1, sizeof(Operand));
    o->kind = OPD_VARIABLE; o->no = ++var_no; return o;
}
Operand *const_op(int value) {
    Operand *o = calloc(1, sizeof(Operand));
    o->kind = OPD_CONSTANT; o->value = value; return o;
}
Operand *addr_op(Operand *var) {
    Operand *o = calloc(1, sizeof(Operand));
    o->kind = OPD_ADDRESS; o->no = var->no; return o;
}
Operand *func_op(const char *name) {
    Operand *o = calloc(1, sizeof(Operand));
    o->kind = OPD_FUNCTION; o->name = dup_str(name); return o;
}

/* ============================================================
 *  Intermediate code list
 * ============================================================ */
static InterCode *ir_head = NULL, *ir_tail = NULL;

InterCode *ir_get_head(void) { return ir_head; }

static InterCode *emit(IRKind kind) {
    InterCode *c = calloc(1, sizeof(InterCode));
    c->kind = kind;
    if (!ir_head) { ir_head = ir_tail = c; }
    else { ir_tail->next = c; c->prev = ir_tail; ir_tail = c; }
    return c;
}
static void emit_assign(Operand *x, Operand *y) {
    InterCode *c = emit(IR_ASSIGN); c->result = x; c->arg1 = y;
}
static void emit_arith(IRKind k, Operand *x, Operand *y, Operand *z) {
    InterCode *c = emit(k); c->result = x; c->arg1 = y; c->arg2 = z;
}
static void emit_label(Operand *l) { emit(IR_LABEL)->arg1 = l; }
static void emit_goto(Operand *l)  { emit(IR_GOTO)->arg1 = l; }

/* ============================================================
 *  Type system
 * ============================================================ */
typedef enum { TYPE_INT, TYPE_ARRAY, TYPE_STRUCT } TypeKind;

typedef struct FieldList {
    char *name;
    struct Type *type;
    int offset;
    struct FieldList *next;
} FieldList;

typedef struct Type {
    TypeKind kind;
    int width;                 /* total size in bytes */
    struct Type *elem;         /* element type (array) */
    int array_size;            /* number of elements (array) */
    FieldList *fields;         /* fields (struct) */
} Type;

static Type *type_int(void) {
    static Type *t = NULL;
    if (!t) { t = calloc(1, sizeof(Type)); t->kind = TYPE_INT; t->width = 4; }
    return t;
}
static Type *array_type(int n, Type *elem) {
    Type *t = calloc(1, sizeof(Type));
    t->kind = TYPE_ARRAY; t->elem = elem; t->array_size = n;
    t->width = n * elem->width;
    return t;
}
static int is_aggregate(Type *t) {
    return t && (t->kind == TYPE_ARRAY || t->kind == TYPE_STRUCT);
}
static FieldList *find_field(Type *st, const char *name) {
    for (FieldList *f = st ? st->fields : NULL; f; f = f->next)
        if (strcmp(f->name, name) == 0) return f;
    return NULL;
}

/* Struct definition table : tag name -> struct Type */
typedef struct StructDef {
    char *name;
    Type *type;
    struct StructDef *next;
} StructDef;
static StructDef *struct_head = NULL;

static void register_struct(const char *name, Type *t) {
    StructDef *s = calloc(1, sizeof(StructDef));
    s->name = dup_str(name); s->type = t;
    s->next = struct_head; struct_head = s;
}
static Type *lookup_struct(const char *name) {
    for (StructDef *s = struct_head; s; s = s->next)
        if (strcmp(s->name, name) == 0) return s->type;
    return NULL;
}

/* ============================================================
 *  Symbol table (flat, names are globally unique)
 * ============================================================ */
typedef struct VarSym {
    char *name;
    Operand *op;
    Type *type;
    int is_param_ref;          /* aggregate parameter: op already holds an address */
    struct VarSym *next;
} VarSym;

static VarSym *sym_head = NULL;

static VarSym *lookup(const char *name) {
    if (!name) return NULL;
    for (VarSym *p = sym_head; p; p = p->next)
        if (strcmp(p->name, name) == 0) return p;
    return NULL;
}
static VarSym *declare(const char *name, Type *type, int is_param_ref) {
    VarSym *s = calloc(1, sizeof(VarSym));
    s->name = dup_str(name);
    s->op = new_var_op();
    s->type = type;
    s->is_param_ref = is_param_ref;
    s->next = sym_head; sym_head = s;
    return s;
}

/* Set when an internal error happens (kept as a safety net). */
static int cannot_translate = 0;

/* ============================================================
 *  Forward declarations
 * ============================================================ */
static Operand *translate_Exp(Node *exp);
static void translate_Cond(Node *exp, Operand *Ltrue, Operand *Lfalse);
static void translate_Stmt(Node *stmt);
static void translate_CompSt(Node *compst);
static Operand *translate_lvalue_addr(Node *exp, Type **out_type);
static Type *specifier_type(Node *spec);
static Type *vardec_type(Node *vardec, Type *base, const char **name);

/* ============================================================
 *  Small helpers
 * ============================================================ */
static int child_is(Node *n, int k, const char *name) {
    Node *c = nth_child(n, k);
    return c && strcmp(c->name, name) == 0;
}
static const char *exp_id_name(Node *exp) { return nth_child(exp, 0)->text; }

/* Make sure an operand can stand where a variable/temp is required. */
static Operand *ensure_var(Operand *op) {
    if (op->kind == OPD_CONSTANT) {
        Operand *t = new_temp(); emit_assign(t, op); return t;
    }
    return op;
}

/* The base address of an aggregate variable. */
static Operand *base_address(VarSym *vs) {
    if (vs->is_param_ref) return vs->op;     /* op already holds the address */
    return addr_op(vs->op);                  /* &v for a local aggregate */
}

/* An address operand usable in `*x` must be a plain variable / temp. */
static Operand *deref_ready(Operand *addr) {
    if (addr->kind == OPD_ADDRESS || addr->kind == OPD_CONSTANT) {
        Operand *t = new_temp(); emit_assign(t, addr); return t;
    }
    return addr;
}

/* ============================================================
 *  Static type of an expression (enough to know if it is aggregate)
 * ============================================================ */
static Type *exp_static_type(Node *exp) {
    int n = num_children(exp);
    if (n == 1) {
        Node *c = nth_child(exp, 0);
        if (strcmp(c->name, "ID") == 0) {
            VarSym *vs = lookup(c->text);
            return vs ? vs->type : type_int();
        }
        return type_int();                       /* INT / FLOAT */
    }
    if (n == 3 && child_is(exp, 1, "DOT")) {
        Type *it = exp_static_type(nth_child(exp, 0));
        FieldList *f = find_field(it, nth_child(exp, 2)->text);
        return f ? f->type : type_int();
    }
    if (n == 4 && child_is(exp, 1, "LB")) {
        Type *it = exp_static_type(nth_child(exp, 0));
        return (it && it->kind == TYPE_ARRAY) ? it->elem : type_int();
    }
    if (n == 3 && child_is(exp, 0, "LP"))
        return exp_static_type(nth_child(exp, 1));
    return type_int();
}

/* ============================================================
 *  Address of an l-value (variable / array element / struct field)
 * ============================================================ */
static Operand *translate_lvalue_addr(Node *exp, Type **out_type) {
    int n = num_children(exp);

    if (n == 1 && child_is(exp, 0, "ID")) {       /* a variable */
        VarSym *vs = lookup(exp_id_name(exp));
        if (!vs) { cannot_translate = 1; *out_type = type_int(); return new_temp(); }
        *out_type = vs->type;
        return base_address(vs);
    }

    if (n == 3 && child_is(exp, 0, "LP"))          /* ( Exp ) */
        return translate_lvalue_addr(nth_child(exp, 1), out_type);

    if (n == 4 && child_is(exp, 1, "LB")) {        /* Exp [ Exp ] */
        Type *it;
        Operand *base = translate_lvalue_addr(nth_child(exp, 0), &it);
        Type *elem = (it && it->kind == TYPE_ARRAY) ? it->elem : type_int();
        *out_type = elem;

        Operand *index = translate_Exp(nth_child(exp, 2));
        Operand *offset;
        if (index->kind == OPD_CONSTANT)
            offset = const_op(index->value * elem->width);
        else {
            offset = new_temp();
            emit_arith(IR_MUL, offset, index, const_op(elem->width));
        }
        if (offset->kind == OPD_CONSTANT && offset->value == 0) return base;
        Operand *addr = new_temp();
        emit_arith(IR_ADD, addr, base, offset);
        return addr;
    }

    if (n == 3 && child_is(exp, 1, "DOT")) {       /* Exp . ID */
        Type *it;
        Operand *base = translate_lvalue_addr(nth_child(exp, 0), &it);
        FieldList *f = find_field(it, nth_child(exp, 2)->text);
        if (!f) { cannot_translate = 1; *out_type = type_int(); return base; }
        *out_type = f->type;
        if (f->offset == 0) return base;
        Operand *addr = new_temp();
        emit_arith(IR_ADD, addr, base, const_op(f->offset));
        return addr;
    }

    cannot_translate = 1;
    *out_type = type_int();
    return new_temp();
}

/* Read the value stored at an l-value. */
static Operand *load_lvalue(Node *exp) {
    Type *t;
    Operand *addr = deref_ready(translate_lvalue_addr(exp, &t));
    Operand *r = new_temp();
    InterCode *c = emit(IR_LOAD); c->result = r; c->arg1 = addr;
    return r;
}

/* Store the value of `rhs` into the l-value `lhs`. */
static Operand *store_lvalue(Node *lhs, Node *rhs) {
    Type *t;
    Operand *addr = deref_ready(translate_lvalue_addr(lhs, &t));
    Operand *r = translate_Exp(rhs);
    InterCode *c = emit(IR_STORE); c->result = addr; c->arg1 = r;
    return r;
}

/* ============================================================
 *  Function-call argument collection
 * ============================================================ */
/* An aggregate is passed by reference (its address); a scalar by value. */
static Operand *translate_arg(Node *exp) {
    Type *t = exp_static_type(exp);
    if (is_aggregate(t)) {
        Type *tt;
        return translate_lvalue_addr(exp, &tt);
    }
    return translate_Exp(exp);
}

static void collect_args(Node *args, Operand **arr, int *cnt) {
    /* Args : Exp COMMA Args | Exp */
    arr[(*cnt)++] = translate_arg(nth_child(args, 0));
    Node *rest = child_named(args, "Args");
    if (rest) collect_args(rest, arr, cnt);
}

/* ============================================================
 *  translate_Exp : translate an expression, return its value operand
 * ============================================================ */
static Operand *translate_Exp(Node *exp) {
    if (cannot_translate) return const_op(0);
    int n = num_children(exp);

    if (n == 1) {
        Node *c = nth_child(exp, 0);
        if (strcmp(c->name, "INT") == 0)   return const_op(c->ival);
        if (strcmp(c->name, "FLOAT") == 0) return const_op(0);
        if (strcmp(c->name, "ID") == 0) {
            VarSym *vs = lookup(c->text);
            return vs ? vs->op : new_var_op();
        }
    }

    if (n == 2) {
        if (child_is(exp, 0, "MINUS")) {           /* MINUS Exp */
            Operand *r = translate_Exp(nth_child(exp, 1));
            if (r->kind == OPD_CONSTANT) return const_op(-r->value);
            Operand *t = new_temp();
            emit_arith(IR_SUB, t, const_op(0), r);
            return t;
        }
        if (child_is(exp, 0, "NOT")) goto bool_value;   /* NOT Exp */
    }

    if (n == 3) {
        Node *mid = nth_child(exp, 1);

        if (strcmp(mid->name, "ASSIGNOP") == 0) {      /* Exp = Exp */
            Node *lhs = nth_child(exp, 0);
            Node *rhs = nth_child(exp, 2);
            if (num_children(lhs) == 1 && child_is(lhs, 0, "ID")) {
                VarSym *vs = lookup(exp_id_name(lhs));
                Operand *r = translate_Exp(rhs);
                emit_assign(vs->op, r);
                return vs->op;
            }
            return store_lvalue(lhs, rhs);             /* array/struct l-value */
        }

        if (strcmp(mid->name, "PLUS") == 0 || strcmp(mid->name, "MINUS") == 0 ||
            strcmp(mid->name, "STAR") == 0 || strcmp(mid->name, "DIV") == 0) {
            Operand *l = translate_Exp(nth_child(exp, 0));
            Operand *r = translate_Exp(nth_child(exp, 2));
            IRKind k = strcmp(mid->name, "PLUS") == 0 ? IR_ADD :
                       strcmp(mid->name, "MINUS") == 0 ? IR_SUB :
                       strcmp(mid->name, "STAR") == 0 ? IR_MUL : IR_DIV;
            if (l->kind == OPD_CONSTANT && r->kind == OPD_CONSTANT) {
                switch (k) {
                    case IR_ADD: return const_op(l->value + r->value);
                    case IR_SUB: return const_op(l->value - r->value);
                    case IR_MUL: return const_op(l->value * r->value);
                    case IR_DIV: if (r->value) return const_op(l->value / r->value); break;
                    default: break;
                }
            }
            Operand *t = new_temp();
            emit_arith(k, t, l, r);
            return t;
        }

        if (strcmp(mid->name, "RELOP") == 0 ||
            strcmp(mid->name, "AND") == 0 ||
            strcmp(mid->name, "OR") == 0) goto bool_value;

        if (child_is(exp, 0, "LP"))                    /* ( Exp ) */
            return translate_Exp(nth_child(exp, 1));

        if (child_is(exp, 0, "ID") && child_is(exp, 1, "LP")) {  /* ID ( ) */
            const char *fname = nth_child(exp, 0)->text;
            if (strcmp(fname, "read") == 0) {
                Operand *t = new_temp();
                emit(IR_READ)->arg1 = t;
                return t;
            }
            Operand *t = new_temp();
            InterCode *c = emit(IR_CALL);
            c->result = t; c->arg1 = func_op(fname);
            return t;
        }

        if (strcmp(mid->name, "DOT") == 0)             /* Exp . ID  (read field) */
            return load_lvalue(exp);
    }

    if (n == 4) {
        if (child_is(exp, 0, "ID") && child_is(exp, 1, "LP")) {  /* ID ( Args ) */
            const char *fname = nth_child(exp, 0)->text;
            Node *args = nth_child(exp, 2);
            Operand *argv[128]; int argc = 0;
            collect_args(args, argv, &argc);

            if (strcmp(fname, "write") == 0) {
                Operand *a = ensure_var(argv[0]);
                emit(IR_WRITE)->arg1 = a;
                return const_op(0);
            }
            for (int i = argc - 1; i >= 0; i--)
                emit(IR_ARG)->arg1 = argv[i];
            Operand *t = new_temp();
            InterCode *c = emit(IR_CALL);
            c->result = t; c->arg1 = func_op(fname);
            return t;
        }
        if (child_is(exp, 1, "LB"))                    /* Exp [ Exp ]  (read element) */
            return load_lvalue(exp);
    }

    return const_op(0);

bool_value:;
    Operand *place = new_temp();
    Operand *L1 = new_label_op(), *L2 = new_label_op();
    emit_assign(place, const_op(0));
    translate_Cond(exp, L1, L2);
    emit_label(L1);
    emit_assign(place, const_op(1));
    emit_label(L2);
    return place;
}

/* ============================================================
 *  translate_Cond
 * ============================================================ */
static void translate_Cond(Node *exp, Operand *Ltrue, Operand *Lfalse) {
    if (cannot_translate) return;
    int n = num_children(exp);

    if (n == 3) {
        Node *mid = nth_child(exp, 1);
        if (strcmp(mid->name, "RELOP") == 0) {
            Operand *l = translate_Exp(nth_child(exp, 0));
            Operand *r = translate_Exp(nth_child(exp, 2));
            InterCode *c = emit(IR_IF);
            c->arg1 = l; c->relop = mid->text; c->arg2 = r; c->result = Ltrue;
            emit_goto(Lfalse);
            return;
        }
        if (strcmp(mid->name, "AND") == 0) {
            Operand *L1 = new_label_op();
            translate_Cond(nth_child(exp, 0), L1, Lfalse);
            emit_label(L1);
            translate_Cond(nth_child(exp, 2), Ltrue, Lfalse);
            return;
        }
        if (strcmp(mid->name, "OR") == 0) {
            Operand *L1 = new_label_op();
            translate_Cond(nth_child(exp, 0), Ltrue, L1);
            emit_label(L1);
            translate_Cond(nth_child(exp, 2), Ltrue, Lfalse);
            return;
        }
        if (child_is(exp, 0, "LP")) {
            translate_Cond(nth_child(exp, 1), Ltrue, Lfalse);
            return;
        }
    }
    if (n == 2 && child_is(exp, 0, "NOT")) {
        translate_Cond(nth_child(exp, 1), Lfalse, Ltrue);
        return;
    }

    Operand *t = translate_Exp(exp);
    InterCode *c = emit(IR_IF);
    c->arg1 = t; c->relop = "!="; c->arg2 = const_op(0); c->result = Ltrue;
    emit_goto(Lfalse);
}

/* ============================================================
 *  translate_Stmt
 * ============================================================ */
static void translate_Stmt(Node *stmt) {
    if (cannot_translate) return;
    Node *first = nth_child(stmt, 0);
    const char *fn = first->name;

    if (strcmp(fn, "Exp") == 0) { translate_Exp(first); return; }
    if (strcmp(fn, "CompSt") == 0) { translate_CompSt(first); return; }
    if (strcmp(fn, "RETURN") == 0) {
        Operand *r = translate_Exp(nth_child(stmt, 1));
        emit(IR_RETURN)->arg1 = r;
        return;
    }
    if (strcmp(fn, "IF") == 0) {
        int n = num_children(stmt);
        Node *cond = nth_child(stmt, 2);
        Node *then_s = nth_child(stmt, 4);
        if (n == 5) {
            Operand *L1 = new_label_op(), *L2 = new_label_op();
            translate_Cond(cond, L1, L2);
            emit_label(L1); translate_Stmt(then_s); emit_label(L2);
        } else {
            Node *else_s = nth_child(stmt, 6);
            Operand *L1 = new_label_op(), *L2 = new_label_op(), *L3 = new_label_op();
            translate_Cond(cond, L1, L2);
            emit_label(L1); translate_Stmt(then_s); emit_goto(L3);
            emit_label(L2); translate_Stmt(else_s);
            emit_label(L3);
        }
        return;
    }
    if (strcmp(fn, "WHILE") == 0) {
        Node *cond = nth_child(stmt, 2);
        Node *body = nth_child(stmt, 4);
        Operand *L1 = new_label_op(), *L2 = new_label_op(), *L3 = new_label_op();
        emit_label(L1);
        translate_Cond(cond, L2, L3);
        emit_label(L2); translate_Stmt(body); emit_goto(L1);
        emit_label(L3);
        return;
    }
}

/* ============================================================
 *  Types : from Specifier / VarDec / struct body
 * ============================================================ */
static Type *vardec_type(Node *vardec, Type *base, const char **name) {
    /* VarDec : ID  |  VarDec LB INT RB */
    if (num_children(vardec) == 1) {
        if (name) *name = nth_child(vardec, 0)->text;
        return base;
    }
    int sz = nth_child(vardec, 2)->ival;
    return vardec_type(nth_child(vardec, 0), array_type(sz, base), name);
}

static Type *build_struct_type(Node *deflist) {
    Type *st = calloc(1, sizeof(Type));
    st->kind = TYPE_STRUCT;
    int offset = 0;
    FieldList **tail = &st->fields;

    for (; deflist && num_children(deflist) > 0; deflist = child_named(deflist, "DefList")) {
        Node *def = nth_child(deflist, 0);             /* Def : Specifier DecList SEMI */
        Type *base = specifier_type(nth_child(def, 0));
        Node *declist = nth_child(def, 1);
        for (; declist; declist = child_named(declist, "DecList")) {
            Node *dec = nth_child(declist, 0);         /* Dec : VarDec [ASSIGNOP Exp] */
            const char *fname;
            Type *ftype = vardec_type(nth_child(dec, 0), base, &fname);
            FieldList *f = calloc(1, sizeof(FieldList));
            f->name = dup_str(fname); f->type = ftype; f->offset = offset;
            *tail = f; tail = &f->next;
            offset += ftype->width;
        }
    }
    st->width = offset;
    return st;
}

static Type *struct_specifier_type(Node *ss) {
    /* StructSpecifier : STRUCT OptTag LC DefList RC | STRUCT Tag */
    if (num_children(ss) == 2) {                       /* STRUCT Tag */
        Node *tag = nth_child(ss, 1);
        Type *t = lookup_struct(nth_child(tag, 0)->text);
        if (!t) { cannot_translate = 1; return type_int(); }
        return t;
    }
    Type *st = build_struct_type(child_named(ss, "DefList"));
    Node *opttag = nth_child(ss, 1);                   /* OptTag : ID | empty */
    if (num_children(opttag) == 1)
        register_struct(nth_child(opttag, 0)->text, st);
    return st;
}

static Type *specifier_type(Node *spec) {
    /* Specifier : TYPE | StructSpecifier */
    Node *c = nth_child(spec, 0);
    if (strcmp(c->name, "TYPE") == 0) return type_int();
    return struct_specifier_type(c);
}

/* ============================================================
 *  Local declarations
 * ============================================================ */
static void translate_Dec(Node *dec, Type *base) {
    /* Dec : VarDec | VarDec ASSIGNOP Exp */
    const char *name;
    Type *t = vardec_type(nth_child(dec, 0), base, &name);
    VarSym *vs = declare(name, t, 0);
    if (is_aggregate(t)) {
        InterCode *c = emit(IR_DEC); c->arg1 = vs->op; c->size = t->width;
    }
    if (num_children(dec) == 3) {                      /* scalar initializer */
        Operand *r = translate_Exp(nth_child(dec, 2));
        emit_assign(vs->op, r);
    }
}

static void translate_DefList(Node *deflist) {
    if (!deflist || num_children(deflist) == 0) return;
    Node *def = nth_child(deflist, 0);                 /* Def : Specifier DecList SEMI */
    Type *base = specifier_type(nth_child(def, 0));
    Node *declist = nth_child(def, 1);
    while (declist) {                                  /* DecList : Dec | Dec COMMA DecList */
        translate_Dec(nth_child(declist, 0), base);
        declist = child_named(declist, "DecList");
    }
    translate_DefList(child_named(deflist, "DefList"));
}

static void translate_StmtList(Node *stmtlist) {
    while (stmtlist && num_children(stmtlist) > 0) {
        translate_Stmt(nth_child(stmtlist, 0));
        stmtlist = child_named(stmtlist, "StmtList");
    }
}

static void translate_CompSt(Node *compst) {
    /* CompSt : LC DefList StmtList RC */
    translate_DefList(child_named(compst, "DefList"));
    translate_StmtList(child_named(compst, "StmtList"));
}

/* ============================================================
 *  Function definitions
 * ============================================================ */
static void translate_params(Node *varlist) {
    /* VarList : ParamDec COMMA VarList | ParamDec */
    while (varlist) {
        Node *param = nth_child(varlist, 0);           /* ParamDec : Specifier VarDec */
        Type *base = specifier_type(nth_child(param, 0));
        const char *name;
        Type *t = vardec_type(nth_child(param, 1), base, &name);
        VarSym *vs = declare(name, t, is_aggregate(t) ? 1 : 0);  /* aggregates: by ref */
        emit(IR_PARAM)->arg1 = vs->op;
        varlist = child_named(varlist, "VarList");
    }
}

static void translate_FunDef(Node *extdef) {
    /* ExtDef : Specifier FunDec CompSt */
    Node *fundec = nth_child(extdef, 1);
    Node *compst = nth_child(extdef, 2);
    const char *fname = nth_child(fundec, 0)->text;

    emit(IR_FUNCTION)->arg1 = func_op(fname);

    Node *varlist = child_named(fundec, "VarList");
    if (varlist) translate_params(varlist);

    translate_CompSt(compst);
}

/* ============================================================
 *  Program
 * ============================================================ */
void translate_program(Node *root) {
    Node *list = child_named(root, "ExtDefList");
    while (list && num_children(list) > 0) {
        Node *extdef = nth_child(list, 0);
        specifier_type(nth_child(extdef, 0));          /* register any struct definition */
        if (child_named(extdef, "FunDec"))
            translate_FunDef(extdef);
        list = child_named(list, "ExtDefList");
    }
}

/* ============================================================
 *  Conservative peephole optimisation
 * ============================================================ */
static void remove_ir(InterCode *c) {
    if (c->prev) c->prev->next = c->next; else ir_head = c->next;
    if (c->next) c->next->prev = c->prev; else ir_tail = c->prev;
}
static int label_referenced(int no) {
    for (InterCode *c = ir_head; c; c = c->next) {
        if (c->kind == IR_GOTO && c->arg1->kind == OPD_LABEL && c->arg1->no == no) return 1;
        if (c->kind == IR_IF && c->result->kind == OPD_LABEL && c->result->no == no) return 1;
    }
    return 0;
}

/* The operand an instruction defines (NULL if it defines nothing). */
static Operand *ir_dest(InterCode *c) {
    switch (c->kind) {
        case IR_ASSIGN: case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV:
        case IR_LOAD: case IR_CALL: return c->result;
        case IR_READ:               return c->arg1;
        default:                    return NULL;
    }
}
static void set_ir_dest(InterCode *c, Operand *o) {
    if (c->kind == IR_READ) c->arg1 = o; else c->result = o;
}
static int temp_uses(int no) {
    int cnt = 0;
    for (InterCode *c = ir_head; c; c = c->next) {
        Operand *ops[3] = { c->result, c->arg1, c->arg2 };
        for (int i = 0; i < 3; i++)
            if (ops[i] && ops[i]->kind == OPD_TEMP && ops[i]->no == no) cnt++;
    }
    return cnt;
}
void ir_optimize(void) {
    int changed = 1, guard = 0;
    while (changed && guard++ < 100) {
        changed = 0;
        /* (1) `GOTO Lx` directly before `LABEL Lx` */
        for (InterCode *c = ir_head; c; ) {
            InterCode *nx = c->next;
            if (c->kind == IR_GOTO && nx && nx->kind == IR_LABEL &&
                c->arg1->no == nx->arg1->no) { remove_ir(c); changed = 1; }
            c = nx;
        }
        /* (2) unreachable code after GOTO / RETURN */
        int reachable = 1;
        for (InterCode *c = ir_head; c; ) {
            InterCode *nx = c->next;
            if (c->kind == IR_LABEL || c->kind == IR_FUNCTION) reachable = 1;
            else if (!reachable) { remove_ir(c); changed = 1; c = nx; continue; }
            if (c->kind == IR_GOTO || c->kind == IR_RETURN) reachable = 0;
            c = nx;
        }
        /* (3) labels that are never targeted */
        for (InterCode *c = ir_head; c; ) {
            InterCode *nx = c->next;
            if (c->kind == IR_LABEL && !label_referenced(c->arg1->no)) { remove_ir(c); changed = 1; }
            c = nx;
        }
        /* (4) copy propagation: `t := expr` + `v := t` (t unused elsewhere) -> `v := expr` */
        for (InterCode *c = ir_head; c; c = c->next) {
            Operand *d = ir_dest(c);
            InterCode *nx = c->next;
            if (d && d->kind == OPD_TEMP && nx && nx->kind == IR_ASSIGN &&
                nx->result && nx->arg1 && nx->arg1->kind == OPD_TEMP &&
                nx->arg1->no == d->no && temp_uses(d->no) == 2) {
                set_ir_dest(c, nx->result);
                remove_ir(nx);
                changed = 1;
            }
        }
    }
}

/* ============================================================
 *  Output
 * ============================================================ */
static void print_op(FILE *out, Operand *o) {
    switch (o->kind) {
        case OPD_VARIABLE: fprintf(out, "v%d", o->no); break;
        case OPD_TEMP:     fprintf(out, "t%d", o->no); break;
        case OPD_CONSTANT: fprintf(out, "#%d", o->value); break;
        case OPD_ADDRESS:  fprintf(out, "&v%d", o->no); break;
        case OPD_LABEL:    fprintf(out, "label%d", o->no); break;
        case OPD_FUNCTION: fprintf(out, "%s", o->name); break;
    }
}

void ir_output(FILE *out) {
    if (cannot_translate) {
        fprintf(out, "Cannot translate: Code contains variables of "
                     "multi-dimensional array type or parameters of array type.\n");
        return;
    }
    ir_optimize();
    for (InterCode *c = ir_head; c; c = c->next) {
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
