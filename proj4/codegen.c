#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ============================================================
 *  Register pool: $t0 - $t9  (10 registers, textbook 5.2.8)
 * ============================================================ */
#define NUM_REGS 10
static const char *reg_names[NUM_REGS] = {
    "$t0","$t1","$t2","$t3","$t4",
    "$t5","$t6","$t7","$t8","$t9"
};

typedef struct RegDesc  { int free; int var_idx; int dirty; } RegDesc;
typedef struct VarDesc  { int kind;     /* 0=VARIABLE, 1=TEMP */
                          int no;
                          int offset;  /* from $sp */
                          int in_reg;  /* which register, -1=none */
                          int used;    /* used in current function */
                          int is_param;/* 1 if this is a function parameter */
                        } VarDesc;

static FILE     *fout;
static int       var_count;
static VarDesc  *var_tab;
static RegDesc   regs[NUM_REGS];

static void emit(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(fout, fmt, ap); va_end(ap);
    fprintf(fout, "\n");
}

/* Avoid MIPS keyword conflicts in function labels */
static const char *safe_label(const char *name) {
    static char buf[256];
    static const char *mips_kw[] = {
        "add","sub","mul","div","and","or","xor","nor","slt",
        "addi","andi","ori","xori","lui","slti",
        "lw","sw","lb","sb","lh","sh","li","la","move",
        "beq","bne","bgt","bge","blt","ble","bgtz","bgez","bltz","blez",
        "j","jal","jr","jalr","syscall","nop","mfhi","mflo","mthi","mtlo",
        "sll","srl","sra","sllv","srlv","srav",
        "abs","neg","negu","not","rem","rol","ror",
        "seq","sge","sgt","sle","sne",
        NULL
    };
    for (const char **p = mips_kw; *p; p++)
        if (strcmp(name, *p) == 0) {
            snprintf(buf, sizeof(buf), "__fn_%s", name);
            return buf;
        }
    return name;
}

/* ============================================================
 *  Map (kind,no) -> index into var_tab
 * ============================================================ */
static int var_id(int kind, int no) {
    for (int i = 0; i < var_count; i++)
        if (var_tab[i].kind == kind && var_tab[i].no == no) return i;
    return -1;
}
static int var_id_of(Operand *op) {
    if (!op) return -1;
    if (op->kind == OPD_VARIABLE) return var_id(0, op->no);
    if (op->kind == OPD_TEMP)     return var_id(1, op->no);
    return -1;
}

/* ============================================================
 *  Pre-scan entire IR to collect all vars/temps
 * ============================================================ */
static void pre_scan(InterCode *ir) {
    int cap = 512;
    var_tab = malloc(cap * sizeof(VarDesc));
    var_count = 0;

    for (InterCode *c = ir; c; c = c->next) {
        Operand *ops[4] = { c->result, c->arg1, c->arg2, c->result };
        int n = (c->kind == IR_STORE) ? 4 : 3;
        for (int k = 0; k < n; k++) {
            if (!ops[k]) continue;
            if (ops[k]->kind != OPD_VARIABLE && ops[k]->kind != OPD_TEMP) continue;
            int gn = (ops[k]->kind == OPD_VARIABLE) ? 0 : 1;
            int found = 0;
            for (int i = 0; i < var_count; i++)
                if (var_tab[i].kind == gn && var_tab[i].no == ops[k]->no)
                    { found = 1; break; }
            if (!found) {
                if (var_count >= cap) { cap *= 2; var_tab = realloc(var_tab, cap * sizeof(VarDesc)); }
                var_tab[var_count].kind   = gn;
                var_tab[var_count].no     = ops[k]->no;
                var_tab[var_count].offset = -1;
                var_tab[var_count].in_reg = -1;
                var_tab[var_count].used   = 0;
                var_tab[var_count].is_param = 0;
                var_count++;
            }
        }
    }
}

/* ============================================================
 *  Next-use matrix (textbook 5.2.6): backward scan
 *  nu[pos][vi] = smallest j > pos where vi is USED, or -1
 * ============================================================ */
