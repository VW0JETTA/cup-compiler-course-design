#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "ir_opt.h"

/* ============================================================
 *  Operand utilities
 * ============================================================ */
static int op_eq(Operand *a, Operand *b) {
    if (!a || !b) return 0;
    if (a->kind != b->kind) return 0;
    if (a->kind == OPD_CONSTANT) return a->value == b->value;
    if (a->kind == OPD_FUNCTION) return a->name && b->name && strcmp(a->name, b->name) == 0;
    return a->no == b->no;
}

static Operand *op_clone(Operand *o) {
    Operand *n = calloc(1, sizeof(Operand));
    memcpy(n, o, sizeof(Operand));
    if (o->name) n->name = strdup(o->name);
    return n;
}

static Operand *make_const(int v) {
    Operand *o = calloc(1, sizeof(Operand));
    o->kind = OPD_CONSTANT; o->value = v;
    return o;
}

static Operand *make_vt(int kind, int no) {
    Operand *o = calloc(1, sizeof(Operand));
    o->kind = kind; o->no = no;
    return o;
}

static int op_is_const(Operand *o) { return o && o->kind == OPD_CONSTANT; }
static int op_is_vt(Operand *o) { return o && (o->kind == OPD_VARIABLE || o->kind == OPD_TEMP); }

static int ir_is_arith(InterCode *c) {
    return c->kind == IR_ADD || c->kind == IR_SUB || c->kind == IR_MUL || c->kind == IR_DIV;
}

static int ir_defines(InterCode *c) {
    switch (c->kind) {
        case IR_ASSIGN: case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV:
        case IR_LOAD: case IR_CALL: case IR_READ: return 1;
        default: return 0;
    }
}

/* the operand written by an instruction (or NULL) */
static Operand *ir_dest(InterCode *c) {
    switch (c->kind) {
        case IR_READ: return c->arg1;
        default: return ir_defines(c) ? c->result : NULL;
    }
}

static void remove_ir(Program *prog, InterCode *c) {
    if (c->prev) c->prev->next = c->next;
    else prog->ir_head = c->next;
    if (c->next) c->next->prev = c->prev;
    else prog->ir_tail = c->prev;
    free(c->relop);
    free(c);
}

/* insert instruction `n` immediately before `at` */
static void insert_before(Program *prog, InterCode *at, InterCode *n) {
    n->prev = at->prev;
    n->next = at;
    if (at->prev) at->prev->next = n;
    else prog->ir_head = n;
    at->prev = n;
}

/* ============================================================
 *  Algebraic simplification and constant folding of literals
 * ============================================================ */
static int algebraic_simplify(Program *prog) {
    int changed = 0;
    for (InterCode *c = prog->ir_head; c; c = c->next) {
        if (c->kind == IR_ADD) {
            if (op_is_const(c->arg1) && op_is_const(c->arg2)) {
                int v = c->arg1->value + c->arg2->value;
                c->kind = IR_ASSIGN; free(c->arg1); free(c->arg2); c->arg2 = NULL; c->arg1 = make_const(v); changed = 1;
            } else if (op_is_const(c->arg2) && c->arg2->value == 0) {
                c->kind = IR_ASSIGN; free(c->arg2); c->arg2 = NULL; changed = 1;
            } else if (op_is_const(c->arg1) && c->arg1->value == 0) {
                c->kind = IR_ASSIGN; free(c->arg1); c->arg1 = c->arg2; c->arg2 = NULL; changed = 1;
            }
        } else if (c->kind == IR_SUB) {
            if (op_is_const(c->arg1) && op_is_const(c->arg2)) {
                int v = c->arg1->value - c->arg2->value;
                c->kind = IR_ASSIGN; free(c->arg1); free(c->arg2); c->arg2 = NULL; c->arg1 = make_const(v); changed = 1;
            } else if (op_is_const(c->arg2) && c->arg2->value == 0) {
                c->kind = IR_ASSIGN; free(c->arg2); c->arg2 = NULL; changed = 1;
            }
        } else if (c->kind == IR_MUL) {
            if (op_is_const(c->arg1) && op_is_const(c->arg2)) {
                int v = c->arg1->value * c->arg2->value;
                c->kind = IR_ASSIGN; free(c->arg1); free(c->arg2); c->arg2 = NULL; c->arg1 = make_const(v); changed = 1;
            } else if (op_is_const(c->arg2) && c->arg2->value == 1) {
                c->kind = IR_ASSIGN; free(c->arg2); c->arg2 = NULL; changed = 1;
            } else if (op_is_const(c->arg1) && c->arg1->value == 1) {
                c->kind = IR_ASSIGN; free(c->arg1); c->arg1 = c->arg2; c->arg2 = NULL; changed = 1;
            } else if (op_is_const(c->arg2) && c->arg2->value == 0) {
                c->kind = IR_ASSIGN; free(c->arg1); free(c->arg2); c->arg2 = NULL; c->arg1 = make_const(0); changed = 1;
            } else if (op_is_const(c->arg1) && c->arg1->value == 0) {
                c->kind = IR_ASSIGN; free(c->arg1); free(c->arg2); c->arg2 = NULL; c->arg1 = make_const(0); changed = 1;
            }
        } else if (c->kind == IR_DIV) {
            if (op_is_const(c->arg1) && op_is_const(c->arg2) && c->arg2->value != 0) {
                int v = c->arg1->value / c->arg2->value;
                c->kind = IR_ASSIGN; free(c->arg1); free(c->arg2); c->arg2 = NULL; c->arg1 = make_const(v); changed = 1;
            } else if (op_is_const(c->arg2) && c->arg2->value == 1) {
                c->kind = IR_ASSIGN; free(c->arg2); c->arg2 = NULL; changed = 1;
            }
        }
    }
    return changed;
}

/* ============================================================
 *  IF simplification (constant condition)
 * ============================================================ */
static int if_simplify(Program *prog) {
    int changed = 0;
    for (InterCode *c = prog->ir_head; c; ) {
        InterCode *nx = c->next;
        if (c->kind == IR_IF && op_is_const(c->arg1) && op_is_const(c->arg2)) {
            int a = c->arg1->value, b = c->arg2->value, cond = 0;
            if (!strcmp(c->relop, "==")) cond = (a == b);
            else if (!strcmp(c->relop, "!=")) cond = (a != b);
            else if (!strcmp(c->relop, "<")) cond = (a < b);
            else if (!strcmp(c->relop, ">")) cond = (a > b);
            else if (!strcmp(c->relop, "<=")) cond = (a <= b);
            else if (!strcmp(c->relop, ">=")) cond = (a >= b);
            if (cond) {
                Operand *tgt = op_clone(c->result);
                free(c->arg1); free(c->arg2); free(c->relop); free(c->result);
                c->kind = IR_GOTO; c->arg1 = tgt; c->arg2 = NULL; c->relop = NULL; c->result = NULL;
            } else {
                remove_ir(prog, c);
            }
            changed = 1;
        }
        c = nx;
    }
    return changed;
}

/* ============================================================
 *  Structural / CFG cleanup
 *    - remove unreachable instructions (after GOTO/RETURN until a label)
 *    - remove `GOTO L` immediately followed by `LABEL L`
 *    - merge duplicate consecutive labels
 *    - remove NOP `x := x`
 *    - remove labels that are never referenced
 * ============================================================ */
static int label_referenced(Program *prog, int no) {
    for (InterCode *d = prog->ir_head; d; d = d->next) {
        if (d->kind == IR_GOTO && d->arg1 && d->arg1->kind == OPD_LABEL && d->arg1->no == no) return 1;
        if (d->kind == IR_IF && d->result && d->result->kind == OPD_LABEL && d->result->no == no) return 1;
    }
    return 0;
}

static int structural_cleanup(Program *prog) {
    int changed = 0;

    /* remove NOP x := x */
    for (InterCode *c = prog->ir_head; c; ) {
        InterCode *nx = c->next;
        if (c->kind == IR_ASSIGN && op_eq(c->result, c->arg1)) { remove_ir(prog, c); changed = 1; }
        c = nx;
    }

    /* unreachable code after GOTO/RETURN until next LABEL/FUNCTION */
    int reachable = 1;
    for (InterCode *c = prog->ir_head; c; ) {
        InterCode *nx = c->next;
        if (c->kind == IR_LABEL || c->kind == IR_FUNCTION) reachable = 1;
        else if (!reachable) { remove_ir(prog, c); changed = 1; c = nx; continue; }
        if (c->kind == IR_GOTO || c->kind == IR_RETURN) reachable = 0;
        c = nx;
    }

    /* GOTO L directly before LABEL L */
    for (InterCode *c = prog->ir_head; c; ) {
        InterCode *nx = c->next;
        if (c->kind == IR_GOTO && nx && nx->kind == IR_LABEL &&
            c->arg1->kind == OPD_LABEL && nx->arg1->kind == OPD_LABEL &&
            c->arg1->no == nx->arg1->no) { remove_ir(prog, c); changed = 1; }
        c = nx;
    }

    /* duplicate consecutive labels: merge nx into c */
    for (InterCode *c = prog->ir_head; c; ) {
        InterCode *nx = c->next;
        if (c->kind == IR_LABEL && nx && nx->kind == IR_LABEL) {
            for (InterCode *d = prog->ir_head; d; d = d->next) {
                if (d->kind == IR_GOTO && d->arg1->kind == OPD_LABEL && d->arg1->no == nx->arg1->no)
                    d->arg1->no = c->arg1->no;
                if (d->kind == IR_IF && d->result->kind == OPD_LABEL && d->result->no == nx->arg1->no)
                    d->result->no = c->arg1->no;
            }
            remove_ir(prog, nx); changed = 1;
            continue;
        }
        c = nx;
    }

    /* remove unreferenced labels */
    for (InterCode *c = prog->ir_head; c; ) {
        InterCode *nx = c->next;
        if (c->kind == IR_LABEL && !label_referenced(prog, c->arg1->no)) {
            remove_ir(prog, c); changed = 1;
        }
        c = nx;
    }

    return changed;
}

/* ============================================================
 *  CFG framework (rebuilt on demand)
 * ============================================================ */
#define MAXI 20000
#define MAXPRED 128

static InterCode *g_arr[MAXI];
static int g_n;

typedef struct {
    int lo, hi;                 /* [lo,hi] instruction index range */
    int succ[2]; int nsucc;
    int pred[MAXPRED]; int npred;
    int idom;
    int is_entry;
} Block;

static Block g_bb[MAXI];
static int g_nbb;
static int g_ins2bb[MAXI];
static int g_label2bb[MAXI];

static void build_index(Program *prog) {
    g_n = 0;
    for (InterCode *c = prog->ir_head; c; c = c->next)
        if (g_n < MAXI) g_arr[g_n++] = c;
}

static int build_cfg(Program *prog) {
    build_index(prog);
    if (g_n == 0) { g_nbb = 0; return 0; }

    static char isleader[MAXI];
    memset(isleader, 0, g_n);
    isleader[0] = 1;
    for (int i = 0; i < g_n; i++) {
        IRKind k = g_arr[i]->kind;
        if (k == IR_LABEL || k == IR_FUNCTION) isleader[i] = 1;
        if ((k == IR_GOTO || k == IR_IF || k == IR_RETURN) && i + 1 < g_n) isleader[i + 1] = 1;
    }

    g_nbb = 0;
    for (int i = 0; i < g_n; ) {
        int j = i + 1;
        while (j < g_n && !isleader[j]) j++;
        if (g_nbb >= MAXI) return 0;
        Block *b = &g_bb[g_nbb];
        b->lo = i; b->hi = j - 1; b->nsucc = 0; b->npred = 0; b->idom = -1;
        b->is_entry = (i == 0 || g_arr[i]->kind == IR_FUNCTION);
        for (int t = i; t < j; t++) g_ins2bb[t] = g_nbb;
        g_nbb++;
        i = j;
    }

    for (int i = 0; i < MAXI; i++) g_label2bb[i] = -1;
    for (int b = 0; b < g_nbb; b++) {
        InterCode *f = g_arr[g_bb[b].lo];
        if (f->kind == IR_LABEL && f->arg1->no >= 0 && f->arg1->no < MAXI)
            g_label2bb[f->arg1->no] = b;
    }

    for (int b = 0; b < g_nbb; b++) {
        Block *bb = &g_bb[b];
        InterCode *last = g_arr[bb->hi];
        if (last->kind == IR_GOTO) {
            int t = (last->arg1->no < MAXI) ? g_label2bb[last->arg1->no] : -1;
            if (t >= 0) bb->succ[bb->nsucc++] = t;
        } else if (last->kind == IR_IF) {
            int t = (last->result->no < MAXI) ? g_label2bb[last->result->no] : -1;
            if (t >= 0) bb->succ[bb->nsucc++] = t;
            if (b + 1 < g_nbb) bb->succ[bb->nsucc++] = b + 1;
        } else if (last->kind == IR_RETURN) {
            /* no successor */
        } else {
            if (b + 1 < g_nbb && g_arr[g_bb[b + 1].lo]->kind != IR_FUNCTION)
                bb->succ[bb->nsucc++] = b + 1;
        }
    }

    for (int b = 0; b < g_nbb; b++)
        for (int s = 0; s < g_bb[b].nsucc; s++) {
            int su = g_bb[b].succ[s];
            if (g_bb[su].npred < MAXPRED) g_bb[su].pred[g_bb[su].npred++] = b;
        }
    return 1;
}