static int *compute_next_use(InterCode **instrs, int len) {
    int *nu = calloc(len * var_count, sizeof(int));
    int *last = malloc(var_count * sizeof(int));
    for (int i = 0; i < var_count; i++) last[i] = -1;

    for (int pos = len - 1; pos >= 0; pos--) {
        InterCode *c = instrs[pos];

        /* Determine def operand */
        Operand *def = NULL;
        switch (c->kind) {
            case IR_ASSIGN: case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV:
            case IR_LOAD: case IR_CALL: def = c->result; break;
            case IR_READ: def = c->arg1; break;
            default: break;
        }

        /* Collect uses at this position (exclude READ arg1, include WRITE/ARG/RETURN arg1, IF args, STORE address) */
        int uses[8], nu_uses = 0;

        if (c->arg1 && (c->arg1->kind == OPD_VARIABLE || c->arg1->kind == OPD_TEMP)
            && c->kind != IR_READ) {  /* READ defines arg1 */
            int vi = var_id_of(c->arg1);
            if (vi >= 0) uses[nu_uses++] = vi;
        }
        if (c->arg2 && (c->arg2->kind == OPD_VARIABLE || c->arg2->kind == OPD_TEMP)) {
            int vi = var_id_of(c->arg2);
            if (vi >= 0) {
                int dup = 0;
                for (int j = 0; j < nu_uses; j++) if (uses[j] == vi) dup = 1;
                if (!dup) uses[nu_uses++] = vi;
            }
        }
        /* STORE: c->result is address operand (a use, not def) */
        if (c->kind == IR_STORE && c->result &&
            (c->result->kind == OPD_VARIABLE || c->result->kind == OPD_TEMP)) {
            int vi = var_id_of(c->result);
            if (vi >= 0) {
                int dup = 0;
                for (int j = 0; j < nu_uses; j++) if (uses[j] == vi) dup = 1;
                if (!dup) uses[nu_uses++] = vi;
            }
        }

        /* Initialize nu for this position */
        for (int vi = 0; vi < var_count; vi++) nu[pos * var_count + vi] = -1;

        /* Record next-use for each use operand */
        for (int i = 0; i < nu_uses; i++) {
            int vi = uses[i];
            nu[pos * var_count + vi] = last[vi];
            last[vi] = pos;
        }

        /* For def: record its next-use before killing */
        if (def) {
            int di = var_id_of(def);
            if (di >= 0) {
                nu[pos * var_count + di] = last[di];
                last[di] = -1;
            }
        }
    }
    free(last);
    return nu;
}

/* ============================================================
 *  Register management (textbook Figures 5.10, 5.11)
 * ============================================================ */