/* ---- dominators (iterative, per entry-reachable region) ---- */
static int g_rpo[MAXI], g_rpon[MAXI], g_rpo_cnt;

static void dfs_po(int b, char *vis) {
    vis[b] = 1;
    for (int s = 0; s < g_bb[b].nsucc; s++)
        if (!vis[g_bb[b].succ[s]]) dfs_po(g_bb[b].succ[s], vis);
    g_rpo[g_rpo_cnt++] = b;   /* postorder */
}

static int dom_intersect(int a, int b) {
    /* g_rpon: reverse-postorder index (entry smallest). idom of a node has a
     * strictly smaller rpon, so advance whichever finger has the larger rpon. */
    while (a != b) {
        while (g_rpon[a] > g_rpon[b]) a = g_bb[a].idom;
        while (g_rpon[b] > g_rpon[a]) b = g_bb[b].idom;
    }
    return a;
}

static void compute_dominators(void) {
    static char vis[MAXI];
    memset(vis, 0, g_nbb);
    g_rpo_cnt = 0;
    for (int b = 0; b < g_nbb; b++) {
        if (g_bb[b].is_entry && !vis[b]) dfs_po(b, vis);
    }
    for (int b = 0; b < g_nbb; b++) if (!vis[b]) { /* unreachable */ }
    /* reverse postorder numbering */
    for (int b = 0; b < g_nbb; b++) g_rpon[b] = -1;
    int num = 0;
    for (int i = g_rpo_cnt - 1; i >= 0; i--) g_rpon[g_rpo[i]] = num++;

    for (int b = 0; b < g_nbb; b++) g_bb[b].idom = -1;
    for (int b = 0; b < g_nbb; b++) if (g_bb[b].is_entry) g_bb[b].idom = b;

    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = g_rpo_cnt - 1; i >= 0; i--) {
            int b = g_rpo[i];
            if (g_bb[b].is_entry) continue;
            int newidom = -1;
            for (int p = 0; p < g_bb[b].npred; p++) {
                int pr = g_bb[b].pred[p];
                if (g_bb[pr].idom == -1) continue;
                newidom = (newidom == -1) ? pr : dom_intersect(newidom, pr);
            }
            if (newidom != -1 && g_bb[b].idom != newidom) { g_bb[b].idom = newidom; changed = 1; }
        }
    }
}

static int dominates(int a, int b) {
    if (a == b) return 1;
    int x = b;
    while (x != -1 && !g_bb[x].is_entry) {
        x = g_bb[x].idom;
        if (x == a) return 1;
        if (x == g_bb[x].idom) break;
    }
    return (a == x);
}

/* ============================================================
 *  Variable/temp interning
 * ============================================================ */
typedef struct { int kind, no; } VKey;
static VKey g_vk[MAXI];
static int g_nvk;

static int vk_get(Operand *o) {
    if (!op_is_vt(o)) return -1;
    for (int i = 0; i < g_nvk; i++)
        if (g_vk[i].kind == o->kind && g_vk[i].no == o->no) return i;
    return -1;
}

static void vk_build(Program *prog) {
    g_nvk = 0;
    for (InterCode *c = prog->ir_head; c; c = c->next) {
        Operand *ops[3] = { c->result, c->arg1, c->arg2 };
        for (int i = 0; i < 3; i++) {
            Operand *o = ops[i];
            if (op_is_vt(o) && vk_get(o) < 0 && g_nvk < MAXI) {
                g_vk[g_nvk].kind = o->kind; g_vk[g_nvk].no = o->no; g_nvk++;
            }
        }
    }
}

/* ============================================================
 *  Global constant propagation (lattice: UNDEF/CONST/NAC)
 * ============================================================ */
#define CP_UNDEF 0
#define CP_NAC   1
#define CP_CONST 2
typedef struct { int st; int val; } CV;

static CV cp_meet(CV a, CV b) {
    if (a.st == CP_UNDEF) return b;
    if (b.st == CP_UNDEF) return a;
    if (a.st == CP_NAC || b.st == CP_NAC) { CV r = { CP_NAC, 0 }; return r; }
    if (a.val == b.val) return a;
    CV r = { CP_NAC, 0 }; return r;
}

static CV cp_opd(Operand *o, CV *m) {
    CV r;
    if (op_is_const(o)) { r.st = CP_CONST; r.val = o->value; return r; }
    int id = vk_get(o);
    if (id < 0) { r.st = CP_NAC; r.val = 0; return r; }
    return m[id];
}

static void cp_apply_inst(InterCode *c, CV *m) {
    Operand *d = ir_dest(c);
    int id = d ? vk_get(d) : -1;
    if (id < 0) return;
    CV nv;
    if (c->kind == IR_ASSIGN) {
        nv = cp_opd(c->arg1, m);
    } else if (ir_is_arith(c)) {
        CV a = cp_opd(c->arg1, m), b = cp_opd(c->arg2, m);
        if (a.st == CP_CONST && b.st == CP_CONST) {
            nv.st = CP_CONST;
            if (c->kind == IR_ADD) nv.val = a.val + b.val;
            else if (c->kind == IR_SUB) nv.val = a.val - b.val;
            else if (c->kind == IR_MUL) nv.val = a.val * b.val;
            else { if (b.val == 0) { nv.st = CP_NAC; nv.val = 0; } else nv.val = a.val / b.val; }
        } else if (a.st == CP_NAC || b.st == CP_NAC) { nv.st = CP_NAC; nv.val = 0; }
        else { nv.st = CP_UNDEF; nv.val = 0; }
    } else { nv.st = CP_NAC; nv.val = 0; }  /* READ / LOAD / CALL */
    m[id] = nv;
}

static int global_const_prop(Program *prog) {
    if (!build_cfg(prog)) return 0;
    vk_build(prog);
    if (g_nvk == 0 || g_nbb == 0) return 0;

    CV *IN = malloc(sizeof(CV) * g_nbb * g_nvk);
    CV *OUT = malloc(sizeof(CV) * g_nbb * g_nvk);
    CV *work = malloc(sizeof(CV) * g_nvk);
    for (int i = 0; i < g_nbb * g_nvk; i++) { IN[i].st = CP_UNDEF; IN[i].val = 0; OUT[i].st = CP_UNDEF; OUT[i].val = 0; }

    int iter = 0, changed = 1;
    while (changed && iter++ < 1000) {
        changed = 0;
        for (int b = 0; b < g_nbb; b++) {
            /* IN[b] = meet of OUT[preds] (entry -> all UNDEF) */
            for (int v = 0; v < g_nvk; v++) { work[v].st = CP_UNDEF; work[v].val = 0; }
            if (!g_bb[b].is_entry) {
                for (int p = 0; p < g_bb[b].npred; p++) {
                    int pr = g_bb[b].pred[p];
                    for (int v = 0; v < g_nvk; v++) work[v] = cp_meet(work[v], OUT[pr * g_nvk + v]);
                }
            }
            for (int v = 0; v < g_nvk; v++) IN[b * g_nvk + v] = work[v];
            /* transfer */
            for (int t = g_bb[b].lo; t <= g_bb[b].hi; t++) cp_apply_inst(g_arr[t], work);
            for (int v = 0; v < g_nvk; v++) {
                if (OUT[b * g_nvk + v].st != work[v].st || OUT[b * g_nvk + v].val != work[v].val) {
                    OUT[b * g_nvk + v] = work[v]; changed = 1;
                }
            }
        }
    }

    /* apply: replace CONST-valued source operands */
    int mod = 0;
    for (int b = 0; b < g_nbb; b++) {
        for (int v = 0; v < g_nvk; v++) work[v] = IN[b * g_nvk + v];
        for (int t = g_bb[b].lo; t <= g_bb[b].hi; t++) {
            InterCode *c = g_arr[t];
            Operand **srcs[3] = { NULL, NULL, NULL }; int ns = 0;
            switch (c->kind) {
                case IR_ASSIGN: srcs[ns++] = &c->arg1; break;
                case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV:
                    srcs[ns++] = &c->arg1; srcs[ns++] = &c->arg2; break;
                case IR_IF: srcs[ns++] = &c->arg1; srcs[ns++] = &c->arg2; break;
                case IR_RETURN: case IR_WRITE: case IR_ARG: srcs[ns++] = &c->arg1; break;
                case IR_LOAD: srcs[ns++] = &c->arg1; break;
                case IR_STORE: srcs[ns++] = &c->result; srcs[ns++] = &c->arg1; break;
                default: break;
            }
            for (int i = 0; i < ns; i++) {
                Operand *o = *srcs[i];
                if (op_is_vt(o)) {
                    int id = vk_get(o);
                    if (id >= 0 && work[id].st == CP_CONST) {
                        free(o); *srcs[i] = make_const(work[id].val); mod = 1;
                    }
                }
            }
            cp_apply_inst(c, work);
        }
    }

    free(IN); free(OUT); free(work);
    return mod;
}

/* ============================================================
 *  Global Common Subexpression Elimination
 *   (available "value-holding facts": pairs (holder, expr))
 * ============================================================ */
typedef struct { int op; int a_c, a; int b_c, b; } Expr;  /* a_c: 1 if constant */
static Expr g_expr[MAXI];
static int g_nexpr;

static int okey_cmp(int c1, int v1, int c2, int v2) {
    if (c1 != c2) return c1 - c2;
    return v1 - v2;
}
static int expr_eq(Expr *x, Expr *y) {
    return x->op == y->op && x->a_c == y->a_c && x->a == y->a && x->b_c == y->b_c && x->b == y->b;
}

/* build Expr for an arith instruction; returns -1 if not eligible */
static int expr_of(InterCode *c, Expr *out) {
    if (!ir_is_arith(c)) return -1;
    Operand *o1 = c->arg1, *o2 = c->arg2;
    Expr e; e.op = c->kind;
    if (op_is_const(o1)) { e.a_c = 1; e.a = o1->value; }
    else if (op_is_vt(o1)) { e.a_c = 0; e.a = vk_get(o1); if (e.a < 0) return -1; }
    else return -1;
    if (op_is_const(o2)) { e.b_c = 1; e.b = o2->value; }
    else if (op_is_vt(o2)) { e.b_c = 0; e.b = vk_get(o2); if (e.b < 0) return -1; }
    else return -1;
    if (e.op == IR_ADD || e.op == IR_MUL) {
        if (okey_cmp(e.a_c, e.a, e.b_c, e.b) > 0) {
            int tc = e.a_c, tv = e.a; e.a_c = e.b_c; e.a = e.b; e.b_c = tc; e.b = tv;
        }
    }
    *out = e;
    return 0;
}

static int expr_intern(Expr *e) {
    for (int i = 0; i < g_nexpr; i++) if (expr_eq(&g_expr[i], e)) return i;
    if (g_nexpr >= MAXI) return -1;
    g_expr[g_nexpr] = *e; return g_nexpr++;
}

/* does expr use vk id `w`? */
static int expr_uses(Expr *e, int w) {
    return (!e->a_c && e->a == w) || (!e->b_c && e->b == w);
}

typedef unsigned long BSW;
#define BSB ((int)(sizeof(BSW) * 8))
static int nwords(int n) { return (n + BSB - 1) / BSB; }
static void bs_set(BSW *b, int i) { b[i / BSB] |= (1UL << (i % BSB)); }
static void bs_clr(BSW *b, int i) { b[i / BSB] &= ~(1UL << (i % BSB)); }
static int bs_test(BSW *b, int i) { return (b[i / BSB] >> (i % BSB)) & 1UL; }

static int g_npairs;   /* g_nvk * g_nexpr */
static int PAIR(int holder, int expr) { return holder * g_nexpr + expr; }