static void reg_init(void) {
    for (int i = 0; i < NUM_REGS; i++)
        { regs[i].free = 1; regs[i].var_idx = -1; regs[i].dirty = 0; }
    for (int i = 0; i < var_count; i++)
        var_tab[i].in_reg = -1;
}
static void spill_reg(int r) {
    if (!regs[r].dirty || regs[r].var_idx < 0) return;
    int vi = regs[r].var_idx;
    emit("  sw %s, %d($sp)", reg_names[r], var_tab[vi].offset);
    regs[r].dirty = 0;
}
static void spill_all(void) {
    for (int i = 0; i < NUM_REGS; i++) spill_reg(i);
}
static void free_reg(int r) {
    spill_reg(r);
    int vi = regs[r].var_idx;
    if (vi >= 0) var_tab[vi].in_reg = -1;
    regs[r].free = 1; regs[r].var_idx = -1; regs[r].dirty = 0;
}
static int pick_spill(int *nu, int pos, int len) {
    int best = 0, best_dist = -2;
    for (int i = 0; i < NUM_REGS; i++) {
        int dist = len + 200;
        int vi = regs[i].var_idx;
        if (vi >= 0) {
            int nuval = nu[pos * var_count + vi];
            dist = (nuval >= 0) ? (nuval - pos) : (len + 50);
        }
        if (dist > best_dist) { best_dist = dist; best = i; }
    }
    return best;
}
static int alloc_load(int vi, int *nu, int pos, int len) {
    for (int i = 0; i < NUM_REGS; i++) if (regs[i].free) {
        regs[i].free = 0; regs[i].var_idx = vi; regs[i].dirty = 0;
        var_tab[vi].in_reg = i;
        emit("  lw %s, %d($sp)", reg_names[i], var_tab[vi].offset);
        return i;
    }
    int r = pick_spill(nu, pos, len); free_reg(r);
    regs[r].free = 0; regs[r].var_idx = vi; regs[r].dirty = 0;
    var_tab[vi].in_reg = r;
    emit("  lw %s, %d($sp)", reg_names[r], var_tab[vi].offset);
    return r;
}
static int ensure(int vi, int *nu, int pos, int len) {
    if (vi < 0) return -1;
    if (var_tab[vi].in_reg >= 0) return var_tab[vi].in_reg;
    return alloc_load(vi, nu, pos, len);
}
static int alloc_result(int vi, int *nu, int pos, int len) {
    if (vi < 0) return -1;
    int old = var_tab[vi].in_reg;
    if (old >= 0) { regs[old].var_idx = -1; regs[old].free = 1; regs[old].dirty = 0; }
    for (int i = 0; i < NUM_REGS; i++) if (regs[i].free) {
        regs[i].free = 0; regs[i].var_idx = vi; regs[i].dirty = 1;
        var_tab[vi].in_reg = i;
        return i;
    }
    int r = pick_spill(nu, pos, len); free_reg(r);
    regs[r].free = 0; regs[r].var_idx = vi; regs[r].dirty = 1;
    var_tab[vi].in_reg = r;
    return r;
}
static void free_if_dead(int vi, int *nu, int pos) {
    if (vi < 0) return;
    if (nu[pos * var_count + vi] < 0 && var_tab[vi].in_reg >= 0) {
        free_reg(var_tab[vi].in_reg);
        var_tab[vi].in_reg = -1;
    }
}

/* ============================================================
 *  Load an operand (variable or constant) into a register.
 *  Always returns a register index.
 *  For constants: li into a free/spilled register (not tracked as var)
 * ============================================================ */
static int load_operand(Operand *op, int *nu, int pos, int len) {
    if (!op) return -1;
    if (op->kind == OPD_CONSTANT) {
        for (int i = 0; i < NUM_REGS; i++) if (regs[i].free) {
            regs[i].free = 0;
            emit("  li %s, %d", reg_names[i], op->value);
            return i;
        }
        int r = pick_spill(nu, pos, len); free_reg(r);
        emit("  li %s, %d", reg_names[r], op->value);
        regs[r].free = 0;
        return r;
    }
    if (op->kind == OPD_ADDRESS) {
        /* Address of a variable: compute $sp + offset */
        int vi = var_id(0, op->no);
        if (vi >= 0) {
            for (int i = 0; i < NUM_REGS; i++) if (regs[i].free) {
                regs[i].free = 0;
                emit("  addi %s, $sp, %d", reg_names[i], var_tab[vi].offset);
                return i;
            }
            int r = pick_spill(nu, pos, len); free_reg(r);
            emit("  addi %s, $sp, %d", reg_names[r], var_tab[vi].offset);
            regs[r].free = 0;
            return r;
        }
        return -1;
    }
    return ensure(var_id_of(op), nu, pos, len);
}
static void free_temp_reg(int r) {
    if (r < 0 || r >= NUM_REGS) return;
    if (regs[r].var_idx < 0) regs[r].free = 1;
}

/* ============================================================
 *  Basic block structure
 * ============================================================ */
static int is_leader(InterCode *c, InterCode *prev) {
    if (!prev) return 1;
    if (c->kind == IR_LABEL || c->kind == IR_FUNCTION) return 1;
    if (prev->kind == IR_GOTO || prev->kind == IR_RETURN) return 1;
    return 0;
}
static int is_terminator(InterCode *c) {
    return (c->kind == IR_GOTO || c->kind == IR_RETURN);
}