/* apply one instruction's effect to the current available-pair set */
static void cse_transfer(InterCode *c, BSW *cur) {
    Operand *d = ir_dest(c);
    int w = d ? vk_get(d) : -1;
    /* kill: any pair whose holder is w, or whose expr uses w */
    if (w >= 0) {
        for (int h = 0; h < g_nvk; h++)
            for (int e = 0; e < g_nexpr; e++) {
                int idx = PAIR(h, e);
                if (!bs_test(cur, idx)) continue;
                if (h == w || expr_uses(&g_expr[e], w)) bs_clr(cur, idx);
            }
    }
    /* gen */
    if (ir_is_arith(c) && w >= 0) {
        Expr e;
        if (expr_of(c, &e) == 0) {
            int ei = expr_intern(&e);
            if (ei >= 0 && !expr_uses(&e, w)) bs_set(cur, PAIR(w, ei));
        }
    } else if (c->kind == IR_ASSIGN && w >= 0 && op_is_vt(c->arg1)) {
        /* copy w := y  -> w holds whatever y holds */
        int y = vk_get(c->arg1);
        if (y >= 0 && y != w) {
            for (int e = 0; e < g_nexpr; e++)
                if (bs_test(cur, PAIR(y, e)) && !expr_uses(&g_expr[e], w))
                    bs_set(cur, PAIR(w, e));
        }
    }
}

static int global_cse(Program *prog) {
    if (!build_cfg(prog)) return 0;
    vk_build(prog);
    /* collect expressions */
    g_nexpr = 0;
    for (InterCode *c = prog->ir_head; c; c = c->next) {
        Expr e;
        if (expr_of(c, &e) == 0) expr_intern(&e);
    }
    if (g_nvk == 0 || g_nexpr == 0 || g_nbb == 0) return 0;
    g_npairs = g_nvk * g_nexpr;
    if (g_npairs <= 0 || g_npairs > 2000000) return 0;

    int W = nwords(g_npairs);
    BSW *IN = calloc((size_t)g_nbb * W, sizeof(BSW));
    BSW *OUT = calloc((size_t)g_nbb * W, sizeof(BSW));
    BSW *cur = calloc(W, sizeof(BSW));
    BSW *tmp = calloc(W, sizeof(BSW));

    /* init OUT = full for all blocks */
    for (int b = 0; b < g_nbb; b++)
        for (int i = 0; i < g_npairs; i++) bs_set(&OUT[b * W], i);

    int iter = 0, changed = 1;
    while (changed && iter++ < 2000) {
        changed = 0;
        for (int b = 0; b < g_nbb; b++) {
            /* IN = intersection of OUT[preds]; entry or no pred -> empty */
            if (g_bb[b].is_entry || g_bb[b].npred == 0) {
                memset(&IN[b * W], 0, W * sizeof(BSW));
            } else {
                for (int i = 0; i < W; i++) tmp[i] = ~0UL;
                for (int p = 0; p < g_bb[b].npred; p++) {
                    int pr = g_bb[b].pred[p];
                    for (int i = 0; i < W; i++) tmp[i] &= OUT[pr * W + i];
                }
                memcpy(&IN[b * W], tmp, W * sizeof(BSW));
            }
            /* transfer */
            memcpy(cur, &IN[b * W], W * sizeof(BSW));
            for (int t = g_bb[b].lo; t <= g_bb[b].hi; t++) cse_transfer(g_arr[t], cur);
            if (memcmp(cur, &OUT[b * W], W * sizeof(BSW)) != 0) {
                memcpy(&OUT[b * W], cur, W * sizeof(BSW)); changed = 1;
            }
        }
    }

    /* apply */
    int mod = 0;
    for (int b = 0; b < g_nbb; b++) {
        memcpy(cur, &IN[b * W], W * sizeof(BSW));
        for (int t = g_bb[b].lo; t <= g_bb[b].hi; t++) {
            InterCode *c = g_arr[t];
            if (ir_is_arith(c)) {
                Expr e;
                if (expr_of(c, &e) == 0) {
                    int ei = expr_intern(&e);
                    Operand *d = ir_dest(c);
                    int w = d ? vk_get(d) : -1;
                    if (ei >= 0 && w >= 0) {
                        /* find a holder available now (prefer VARIABLE) */
                        int best = -1;
                        for (int h = 0; h < g_nvk; h++) {
                            if (h == w) continue;
                            if (bs_test(cur, PAIR(h, ei))) {
                                if (best < 0) best = h;
                                else if (g_vk[h].kind == OPD_VARIABLE && g_vk[best].kind != OPD_VARIABLE) best = h;
                            }
                        }
                        if (best >= 0) {
                            free(c->arg1); free(c->arg2); c->arg2 = NULL;
                            c->kind = IR_ASSIGN;
                            c->arg1 = make_vt(g_vk[best].kind, g_vk[best].no);
                            mod = 1;
                        }
                    }
                }
            }
            cse_transfer(c, cur);
        }
    }

    free(IN); free(OUT); free(cur); free(tmp);
    return mod;
}

/* ============================================================
 *  Copy propagation:  t := y ; ... use(t)  ->  use(y)
 *  Only for temps with a single use (safe, local-ish helper).
 * ============================================================ */
static int copy_propagation(Program *prog) {
    int changed = 0, pass = 0;
    while (pass++ < 8) {
        int local = 0;
        for (InterCode *c = prog->ir_head; c; ) {
            InterCode *nx = c->next;
            if (c->kind == IR_ASSIGN && c->result && c->result->kind == OPD_TEMP &&
                c->arg1 && op_is_vt(c->arg1)) {
                Operand *src = c->arg1, *dest = c->result;
                int total_uses = 0; InterCode *the_use = NULL; int use_safe = 0;
                int crossed = 0, src_redef = 0;
                for (InterCode *d = c->next; d; d = d->next) {
                    if (d->kind == IR_LABEL || d->kind == IR_FUNCTION) crossed = 1;
                    Operand *dd = ir_dest(d);
                    if (dd && op_eq(dd, dest)) break;   /* dest live range ends */
                    int used = 0;
                    Operand *u[4] = { NULL,NULL,NULL,NULL }; int nu = 0;
                    if (d->kind == IR_ASSIGN) u[nu++] = d->arg1;
                    else if (ir_is_arith(d)) { u[nu++] = d->arg1; u[nu++] = d->arg2; }
                    else if (d->kind == IR_IF) { u[nu++] = d->arg1; u[nu++] = d->arg2; }
                    else if (d->kind == IR_RETURN || d->kind == IR_WRITE || d->kind == IR_ARG) u[nu++] = d->arg1;
                    else if (d->kind == IR_LOAD) u[nu++] = d->arg1;
                    else if (d->kind == IR_STORE) { u[nu++] = d->result; u[nu++] = d->arg1; }
                    for (int i = 0; i < nu; i++) if (op_eq(u[i], dest)) used = 1;
                    if (used) {
                        total_uses++;
                        if (total_uses == 1) { the_use = d; use_safe = (!crossed && !src_redef); }
                    }
                    if (dd && op_eq(dd, src)) src_redef = 1;
                    if (d->kind == IR_GOTO || d->kind == IR_IF || d->kind == IR_RETURN) crossed = 1;
                }
                InterCode *last_use = the_use;
                if (total_uses == 1 && last_use && use_safe) {
                    Operand *u[4] = { NULL,NULL,NULL,NULL }; Operand **pp[4]; int nu = 0;
                    if (last_use->kind == IR_ASSIGN) { pp[nu] = &last_use->arg1; u[nu] = last_use->arg1; nu++; }
                    else if (ir_is_arith(last_use)) { pp[nu]=&last_use->arg1; u[nu]=last_use->arg1; nu++; pp[nu]=&last_use->arg2; u[nu]=last_use->arg2; nu++; }
                    else if (last_use->kind == IR_IF) { pp[nu]=&last_use->arg1; u[nu]=last_use->arg1; nu++; pp[nu]=&last_use->arg2; u[nu]=last_use->arg2; nu++; }
                    else if (last_use->kind == IR_RETURN || last_use->kind == IR_WRITE || last_use->kind == IR_ARG) { pp[nu]=&last_use->arg1; u[nu]=last_use->arg1; nu++; }
                    else if (last_use->kind == IR_LOAD) { pp[nu]=&last_use->arg1; u[nu]=last_use->arg1; nu++; }
                    else if (last_use->kind == IR_STORE) { pp[nu]=&last_use->result; u[nu]=last_use->result; nu++; pp[nu]=&last_use->arg1; u[nu]=last_use->arg1; nu++; }
                    for (int i = 0; i < nu; i++) if (op_eq(u[i], dest)) { free(*pp[i]); *pp[i] = op_clone(src); }
                    remove_ir(prog, c);
                    changed = 1; local = 1;
                }
            }
            c = nx;
        }
        if (!local) break;
    }
    return changed;
}

/* ============================================================
 *  Global Dead Code Elimination (live-variable analysis)
 * ============================================================ */
static int inst_uses(InterCode *c, int *u) {
    int n = 0;
    Operand *cand[4] = { NULL,NULL,NULL,NULL }; int nc = 0;
    switch (c->kind) {
        case IR_ASSIGN: cand[nc++] = c->arg1; break;
        case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: cand[nc++] = c->arg1; cand[nc++] = c->arg2; break;
        case IR_LOAD: cand[nc++] = c->arg1; break;
        case IR_STORE: cand[nc++] = c->result; cand[nc++] = c->arg1; break;
        case IR_IF: cand[nc++] = c->arg1; cand[nc++] = c->arg2; break;
        case IR_RETURN: case IR_WRITE: case IR_ARG: cand[nc++] = c->arg1; break;
        default: break;
    }
    for (int i = 0; i < nc; i++) {
        int id = vk_get(cand[i]);
        if (id >= 0) u[n++] = id;
    }
    return n;
}

static int inst_def(InterCode *c) {
    Operand *d = ir_dest(c);
    return d ? vk_get(d) : -1;
}

static int inst_removable(InterCode *c) {
    switch (c->kind) {
        case IR_ASSIGN: case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: case IR_LOAD:
            return 1;   /* pure: removable when dead */
        default: return 0;  /* READ/CALL/STORE/WRITE/... have side effects */
    }
}

static int global_dce(Program *prog) {
    if (!build_cfg(prog)) return 0;
    vk_build(prog);
    if (g_nbb == 0) return 0;
    if (g_nvk == 0) return 0;

    int W = nwords(g_nvk);
    BSW *IN = calloc((size_t)g_nbb * W, sizeof(BSW));
    BSW *OUT = calloc((size_t)g_nbb * W, sizeof(BSW));
    BSW *use = calloc((size_t)g_nbb * W, sizeof(BSW));
    BSW *def = calloc((size_t)g_nbb * W, sizeof(BSW));
    BSW *live = calloc(W, sizeof(BSW));

    /* compute use/def per block (forward within block) */
    for (int b = 0; b < g_nbb; b++) {
        for (int t = g_bb[b].lo; t <= g_bb[b].hi; t++) {
            InterCode *c = g_arr[t];
            int u[4], nu = inst_uses(c, u);
            for (int i = 0; i < nu; i++)
                if (!bs_test(&def[b * W], u[i])) bs_set(&use[b * W], u[i]);
            int d = inst_def(c);
            if (d >= 0) bs_set(&def[b * W], d);
        }
    }

    int iter = 0, changed = 1;
    while (changed && iter++ < 2000) {
        changed = 0;
        for (int b = g_nbb - 1; b >= 0; b--) {
            /* OUT = union of IN[succ] */
            memset(live, 0, W * sizeof(BSW));
            for (int s = 0; s < g_bb[b].nsucc; s++) {
                int su = g_bb[b].succ[s];
                for (int i = 0; i < W; i++) live[i] |= IN[su * W + i];
            }
            if (memcmp(live, &OUT[b * W], W * sizeof(BSW)) != 0) { memcpy(&OUT[b * W], live, W * sizeof(BSW)); changed = 1; }
            /* IN = use ∪ (OUT - def) */
            for (int i = 0; i < W; i++) live[i] = use[b * W + i] | (OUT[b * W + i] & ~def[b * W + i]);
            if (memcmp(live, &IN[b * W], W * sizeof(BSW)) != 0) { memcpy(&IN[b * W], live, W * sizeof(BSW)); changed = 1; }
        }
    }

    /* removal: backward walk within each block */
    int mod = 0;
    for (int b = 0; b < g_nbb; b++) {
        memcpy(live, &OUT[b * W], W * sizeof(BSW));
        for (int t = g_bb[b].hi; t >= g_bb[b].lo; t--) {
            InterCode *c = g_arr[t];
            int d = inst_def(c);
            if (d >= 0 && inst_removable(c) && !bs_test(live, d)) {
                remove_ir(prog, c);
                mod = 1;
                continue;   /* do not add its uses to live */
            }
            /* update live: kill def, gen uses */
            if (d >= 0) bs_clr(live, d);
            int u[4], nu = inst_uses(c, u);
            for (int i = 0; i < nu; i++) bs_set(live, u[i]);
        }
    }

    free(IN); free(OUT); free(use); free(def); free(live);
    return mod;
}

/* ============================================================
 *  Loop analysis helpers
 * ============================================================ */
typedef struct { int header, latch; } BackEdge;