/* ============================================================
 *  Translate a single basic block to MIPS
 * ============================================================ */
static void translate_bb(InterCode **instrs, int len, int frame_size, const char *epi_label) {
    if (len == 0) return;
    (void)frame_size;
    reg_init();
    int *nu = compute_next_use(instrs, len);

    /* buffered arguments for CALL */
    int buf_vi[128], buf_imm[128], num_args = 0;

    for (int pos = 0; pos < len; pos++) {
        InterCode *c = instrs[pos];

        /* Spill before terminators so stores are reachable */
        if (c->kind == IR_GOTO || c->kind == IR_RETURN)
            spill_all();

        switch (c->kind) {

        case IR_LABEL:
            emit("label%d:", c->arg1->no);
            break;

        case IR_FUNCTION:
            /* already emitted by translate_func */
            break;

        /* ----- assignment / arithmetic ----- */
        case IR_ASSIGN: {
            int vr = var_id_of(c->result);
            if (c->arg1->kind == OPD_CONSTANT) {
                int rz = alloc_result(vr, nu, pos, len);
                emit("  li %s, %d", reg_names[rz], c->arg1->value);
            } else {
                int r1 = load_operand(c->arg1, nu, pos, len);
                int rz = alloc_result(vr, nu, pos, len);
                if (rz != r1) emit("  move %s, %s", reg_names[rz], reg_names[r1]);
                free_temp_reg(r1);
            }
            break;
        }

        case IR_ADD:
        case IR_SUB:
        case IR_MUL:
        case IR_DIV: {
            int vr = var_id_of(c->result);
            int r1 = load_operand(c->arg1, nu, pos, len);
            int r2 = load_operand(c->arg2, nu, pos, len);
            /* Free operands not used later BEFORE allocate result (textbook Fig 5.10) */
            free_if_dead(var_id_of(c->arg1), nu, pos);
            free_if_dead(var_id_of(c->arg2), nu, pos);
            int rz = alloc_result(vr, nu, pos, len);
            const char *opname = (c->kind == IR_ADD) ? "add" :
                                 (c->kind == IR_SUB) ? "sub" :
                                 (c->kind == IR_MUL) ? "mul" : "div";
            emit("  %s %s, %s, %s", opname, reg_names[rz], reg_names[r1], reg_names[r2]);
            free_temp_reg(r1); free_temp_reg(r2);
            break;
        }

        /* ----- control flow ----- */
        case IR_GOTO:
            emit("  j label%d", c->arg1->no);
            break;

        case IR_RETURN:
            if (c->arg1) {
                if (c->arg1->kind == OPD_CONSTANT)
                    emit("  li $v0, %d", c->arg1->value);
                else {
                    int r1 = load_operand(c->arg1, nu, pos, len);
                    emit("  move $v0, %s", reg_names[r1]);
                    free_temp_reg(r1);
                }
            }
            emit("  j %s", epi_label);
            break;

        case IR_IF: {
            /* Spill dirty regs before branch to ensure memory is up-to-date */
            spill_all();
            for (int i = 0; i < var_count; i++) var_tab[i].in_reg = -1;
            reg_init();
            int r1 = load_operand(c->arg1, nu, pos, len);
            int r2 = load_operand(c->arg2, nu, pos, len);
            const char *rel = c->relop;
            const char *bop = NULL;
            if      (!strcmp(rel,">"))  bop = "bgt";
            else if (!strcmp(rel,">=")) bop = "bge";
            else if (!strcmp(rel,"<"))  bop = "blt";
            else if (!strcmp(rel,"<=")) bop = "ble";
            else if (!strcmp(rel,"==")) bop = "beq";
            else if (!strcmp(rel,"!=")) bop = "bne";
            if (bop) emit("  %s %s, %s, label%d", bop, reg_names[r1], reg_names[r2], c->result->no);
            free_temp_reg(r1); free_temp_reg(r2);
            break;
        }

        /* ----- function calls ----- */
        case IR_ARG:
            if (num_args < 128) {
                if (c->arg1->kind == OPD_CONSTANT) {
                    buf_imm[num_args] = 1;
                    buf_vi[num_args]  = c->arg1->value;
                } else {
                    buf_imm[num_args] = 0;
                    buf_vi[num_args]  = var_id_of(c->arg1);
                }
                num_args++;
            }
            break;

        case IR_CALL: {
            int n = num_args; num_args = 0;
            int stk = (n > 4) ? (n - 4) : 0;

            /* Load $a0-$a3 first, while $sp is still un-shifted */
            int ac = (n < 4) ? n : 4;
            for (int k = 0; k < ac; k++) {
                int idx = n - 1 - k;
                if (buf_imm[idx])
                    emit("  li $a%d, %d", k, buf_vi[idx]);
                else {
                    int r = ensure(buf_vi[idx], nu, pos, len);
                    emit("  move $a%d, %s", k, reg_names[r]);
                }
            }

            /* Spill caller-saved regs to home slots before touching $sp */
            spill_all();
            for (int i = 0; i < var_count; i++) var_tab[i].in_reg = -1;
            reg_init();

            /* Push extra args (>4) to stack, reading from home slots.
               $sp is shifted by stk*4, so home offsets shift accordingly. */
            if (stk > 0) {
                emit("  addi $sp, $sp, -%d", stk * 4);
                for (int k = 0; k < stk; k++) {
                    int idx = n - 5 - k;
                    if (buf_imm[idx]) {
                        emit("  li $t9, %d", buf_vi[idx]);
                    } else {
                        emit("  lw $t9, %d($sp)", var_tab[buf_vi[idx]].offset + stk * 4);
                    }
                    emit("  sw $t9, %d($sp)", k * 4);
                }
            }

            /* Call */
            emit("  addi $sp, $sp, -4");
            emit("  sw $ra, 0($sp)");
            emit("  jal %s", safe_label(c->arg1->name));
            emit("  lw $ra, 0($sp)");
            emit("  addi $sp, $sp, 4");

            /* Pop stack args */
            if (stk > 0) emit("  addi $sp, $sp, %d", stk * 4);

            /* Result */
            int vr = var_id_of(c->result);
            if (vr >= 0) {
                int rz = alloc_result(vr, nu, pos, len);
                emit("  move %s, $v0", reg_names[rz]);
            }
            break;
        }

        case IR_READ: {
            spill_all();
            for (int i = 0; i < var_count; i++) var_tab[i].in_reg = -1;
            reg_init();
            emit("  addi $sp, $sp, -4");
            emit("  sw $ra, 0($sp)");
            emit("  jal read");
            emit("  lw $ra, 0($sp)");
            emit("  addi $sp, $sp, 4");
            int vr = var_id_of(c->arg1);
            if (vr >= 0) {
                int rz = alloc_result(vr, nu, pos, len);
                emit("  move %s, $v0", reg_names[rz]);
            }
            break;
        }

        case IR_WRITE:
            /* Load arg before flushing registers */
            if (c->arg1->kind == OPD_CONSTANT)
                emit("  li $a0, %d", c->arg1->value);
            else {
                int r1 = load_operand(c->arg1, nu, pos, len);
                emit("  move $a0, %s", reg_names[r1]);
                free_temp_reg(r1);
            }
            /* Spill dirty regs + clear tracking before call */
            spill_all();
            for (int i = 0; i < var_count; i++) var_tab[i].in_reg = -1;
            reg_init();
            emit("  addi $sp, $sp, -4");
            emit("  sw $ra, 0($sp)");
            emit("  jal write");
            emit("  lw $ra, 0($sp)");
            emit("  addi $sp, $sp, 4");
            break;

        case IR_LOAD: {
            int vi_a = var_id_of(c->arg1);
            int vi_r = var_id_of(c->result);
            int ra = ensure(vi_a, nu, pos, len);
            int rz = alloc_result(vi_r, nu, pos, len);
            emit("  lw %s, 0(%s)", reg_names[rz], reg_names[ra]);
            break;
        }

        case IR_STORE: {
            /* result = addr, arg1 = value */
            int vi_a = var_id_of(c->result);
            int vi_v = var_id_of(c->arg1);
            int ra = ensure(vi_a, nu, pos, len);
            int rv = ensure(vi_v, nu, pos, len);
            emit("  sw %s, 0(%s)", reg_names[rv], reg_names[ra]);
            break;
        }

        default: break;
        }

        /* Free operands that have no further use */
        if (c->arg1) free_if_dead(var_id_of(c->arg1), nu, pos);
        if (c->arg2) free_if_dead(var_id_of(c->arg2), nu, pos);
        if (c->kind == IR_STORE && c->result) free_if_dead(var_id_of(c->result), nu, pos);
    }

    /* Spill all modified regs at end of block (for blocks without terminator) */
    int last_pos = len - 1;
    if (last_pos >= 0) {
        InterCode *lc = instrs[last_pos];
        if (lc->kind != IR_GOTO && lc->kind != IR_RETURN)
            spill_all();
    }
    free(nu);
}