/* mark blocks of the natural loop of back edge latch->header */
static void collect_loop(int header, int latch, char *inloop) {
    memset(inloop, 0, g_nbb);
    inloop[header] = 1;
    int stack[MAXI], sp = 0;
    if (!inloop[latch]) { inloop[latch] = 1; stack[sp++] = latch; }
    while (sp) {
        int b = stack[--sp];
        for (int p = 0; p < g_bb[b].npred; p++) {
            int pr = g_bb[b].pred[p];
            if (!inloop[pr]) { inloop[pr] = 1; stack[sp++] = pr; }
        }
    }
}

/* is operand invariant w.r.t. loop (constant, or not defined inside loop)? */
static int operand_invariant(Operand *o, char *inloop) {
    if (!o) return 1;
    if (op_is_const(o)) return 1;
    if (!op_is_vt(o)) return 1;  /* address/label treated as invariant literal */
    /* defined inside loop? */
    for (int b = 0; b < g_nbb; b++) {
        if (!inloop[b]) continue;
        for (int t = g_bb[b].lo; t <= g_bb[b].hi; t++) {
            Operand *d = ir_dest(g_arr[t]);
            if (d && op_eq(d, o)) return 0;
        }
    }
    return 1;
}

static int count_defs_in_loop(Operand *o, char *inloop) {
    int cnt = 0;
    for (int b = 0; b < g_nbb; b++) {
        if (!inloop[b]) continue;
        for (int t = g_bb[b].lo; t <= g_bb[b].hi; t++) {
            Operand *d = ir_dest(g_arr[t]);
            if (d && op_eq(d, o)) cnt++;
        }
    }
    return cnt;
}

/* ============================================================
 *  Loop Invariant Code Motion
 * ============================================================ */
static int loop_invariant_code_motion(Program *prog) {
    if (!build_cfg(prog)) return 0;
    vk_build(prog);
    compute_dominators();
    if (g_nbb == 0) return 0;

    static char inloop[MAXI];

    for (int b = 0; b < g_nbb; b++) {
        InterCode *last = g_arr[g_bb[b].hi];
        int hdr = -1;
        if (last->kind == IR_GOTO && last->arg1->kind == OPD_LABEL && last->arg1->no < MAXI)
            hdr = g_label2bb[last->arg1->no];
        else if (last->kind == IR_IF && last->result->kind == OPD_LABEL && last->result->no < MAXI)
            hdr = g_label2bb[last->result->no];
        if (hdr < 0) continue;
        if (!dominates(hdr, b)) continue;   /* real back edge: header dominates latch */

        collect_loop(hdr, b, inloop);

        /* header must begin with a LABEL and be entered from outside only by
         * fall-through (so the slot before the LABEL is a valid preheader). */
        InterCode *hlabel = g_arr[g_bb[hdr].lo];
        if (hlabel->kind != IR_LABEL) continue;
        InterCode *pre = hlabel->prev;
        if (!pre) continue;
        if (pre->kind == IR_GOTO || pre->kind == IR_RETURN) continue;
        /* no block OUTSIDE the loop may jump to the header label */
        int ext_jump = 0;
        for (int x = 0; x < g_nbb; x++) {
            if (inloop[x]) continue;
            InterCode *lx = g_arr[g_bb[x].hi];
            if (lx->kind == IR_GOTO && lx->arg1->kind == OPD_LABEL && lx->arg1->no == hlabel->arg1->no) ext_jump = 1;
            if (lx->kind == IR_IF && lx->result->kind == OPD_LABEL && lx->result->no == hlabel->arg1->no) ext_jump = 1;
        }
        if (ext_jump) continue;

        /* loop exit blocks: in-loop blocks with a successor outside the loop */
        /* try to hoist invariant instructions */
        int hoisted_any = 0;
        for (int b2 = 0; b2 < g_nbb && !hoisted_any; b2++) {
            if (!inloop[b2]) continue;
            for (int t = g_bb[b2].lo; t <= g_bb[b2].hi; t++) {
                InterCode *c = g_arr[t];
                if (c->kind != IR_ASSIGN && !ir_is_arith(c)) continue;
                Operand *dst = ir_dest(c);
                if (!dst || !op_is_vt(dst)) continue;
                int inv = operand_invariant(c->arg1, inloop) &&
                          (!ir_is_arith(c) || operand_invariant(c->arg2, inloop));
                if (!inv) continue;
                if (count_defs_in_loop(dst, inloop) != 1) continue;

                /* the def must dominate every loop-exit block */
                int dominates_exits = 1;
                for (int x = 0; x < g_nbb; x++) {
                    if (!inloop[x]) continue;
                    int is_exit = 0;
                    for (int s = 0; s < g_bb[x].nsucc; s++) if (!inloop[g_bb[x].succ[s]]) is_exit = 1;
                    if (is_exit && !dominates(b2, x)) { dominates_exits = 0; break; }
                }
                if (!dominates_exits) continue;

                /* def block must dominate all in-loop uses of dst; if the use is
                 * in the same block it must appear after c. Conservative check. */
                int ok_uses = 1;
                for (int x = 0; x < g_nbb && ok_uses; x++) {
                    if (!inloop[x]) continue;
                    for (int tt = g_bb[x].lo; tt <= g_bb[x].hi; tt++) {
                        InterCode *d = g_arr[tt];
                        if (d == c) continue;
                        int u[4], nu = inst_uses(d, u);
                        int uses_dst = 0;
                        for (int i = 0; i < nu; i++) if (g_vk[u[i]].kind == dst->kind && g_vk[u[i]].no == dst->no) uses_dst = 1;
                        if (uses_dst) {
                            if (!dominates(b2, x)) { ok_uses = 0; break; }
                            if (x == b2 && tt < t) { ok_uses = 0; break; }
                        }
                    }
                }
                if (!ok_uses) continue;

                /* hoist c to just before the header label */
                if (c->prev) c->prev->next = c->next; else prog->ir_head = c->next;
                if (c->next) c->next->prev = c->prev; else prog->ir_tail = c->prev;
                insert_before(prog, hlabel, c);
                hoisted_any = 1;
                break;
            }
        }
        if (hoisted_any) return 1;   /* rebuild CFG and retry from outer driver */
    }
    return 0;
}

/* ============================================================
 *  Induction-variable Strength Reduction
 *    Pattern per loop:
 *      basic IV  i :  i = i + s  (possibly via temp: tmp=i+s; i=tmp)
 *      j = c * i (or i * c), and just afterwards a var holds j and equalled
 *      c*i on entry -> replace multiplication by  j := <holder> +/- c*s
 * ============================================================ */
static int strength_reduction(Program *prog) {
    if (!build_cfg(prog)) return 0;
    vk_build(prog);
    compute_dominators();
    if (g_nbb == 0) return 0;

    static char inloop[MAXI];

    for (int b = 0; b < g_nbb; b++) {
        InterCode *last = g_arr[g_bb[b].hi];
        int hdr = -1;
        if (last->kind == IR_GOTO && last->arg1->kind == OPD_LABEL && last->arg1->no < MAXI)
            hdr = g_label2bb[last->arg1->no];
        else if (last->kind == IR_IF && last->result->kind == OPD_LABEL && last->result->no < MAXI)
            hdr = g_label2bb[last->result->no];
        if (hdr < 0 || !dominates(hdr, b)) continue;
        collect_loop(hdr, b, inloop);

        /* enumerate instructions of the loop in program order */
        int lo = MAXI, hi = -1;
        for (int x = 0; x < g_nbb; x++) if (inloop[x]) { if (g_bb[x].lo < lo) lo = g_bb[x].lo; if (g_bb[x].hi > hi) hi = g_bb[x].hi; }
        if (hi < 0) continue;

        for (int t = lo; t <= hi; t++) {
            if (!inloop[g_ins2bb[t]]) continue;
            InterCode *mc = g_arr[t];
            if (mc->kind != IR_MUL) continue;
            /* c * i  or  i * c */
            Operand *iv = NULL, *cst = NULL;
            if (op_is_const(mc->arg1) && op_is_vt(mc->arg2)) { cst = mc->arg1; iv = mc->arg2; }
            else if (op_is_const(mc->arg2) && op_is_vt(mc->arg1)) { cst = mc->arg2; iv = mc->arg1; }
            if (!iv) continue;

            /* find the increment of iv inside the loop:  iv = iv +/- s   (via temp allowed) */
            int step = 0, found_step = 0, inc_idx = -1;
            for (int q = lo; q <= hi; q++) {
                if (!inloop[g_ins2bb[q]]) continue;
                InterCode *ic = g_arr[q];
                Operand *d = ir_dest(ic);
                if (!d || !op_eq(d, iv)) continue;
                /* iv := iv +/- const */
                if ((ic->kind == IR_ADD || ic->kind == IR_SUB) && op_eq(ic->arg1, iv) && op_is_const(ic->arg2)) {
                    step = (ic->kind == IR_ADD) ? ic->arg2->value : -ic->arg2->value;
                    found_step = 1; inc_idx = q;
                }
                /* iv := tmp  where tmp := iv +/- const  */
                else if (ic->kind == IR_ASSIGN && op_is_vt(ic->arg1)) {
                    for (int r = lo; r <= hi; r++) {
                        if (!inloop[g_ins2bb[r]]) continue;
                        InterCode *tc = g_arr[r];
                        Operand *td = ir_dest(tc);
                        if (td && op_eq(td, ic->arg1) &&
                            (tc->kind == IR_ADD || tc->kind == IR_SUB) &&
                            op_eq(tc->arg1, iv) && op_is_const(tc->arg2)) {
                            step = (tc->kind == IR_ADD) ? tc->arg2->value : -tc->arg2->value;
                            found_step = 1; inc_idx = q;
                        }
                    }
                } else { found_step = 0; inc_idx = -2; break; }
            }
            if (!found_step || inc_idx < 0) continue;
            /* increment must be executed before the multiplication in this iteration */
            if (!(inc_idx < t)) continue;

            /* the mul result must be stored into a variable j right after (mc: tX = c*i ; next: j = tX)
             * and j must currently equal c*i (i.e. defined by the same 'j = c*i' pattern
             * elsewhere / previous iteration). We require: the instruction after mc is
             * `j := dest(mc)` with j a VARIABLE, and j has a def of form `j := c*i` reachable. */
            Operand *mres = ir_dest(mc);
            InterCode *nb = mc->next;
            if (!nb || nb->kind != IR_ASSIGN || !op_eq(nb->arg1, mres) || nb->result->kind != OPD_VARIABLE) continue;
            Operand *j = nb->result;

            /* j must be assigned c*i somewhere else too (holds the running value) and
             * j must not be modified between its holder value and here except by this pattern.
             * Verify there is another `j := (c*i)` definition (initialisation) so the value chain holds. */
            int has_init = 0;
            for (InterCode *d = prog->ir_head; d; d = d->next) {
                if (d == nb) continue;
                if (d->kind == IR_ASSIGN && op_eq(d->result, j) && op_is_vt(d->arg1)) {
                    /* d: j := tK ; find tK := c*i */
                    for (InterCode *e = prog->ir_head; e; e = e->next) {
                        Operand *ed = ir_dest(e);
                        if (ed && op_eq(ed, d->arg1) && e->kind == IR_MUL) {
                            int ok = (op_is_const(e->arg1) && e->arg1->value == cst->value && op_eq(e->arg2, iv)) ||
                                     (op_is_const(e->arg2) && e->arg2->value == cst->value && op_eq(e->arg1, iv));
                            if (ok) has_init = 1;
                        }
                    }
                }
            }
            if (!has_init) continue;

            /* j must not be redefined between the two evaluations except by j:=mres.
             * Conservative: j has exactly the two defs (init + this one) per loop iter. */
            if (count_defs_in_loop(j, inloop) != 1) continue;

            long delta = (long)cst->value * step;
            if (delta == 0) continue;
            /* replace  mres := c * i   with   mres := j (+/-) |delta| */
            free(mc->arg1); free(mc->arg2); mc->arg2 = NULL;
            mc->kind = (delta > 0) ? IR_ADD : IR_SUB;
            mc->arg1 = op_clone(j);
            mc->arg2 = make_const((int)(delta > 0 ? delta : -delta));
            return 1;
        }
    }
    return 0;
}

/* ============================================================
 *  Driver
 * ============================================================ */
void optimize_program(Program *prog) {
    structural_cleanup(prog);

    int iter = 0;
    while (iter++ < 30) {
        int ch = 0;
        ch |= algebraic_simplify(prog);
        ch |= global_const_prop(prog);
        ch |= algebraic_simplify(prog);
        ch |= global_cse(prog);
        ch |= copy_propagation(prog);
        ch |= if_simplify(prog);
        ch |= algebraic_simplify(prog);
        ch |= loop_invariant_code_motion(prog);
        ch |= strength_reduction(prog);
        ch |= global_dce(prog);
        ch |= structural_cleanup(prog);
        if (!ch) break;
    }

    global_dce(prog);
    structural_cleanup(prog);
}