/* ============================================================
 *  Translate one function
 * ============================================================ */
static void translate_func(InterCode *start, InterCode *end) {
    /* Reset per-function state */
    for (int i = 0; i < var_count; i++)
        { var_tab[i].used = 0; var_tab[i].is_param = 0; var_tab[i].offset = -1; }

    int param_count = 0, dec_bytes = 0;
    const char *fname = "unknown";

    /* Mark used vars, identify params, track DEC sizes */
    int *dec_size = calloc(var_count, sizeof(int));
    for (InterCode *c = start; c && c != end; c = c->next) {
        if (c->kind == IR_FUNCTION) fname = c->arg1->name;
        if (c->kind == IR_PARAM) {
            int vi = var_id_of(c->arg1);
            if (vi >= 0) { var_tab[vi].used = 1; var_tab[vi].is_param = 1; }
            param_count++;
        }
        if (c->kind == IR_DEC) {
            int vi = var_id_of(c->arg1);
            if (vi >= 0) dec_size[vi] = c->size;
            dec_bytes += c->size;
        }

        Operand *ops[4] = { c->result, c->arg1, c->arg2, c->result };
        int n = (c->kind == IR_STORE) ? 4 : 3;
        for (int k = 0; k < n; k++) {
            if (ops[k] && (ops[k]->kind == OPD_VARIABLE || ops[k]->kind == OPD_TEMP)) {
                int vi = var_id_of(ops[k]);
                if (vi >= 0) var_tab[vi].used = 1;
            }
        }
    }

    /* Stack layout: $ra at frame-4, vars at offsets upward */
    int off = 0;
    /* non-param, non-DEC user vars: 4 bytes each */
    for (int i = 0; i < var_count; i++)
        if (var_tab[i].used && var_tab[i].kind == 0 && !var_tab[i].is_param && !dec_size[i])
            { var_tab[i].offset = off; off += 4; }
    /* param, non-DEC vars: 4 bytes each */
    for (int i = 0; i < var_count; i++)
        if (var_tab[i].used && var_tab[i].kind == 0 && var_tab[i].is_param && !dec_size[i])
            { var_tab[i].offset = off; off += 4; }
    /* temps: 4 bytes each */
    for (int i = 0; i < var_count; i++)
        if (var_tab[i].used && var_tab[i].kind == 1 && !dec_size[i])
            { var_tab[i].offset = off; off += 4; }
    /* DEC vars (arrays): live in DEC space, N bytes each */
    for (int i = 0; i < var_count; i++)
        if (var_tab[i].used && dec_size[i] > 0)
            { var_tab[i].offset = off; off += dec_size[i]; }
    /* frame = total + $ra (4 bytes) */
    int frame = off + 4;
    if (frame % 8) frame = (frame / 8 + 1) * 8;
    free(dec_size);

    /* Epilogue label */
    char epi_label[64];
    snprintf(epi_label, sizeof(epi_label), "__%s_epilogue", fname);

    /* Function header + prologue */
    emit("%s:", safe_label(fname));
    emit("  subu $sp, $sp, %d", frame);
    emit("  sw $ra, %d($sp)", frame - 4);

    /* Zero-init user vars (non-param) */
    for (int i = 0; i < var_count; i++) {
        if (!var_tab[i].used || var_tab[i].kind != 0 || var_tab[i].is_param) continue;
        int sz = 4; /* default 4 bytes */
        /* If this var had a DEC, use its size */
        for (InterCode *c = start; c && c != end; c = c->next)
            if (c->kind == IR_DEC && var_id_of(c->arg1) == i) { sz = c->size; break; }
        for (int b = 0; b < sz; b += 4)
            emit("  sw $0, %d($sp)", var_tab[i].offset + b);
    }

    /* Zero-init temps */
    for (int i = 0; i < var_count; i++)
        if (var_tab[i].used && var_tab[i].kind == 1)
            emit("  sw $0, %d($sp)", var_tab[i].offset);

    /* Load params AFTER zero-init (overwrites zeros) */
    int pi = 0;
    for (InterCode *c = start; c && c != end; c = c->next) {
        if (c->kind != IR_PARAM) continue;
        int vi = var_id_of(c->arg1);
        if (vi < 0) { pi++; continue; }
        if (pi < 4)
            emit("  sw $a%d, %d($sp)", pi, var_tab[vi].offset);
        else {
            int extra = pi - 4;
            emit("  lw $t9, %d($sp)", frame + 4 + extra * 4);
            emit("  sw $t9, %d($sp)", var_tab[vi].offset);
        }
        pi++;
    }

    /* Build basic blocks */
    InterCode *cur = start;
    while (cur && cur != end) {
        int cap = 64, len = 0;
        InterCode **bb = malloc(cap * sizeof(InterCode *));
        InterCode *prev = NULL;

        while (cur && cur != end) {
            if (len > 0 && is_leader(cur, prev)) break;
            if (len >= cap) { cap *= 2; bb = realloc(bb, cap * sizeof(InterCode *)); }
            bb[len++] = cur;
            prev = cur;
            if (is_terminator(cur)) { cur = cur->next; break; }
            cur = cur->next;
        }

        translate_bb(bb, len, frame, epi_label);
        free(bb);
    }

    /* Epilogue */
    emit("%s:", epi_label);
    emit("  lw $ra, %d($sp)", frame - 4);
    emit("  addi $sp, $sp, %d", frame);
    emit("  jr $ra");
    emit("");
}

/* ============================================================
 *  Entry point
 * ============================================================ */
void generate_mips(FILE *out) {
    fout = out;
    InterCode *ir = ir_get_head();
    if (!ir) return;
    pre_scan(ir);

    emit(".data");
    emit("_prompt: .asciiz \"Enter an integer:\"");
    emit("_ret:   .asciiz \"\\n\"");
    emit(".globl main");
    emit(".text");
    emit("");
    emit("read:");
    emit("  li $v0, 4");
    emit("  la $a0, _prompt");
    emit("  syscall");
    emit("  li $v0, 5");
    emit("  syscall");
    emit("  jr $ra");
    emit("");
    emit("write:");
    emit("  li $v0, 1");
    emit("  syscall");
    emit("  li $v0, 4");
    emit("  la $a0, _ret");
    emit("  syscall");
    emit("  move $v0, $0");
    emit("  jr $ra");
    emit("");

    /* Iterate functions */
    InterCode *c = ir;
    while (c) {
        if (c->kind == IR_FUNCTION) {
            InterCode *fend = c->next;
            while (fend && fend->kind != IR_FUNCTION) fend = fend->next;
            translate_func(c, fend);
            c = fend;
        } else {
            c = c->next;
        }
    }

    free(var_tab);
}
