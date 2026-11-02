/*
 * BASM — Basalt Assembly assembler + ELF64 linker (single unit).
 * Subset tailored to Basalt: slots, frame, li/load/store, arith, jumps, calls.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <unistd.h>
#include "bopt_lib.h"
#include <unistd.h>

#define MAX_LINE 512
#define MAX_LABELS 16384
#define MAX_RELOCS 32768
#define MAX_CODE (1<<20)
#define MAX_RODATA (1<<18)
#define MAX_SLOTS 2048
#define MAX_FUNCS 1024

typedef struct { char name[64]; int32_t off; int defined; } Label;
typedef struct { int32_t at; char name[64]; int kind; /* 0=rel32 call/jmp, 1=rel32 lea */ } Reloc;
typedef struct { char name[64]; int32_t off; } Slot;
typedef struct { char name[64]; int32_t addr; int32_t frame; int has_enter; } Func;

static uint8_t code[MAX_CODE];
static int32_t code_len;
static uint8_t rodata[MAX_RODATA];
static int32_t rodata_len;

static Label labels[MAX_LABELS];
static int nlabels;
static Reloc relocs[MAX_RELOCS];
static int nrelocs;
static Slot slots[MAX_SLOTS];
static int nslots;
static Func funcs[MAX_FUNCS];
static int nfuncs;

static int32_t cur_frame = 256;
static int32_t next_slot_off = 8;
static char cur_func[64];
static int verbose;
static char errbuf[256];

static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "basm: error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

static void emit(uint8_t b) {
    if (code_len >= MAX_CODE) die("code too large");
    code[code_len++] = b;
}
static void emit32(uint32_t v) {
    emit(v); emit(v>>8); emit(v>>16); emit(v>>24);
}
static void emit64(uint64_t v) {
    emit32((uint32_t)v); emit32((uint32_t)(v>>32));
}

/* Physical: r0=rax r1=rcx r2=rdx r3=rsi r4=rdi r5=r8 r6=r9 r7=r10
 *           r8=r11 r9=rbx r10=r12 r11=r13 r12=r14 r13=r15
 * Virtual:  r14..r4095 — linear-scan allocated to phys or spill */
#define NPHYS 14
#define MAX_VREG 4096
static int reg_id(const char *s) {
    if (!s || s[0] != 'r') return -1;
    char *e = NULL;
    long n = strtol(s + 1, &e, 10);
    if (e == s + 1 || *e) return -1;
    if (n < 0 || n >= MAX_VREG) return -1;
    return (int)n;
}
static int arg_id(const char *s) {
    if (!s || s[0] != 'a') return -1;
    if (s[1] >= '0' && s[1] <= '5' && s[2] == 0) return s[1] - '0';
    return -1;
}
/* phys index → ModRM reg field + REX.B/R bit */
static const int reg_rm[NPHYS]  = { 0,1,2,6,7,0,1,2,3,3,4,5,6,7 };
static const int reg_rex[NPHYS] = { 0,0,0,0,0,1,1,1,1,0,1,1,1,1 };

/* ---- Virtual register allocation (linear scan) ---- */
static char *fn_lines[8192];
static int fn_nlines;
static int vmap[MAX_VREG];          /* vreg → phys (0..NPHYS-1) or -1 spill */
static int spill_slot[MAX_VREG];    /* stack offset for spilled vreg */
static int next_spill;
static int phys_free[NPHYS];

static void ra_reset(void) {
    for (int i = 0; i < MAX_VREG; i++) { vmap[i] = -2; spill_slot[i] = 0; }
    for (int i = 0; i < NPHYS; i++) phys_free[i] = 1;
    /* reserve r0 as scratch for spill reloads sometimes — keep allocatable */
    next_spill = 2048; /* high frame area for spills */
    for (int i = 0; i < fn_nlines; i++) free(fn_lines[i]);
    fn_nlines = 0;
}

static void ra_note_vreg(int v) {
    if (v < 0) return;
    if (v < NPHYS) { vmap[v] = v; return; } /* identity for low regs used as phys */
    if (vmap[v] == -2) vmap[v] = -1; /* mark seen, unallocated */
}

static void ra_scan_line(const char *line) {
    const char *p = line;
    while (*p) {
        if (*p == 'r' && isdigit((unsigned char)p[1])) {
            int v = reg_id(p);
            if (v >= 0) ra_note_vreg(v);
            while (*p && (isalnum((unsigned char)*p))) p++;
        } else p++;
    }
}

static int ra_pick_phys(void) {
    /* caller-saved first (avoid rbp/rsp); leave r0 free-ish for spills by preferring others */
    static const int order[] = {1,2,3,5,6,7,8,4,9,10,11,12,13,0};
    for (int i = 0; i < NPHYS; i++) {
        int p = order[i];
        if (phys_free[p]) { phys_free[p] = 0; return p; }
    }
    return -1;
}

/* Evict least-recently-used phys when needed (simple global RA pressure) */
static int phys_lru[NPHYS];
static int phys_clock;
static int ra_pick_or_evict(int *evicted_v) {
    int p = ra_pick_phys();
    if (p >= 0) { phys_lru[p] = ++phys_clock; return p; }
    /* find LRU phys among 1..13 (keep r0 scratch) */
    int best = 1, best_t = phys_lru[1];
    for (int i = 2; i < NPHYS; i++) {
        if (phys_lru[i] < best_t) { best_t = phys_lru[i]; best = i; }
    }
    /* unmap any vreg pointing at best */
    *evicted_v = -1;
    for (int v = NPHYS; v < MAX_VREG; v++) {
        if (vmap[v] == best) {
            vmap[v] = -1;
            if (!spill_slot[v]) { spill_slot[v] = next_spill; next_spill += 8; }
            *evicted_v = v;
            break;
        }
    }
    phys_free[best] = 0;
    phys_lru[best] = ++phys_clock;
    return best;
}

static void ra_allocate(void) {
    for (int i = 0; i < fn_nlines; i++) ra_scan_line(fn_lines[i]);
    for (int v = 0; v < MAX_VREG; v++) {
        if (vmap[v] == -2) continue;
        if (v < NPHYS) { vmap[v] = v; phys_free[v] = 0; continue; }
        int p = ra_pick_phys();
        if (p >= 0) vmap[v] = p;
        else {
            vmap[v] = -1;
            spill_slot[v] = next_spill;
            next_spill += 8;
        }
    }
}

static void ra_rewrite_line(char *dst, size_t dstsz, const char *src) {
    size_t o = 0;
    const char *p = src;
    while (*p && o + 8 < dstsz) {
        if (*p == 'r' && isdigit((unsigned char)p[1])) {
            int v = reg_id(p);
            if (v >= 0) {
                int phys = (v < NPHYS) ? v : (vmap[v] >= 0 ? vmap[v] : -1);
                if (phys >= 0) {
                    o += (size_t)snprintf(dst + o, dstsz - o, "r%d", phys);
                } else {
                    /* spilled: keep as special slot ref — encode path uses spill */
                    o += (size_t)snprintf(dst + o, dstsz - o, "r0"); /* filled by spill inject */
                }
                while (*p && (isalnum((unsigned char)*p))) p++;
                continue;
            }
        }
        dst[o++] = *p++;
    }
    dst[o] = 0;
}

static Label *label_get(const char *name, int define) {
    for (int i = 0; i < nlabels; i++)
        if (!strcmp(labels[i].name, name)) {
            if (define) {
                labels[i].off = code_len;
                labels[i].defined = 1;
            }
            return &labels[i];
        }
    if (nlabels >= MAX_LABELS) die("too many labels");
    Label *L = &labels[nlabels++];
    memset(L, 0, sizeof(*L));
    snprintf(L->name, sizeof(L->name), "%s", name);
    if (define) { L->off = code_len; L->defined = 1; }
    return L;
}

static int32_t slot_off(const char *name) {
    for (int i = 0; i < nslots; i++)
        if (!strcmp(slots[i].name, name)) return slots[i].off;
    /* auto-declare */
    if (nslots >= MAX_SLOTS) die("too many slots");
    Slot *s = &slots[nslots++];
    snprintf(s->name, sizeof(s->name), "%s", name);
    s->off = next_slot_off;
    next_slot_off += 8;
    return s->off;
}

static void add_reloc(const char *name, int kind) {
    if (nrelocs >= MAX_RELOCS) die("too many relocs");
    Reloc *r = &relocs[nrelocs++];
    r->at = code_len;
    snprintf(r->name, sizeof(r->name), "%s", name);
    r->kind = kind;
    emit32(0); /* patch later */
}

/* ---- encoders ---- */
static void enc_push_rbp(void) { emit(0x55); }
static void enc_pop_rbp(void)  { emit(0x5d); }
static void enc_ret(void)      { emit(0xc3); }
static void enc_leave(void)    { emit(0xc9); }
static void enc_cqo(void)      { emit(0x48); emit(0x99); }

static void enc_mov_rr(int d, int s) {
    /* mov %rs, %rd  (64-bit) */
    uint8_t rex = 0x48 | (reg_rex[s]<<2) | reg_rex[d];
    emit(rex); emit(0x89);
    emit(0xc0 | (reg_rm[s]<<3) | reg_rm[d]);
}

static void enc_mov_ri(int d, int64_t imm) {
    if (imm == 0) {
        /* xor %reg,%reg — shorter & clears upper */
        uint8_t rex = 0x48 | (reg_rex[d]<<2) | reg_rex[d];
        emit(rex); emit(0x31); emit(0xc0 | (reg_rm[d]<<3) | reg_rm[d]);
        return;
    }
    if (imm >= INT32_MIN && imm <= INT32_MAX && !reg_rex[d]) {
        emit(0x48); emit(0xc7); emit(0xc0 | reg_rm[d]); emit32((uint32_t)(int32_t)imm);
    } else if (!reg_rex[d]) {
        emit(0x48); emit(0xb8 | reg_rm[d]); emit64((uint64_t)imm);
    } else {
        emit(0x49); emit(0xb8 | reg_rm[d]); emit64((uint64_t)imm);
    }
}

static void enc_load_slot(int d, int32_t off) {
    /* mov -off(%rbp), %rd */
    uint8_t rex = 0x48 | reg_rex[d];
    emit(rex); emit(0x8b);
    if (off <= 127) {
        emit(0x45 | (reg_rm[d]<<3));
        emit((uint8_t)(int8_t)(-off));
    } else {
        emit(0x85 | (reg_rm[d]<<3));
        emit32((uint32_t)(-off));
    }
}

static void enc_store_slot(int32_t off, int s) {
    /* mov %rs, -off(%rbp) */
    uint8_t rex = 0x48 | (reg_rex[s]<<2);
    emit(rex); emit(0x89);
    if (off <= 127) {
        emit(0x45 | (reg_rm[s]<<3));
        emit((uint8_t)(int8_t)(-off));
    } else {
        emit(0x85 | (reg_rm[s]<<3));
        emit32((uint32_t)(-off));
    }
}



static void enc_load_mem(int d, int s) {
    /* mov (%rs), %rd  — systems: load through pointer */
    uint8_t rex = 0x48 | (reg_rex[s]<<2) | reg_rex[d];
    emit(rex); emit(0x8b);
    if (reg_rm[s] == 4) { /* rsp needs SIB */
        emit(0x04 | (reg_rm[d]<<3)); emit(0x24);
    } else if (reg_rm[s] == 5) { /* rbp needs disp0 */
        emit(0x45 | (reg_rm[d]<<3)); emit(0);
    } else {
        emit(0x00 | (reg_rm[d]<<3) | reg_rm[s]);
    }
}

static void enc_store_mem(int d, int s) {
    /* mov %rs, (%rd)  — systems: store through pointer */
    uint8_t rex = 0x48 | (reg_rex[s]<<2) | reg_rex[d];
    emit(rex); emit(0x89);
    if (reg_rm[d] == 4) {
        emit(0x04 | (reg_rm[s]<<3)); emit(0x24);
    } else if (reg_rm[d] == 5) {
        emit(0x45 | (reg_rm[s]<<3)); emit(0);
    } else {
        emit(0x00 | (reg_rm[s]<<3) | reg_rm[d]);
    }
}

static void enc_and_rr(int d, int s) {
    uint8_t rex = 0x48 | (reg_rex[s]<<2) | reg_rex[d];
    emit(rex); emit(0x21); emit(0xc0 | (reg_rm[s]<<3) | reg_rm[d]);
}
static void enc_or_rr(int d, int s) {
    uint8_t rex = 0x48 | (reg_rex[s]<<2) | reg_rex[d];
    emit(rex); emit(0x09); emit(0xc0 | (reg_rm[s]<<3) | reg_rm[d]);
}
static void enc_xor_rr(int d, int s) {
    uint8_t rex = 0x48 | (reg_rex[s]<<2) | reg_rex[d];
    emit(rex); emit(0x31); emit(0xc0 | (reg_rm[s]<<3) | reg_rm[d]);
}
static void enc_andi(int d, int32_t imm) {
    uint8_t rex = 0x48 | reg_rex[d];
    emit(rex); emit(0x81); emit(0xe0 | reg_rm[d]); emit32((uint32_t)imm);
}
static void enc_shli(int d, int imm8) {
    uint8_t rex = 0x48 | reg_rex[d];
    emit(rex); emit(0xc1); emit(0xe0 | reg_rm[d]); emit((uint8_t)imm8);
}
static void enc_sari(int d, int imm8) {
    uint8_t rex = 0x48 | reg_rex[d];
    emit(rex); emit(0xc1); emit(0xf8 | reg_rm[d]); emit((uint8_t)imm8);
}
static void enc_shri(int d, int imm8) {
    uint8_t rex = 0x48 | reg_rex[d];
    emit(rex); emit(0xc1); emit(0xe8 | reg_rm[d]); emit((uint8_t)imm8);
}

static void enc_binop(int op /*0add 1sub 2imul*/, int d, int s) {
    /* ADD/SUB: dest is r/m (B extends d), src is reg (R extends s)
       IMUL:    dest is reg (R extends d), src is r/m (B extends s) */
    if (op == 2) {
        uint8_t rex = 0x48 | (reg_rex[d]<<2) | reg_rex[s];
        emit(rex); emit(0x0f); emit(0xaf); emit(0xc0 | (reg_rm[d]<<3) | reg_rm[s]);
        return;
    }
    uint8_t rex = 0x48 | (reg_rex[s]<<2) | reg_rex[d];
    emit(rex);
    if (op == 0) { emit(0x01); emit(0xc0 | (reg_rm[s]<<3) | reg_rm[d]); }
    else { emit(0x29); emit(0xc0 | (reg_rm[s]<<3) | reg_rm[d]); }
}
static void enc_addi(int d, int64_t imm) {
    if (imm >= -128 && imm <= 127) {
        uint8_t rex = 0x48 | reg_rex[d];
        emit(rex); emit(0x83); emit(0xc0 | reg_rm[d]); emit((uint8_t)(int8_t)imm);
        return;
    }
    if (imm >= INT32_MIN && imm <= INT32_MAX) {
        uint8_t rex = 0x48 | reg_rex[d];
        emit(rex); emit(0x81); emit(0xc0 | reg_rm[d]); emit32((uint32_t)(int32_t)imm);
        return;
    }
    enc_mov_ri(0, imm); enc_binop(0, d, 0);
}
static void enc_inc(int d) {
    uint8_t rex = 0x48 | reg_rex[d];
    emit(rex); emit(0xff); emit(0xc0 | reg_rm[d]);
}


static void enc_idiv(int s) {
    /* cqo; idiv %rs */
    enc_cqo();
    uint8_t rex = 0x48 | reg_rex[s];
    emit(rex); emit(0xf7); emit(0xf8 | reg_rm[s]);
}

static void enc_cmp_rr(int a, int b) {
    uint8_t rex = 0x48 | (reg_rex[b]<<2) | reg_rex[a];
    emit(rex); emit(0x39); emit(0xc0 | (reg_rm[b]<<3) | reg_rm[a]);
}

static void enc_cmp_ri(int a, int64_t imm) {
    /* 32-bit signed immediate fits in cmp $imm32, %reg */
    if (imm >= INT32_MIN && imm <= INT32_MAX) {
        uint8_t rex = 0x48 | reg_rex[a];
        emit(rex); emit(0x81); emit(0xf8 | reg_rm[a]); emit32((uint32_t)(int32_t)imm);
        return;
    }
    /* large: movabs imm -> r0 (scratch), then cmp %r0, %ra  (ra - imm) */
    enc_mov_ri(0, imm);
    enc_cmp_rr(a, 0);
}

static void enc_jcc(int cc /*0e 1ne 2l 3le 4g 5ge*/, const char *lab) {
    emit(0x0f);
    static const uint8_t map[] = { 0x84, 0x85, 0x8c, 0x8e, 0x8f, 0x8d };
    emit(map[cc]);
    add_reloc(lab, 0);
}

static void enc_jmp(const char *lab) {
    emit(0xe9);
    add_reloc(lab, 0);
}

static void enc_call(const char *name) {
    emit(0xe8);
    add_reloc(name, 0);
}

static void enc_sub_rsp(int32_t n) {
    emit(0x48); emit(0x81); emit(0xec); emit32((uint32_t)n);
}

static void enc_lea_str(int d, const char *lab) {
    /* leaq lab(%rip), %rd */
    uint8_t rex = 0x48 | reg_rex[d];
    emit(rex); emit(0x8d);
    emit(0x05 | (reg_rm[d]<<3));
    add_reloc(lab, 1); /* rip-rel */
}

static void enc_enter_frame(int32_t frame) {
    enc_push_rbp();
    /* mov %rsp, %rbp */
    emit(0x48); emit(0x89); emit(0xe5);
    if (frame > 0) enc_sub_rsp(frame);
}

static void enc_ret_frame(void) {
    enc_leave();
    enc_ret();
}

/* map a0..a5 to physical for arg */
static int arg_to_reg(int a) {
    static const int m[] = { 4, 3, 2, 1, 5, 6 }; /* rdi rsi rdx rcx r8 r9 */
    return m[a];
}

/* ---- parse helpers ---- */
static char *skip_ws(char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}
static char *tok(char **pp, char *buf, int n) {
    char *p = skip_ws(*pp);
    if (!*p) { buf[0]=0; *pp=p; return buf; }
    int i = 0;
    if (*p == '"') {
        buf[i++] = *p++;
        while (*p && *p != '"' && i < n-2) {
            if (*p == '\\' && p[1]) { buf[i++]=*p++; buf[i++]=*p++; }
            else buf[i++]=*p++;
        }
        if (*p == '"') buf[i++]=*p++;
        buf[i]=0; *pp=p; return buf;
    }
    while (*p && !isspace((unsigned char)*p) && *p != ',' && *p != ';' && i < n-1)
        buf[i++] = *p++;
    buf[i]=0;
    if (*p == ',') p++;
    *pp = p;
    return buf;
}

static int64_t parse_imm(const char *s) {
    if (!s || !*s) return 0;
    if (s[0]=='0' && (s[1]=='x'||s[1]=='X')) return strtoll(s, NULL, 16);
    if (s[0]=='0' && (s[1]=='b'||s[1]=='B')) return strtoll(s+2, NULL, 2);
    return strtoll(s, NULL, 10);
}

/* runtime symbols resolved at link to PLT-like stubs that call via reloc table
 * For v0.1 we emit calls as e8 rel32 and at the end append trampolines that
 * jump through a GOT filled by the ELF loader for libc-backed runtime —
 * simpler: define known runtime as external undef symbols in ELF.
 */
typedef struct { char name[64]; int32_t plt_off; } Ext;
static Ext exts[64];
static int nexts;

static int32_t extern_plt(const char *name) {
    for (int i = 0; i < nexts; i++)
        if (!strcmp(exts[i].name, name)) return exts[i].plt_off;
    /* create a local stub label __ext_name that will be an undef reloc */
    if (nexts >= 64) die("too many externs");
    Ext *e = &exts[nexts++];
    snprintf(e->name, sizeof(e->name), "%s", name);
    /* stub placed later */
    e->plt_off = -1;
    return -1;
}

static const char *runtime_map(const char *name) {
    if (!strcmp(name, "print_int")) return "basalt_print_int";
    if (!strcmp(name, "print_str")) return "basalt_print_str";
    if (!strcmp(name, "print_nl"))  return "basalt_print_nl";
    if (!strcmp(name, "panic"))     return "basalt_panic";
    if (!strcmp(name, "strlen"))    return "basalt_strlen";
    if (!strcmp(name, "alloc"))     return "basalt_alloc";
    if (!strcmp(name, "free"))      return "basalt_free";
    return name;
}


/* Online vreg → phys (linear-scan greedy). Spill to [rbp-off]. */
static int phys_resolve(int v) {
    if (v < 0) return -1;
    if (v < NPHYS) { if (v < NPHYS) phys_lru[v] = ++phys_clock; return v; }
    if (vmap[v] >= 0) { phys_lru[vmap[v]] = ++phys_clock; return vmap[v]; }
    if (vmap[v] == -1) return -1;
    int ev = -1;
    int p = ra_pick_or_evict(&ev);
    if (p < 0) {
        vmap[v] = -1;
        if (!spill_slot[v]) { spill_slot[v] = next_spill; next_spill += 8; }
        return -1;
    }
    /* if we evicted someone, their value is on stack already when spilled next write */
    vmap[v] = p;
    phys_lru[p] = ++phys_clock;
    return p;
}
static int use_src(int v) {
    int p = phys_resolve(v);
    if (p >= 0) return p;
    enc_load_slot(0, spill_slot[v]);
    return 0;
}
static int use_dst(int v) {
    int p = phys_resolve(v);
    if (p >= 0) return p;
    return 0; /* accumulate in r0 */
}
static void commit_dst(int v, int p) {
    if (v >= NPHYS && vmap[v] == -1)
        enc_store_slot(spill_slot[v], p);
}

/* ---- line assembler ---- */
static void assemble_line(char *line, int lineno) {
    char *p = line;
    char a[128], b[128], c[128];
    /* strip comment */
    for (char *q = p; *q; q++) {
        if (*q == ';' || (q[0]=='/' && q[1]=='/')) { *q = 0; break; }
    }
    p = skip_ws(p);
    if (!*p) return;

    /* label: */
    char *colon = strchr(p, ':');
    if (colon && colon > p) {
        int ok = 1;
        for (char *q = p; q < colon; q++)
            if (!(isalnum((unsigned char)*q) || *q=='_' || *q=='.')) ok = 0;
        if (ok) {
            *colon = 0;
            label_get(p, 1);
            p = skip_ws(colon + 1);
            if (!*p) return;
        }
    }

    tok(&p, a, sizeof a);
    if (!a[0]) return;

    if (a[0] == '.') {
        if (!strcmp(a, ".func")) {
            tok(&p, b, sizeof b);
            if (nfuncs >= MAX_FUNCS) die("too many funcs");
            Func *f = &funcs[nfuncs++];
            memset(f, 0, sizeof(*f));
            snprintf(f->name, sizeof(f->name), "%s", b);
            f->addr = code_len;
            f->frame = cur_frame;
            snprintf(cur_func, sizeof(cur_func), "%s", b);
            label_get(b, 1);
            nslots = 0;
            next_slot_off = 8;
            ra_reset();
            return;
        }
        if (!strcmp(a, ".frame")) {
            tok(&p, b, sizeof b);
            cur_frame = (int32_t)parse_imm(b);
            if (nfuncs) funcs[nfuncs-1].frame = cur_frame;
            return;
        }
        if (!strcmp(a, ".slot")) {
            tok(&p, b, sizeof b);
            slot_off(b);
            return;
        }
        if (!strcmp(a, ".end")) {
            cur_func[0] = 0;
            return;
        }
        if (!strcmp(a, ".str")) {
            tok(&p, b, sizeof b); /* label */
            tok(&p, c, sizeof c); /* "text" */
            if (c[0] != '"') die("line %d: .str expects string", lineno);
            /* strip quotes + basic escapes into rodata */
            Label *L = label_get(b, 0);
            L->off = rodata_len | 0x40000000; /* high bit mark: rodata */
            L->defined = 1;
            const char *s = c + 1;
            while (*s && *s != '"') {
                char ch = *s++;
                if (ch == '\\' && *s) {
                    char e = *s++;
                    if (e=='n') ch='\n';
                    else if (e=='t') ch='\t';
                    else if (e=='0') ch=0;
                    else ch = e;
                }
                if (rodata_len >= MAX_RODATA) die("rodata full");
                rodata[rodata_len++] = (uint8_t)ch;
            }
            rodata[rodata_len++] = 0;
            return;
        }
        if (!strcmp(a, ".tier")) {
            /* metadata only in v0.1 */
            return;
        }
        die("line %d: unknown directive %s", lineno, a);
    }

    tok(&p, b, sizeof b);
    tok(&p, c, sizeof c);

    if (!strcmp(a, "enter")) {
        enc_enter_frame(cur_frame);
        if (nfuncs) funcs[nfuncs-1].has_enter = 1;
        return;
    }
    if (!strcmp(a, "ret")) { enc_ret_frame(); return; }
    if (!strcmp(a, "reti")) {
        enc_mov_ri(0, parse_imm(b));
        enc_ret_frame();
        return;
    }
    if (!strcmp(a, "retv")) {
        int rs = reg_id(b);
        if (rs < 0) die("line %d: retv reg", lineno);
        if (rs != 0) enc_mov_rr(0, rs);
        enc_ret_frame();
        return;
    }
    if (!strcmp(a, "li")) {
        int vd = reg_id(b);
        if (vd < 0) die("line %d: li reg", lineno);
        int rd = use_dst(vd);
        enc_mov_ri(rd, parse_imm(c));
        commit_dst(vd, rd);
        return;
    }
    if (!strcmp(a, "mov")) {
        int vd = reg_id(b), vs = reg_id(c);
        if (vd < 0 || vs < 0) die("line %d: mov regs", lineno);
        int rs = use_src(vs);
        int rd = use_dst(vd);
        if (rd != rs) enc_mov_rr(rd, rs);
        else if (vd != vs) { /* same phys, still commit if spill dest */ }
        commit_dst(vd, rd);
        return;
    }
    if (!strcmp(a, "load")) {
        int vd = reg_id(b);
        if (vd < 0) die("line %d: load reg", lineno);
        int rd = use_dst(vd);
        enc_load_slot(rd, slot_off(c));
        commit_dst(vd, rd);
        return;
    }
    if (!strcmp(a, "store")) {
        int vs = reg_id(c);
        if (vs < 0) die("line %d: store reg", lineno);
        int rs = use_src(vs);
        enc_store_slot(slot_off(b), rs);
        return;
    }

    if (!strcmp(a, "loadm")) {
        int vd = reg_id(b), vs = reg_id(c);
        if (vd < 0 || vs < 0) die("line %d: loadm regs", lineno);
        int rs = use_src(vs);
        int rd = use_dst(vd);
        enc_load_mem(rd, rs);
        commit_dst(vd, rd);
        return;
    }
    if (!strcmp(a, "storem")) {
        /* storem ptr, val  →  *ptr = val */
        int vp = reg_id(b), vs = reg_id(c);
        if (vp < 0 || vs < 0) die("line %d: storem regs", lineno);
        int rp = use_src(vp);
        int rs = use_src(vs);
        enc_store_mem(rp, rs);
        return;
    }
    if (!strcmp(a, "add") || !strcmp(a, "sub") || !strcmp(a, "mul")) {
        int vd = reg_id(b), vs = reg_id(c);
        if (vd < 0) die("line %d: binop dst", lineno);
        if (vs < 0 && c && (c[0]=='-' || (c[0]>='0'&&c[0]<='9'))) {
            int64_t imm = parse_imm(c);
            int rd = use_dst(vd);
            if (a[0]=='a') { if (imm==1) enc_inc(rd); else enc_addi(rd, imm); }
            else if (a[0]=='s') { enc_addi(rd, -imm); }
            else die("line %d: mul needs reg", lineno);
            commit_dst(vd, rd);
            return;
        }
        if (vs < 0) die("line %d: binop regs", lineno);
        int rs = use_src(vs);
        int rd = use_dst(vd);
        if (vd >= NPHYS && vmap[vd] == -1 && rs == 0) {
            enc_mov_rr(1, 0); rs = 1; rd = 0;
            enc_load_slot(0, spill_slot[vd]);
        }
        int op = a[0]=='a' ? 0 : a[0]=='s' ? 1 : 2;
        enc_binop(op, rd, rs);
        commit_dst(vd, rd);
        return;
    }
    if (!strcmp(a, "div")) {
        int rd = reg_id(b), rs = reg_id(c);
        if (rd != 0) die("line %d: div dst must be r0", lineno);
        if (rs < 0) die("line %d: div reg", lineno);
        enc_idiv(rs);
        return;
    }
    if (!strcmp(a, "and")) {
        int vd = reg_id(b), vs = reg_id(c);
        if (vd < 0 || vs < 0) die("line %d: and regs", lineno);
        int rs = use_src(vs);
        int rd = use_dst(vd);
        enc_and_rr(rd, rs);
        commit_dst(vd, rd);
        return;
    }
    if (!strcmp(a, "or")) {
        int vd = reg_id(b), vs = reg_id(c);
        if (vd < 0 || vs < 0) die("line %d: or regs", lineno);
        int rs = use_src(vs);
        int rd = use_dst(vd);
        enc_or_rr(rd, rs);
        commit_dst(vd, rd);
        return;
    }
    if (!strcmp(a, "xor")) {
        int vd = reg_id(b), vs = reg_id(c);
        if (vd < 0 || vs < 0) die("line %d: xor regs", lineno);
        int rs = use_src(vs);
        int rd = use_dst(vd);
        enc_xor_rr(rd, rs);
        commit_dst(vd, rd);
        return;
    }
    if (!strcmp(a, "andi")) {
        int vd = reg_id(b);
        if (vd < 0) die("line %d: andi reg", lineno);
        int rd = use_dst(vd);
        /* reload if needed */
        if (vd >= NPHYS && vmap[vd] == -1) enc_load_slot(rd, spill_slot[vd]);
        else if (vd >= NPHYS && vmap[vd] >= 0) { /* already in reg */ }
        enc_andi(rd, (int32_t)parse_imm(c));
        commit_dst(vd, rd);
        return;
    }
    if (!strcmp(a, "shli")) {
        int vd = reg_id(b);
        if (vd < 0) die("line %d: shli", lineno);
        int rd = use_dst(vd);
        if (vd >= NPHYS && vmap[vd] == -1) enc_load_slot(rd, spill_slot[vd]);
        enc_shli(rd, (int)parse_imm(c));
        commit_dst(vd, rd);
        return;
    }
    
        if (!strcmp(a, "mul3")) {
        int vd = reg_id(b);
        if (vd < 0) die("line %d: mul3", lineno);
        int rd = use_dst(vd);
        if (vd >= NPHYS && vmap[vd] == -1) enc_load_slot(rd, spill_slot[vd]);
        /* lea rd, [rd+rd*2] — scale=2 index=rd base=rd */
        if (rd == 0) {
            emit(0x48); emit(0x8d); emit(0x04); emit(0x40); /* lea rax,[rax+rax*2] */
        } else if (rd < 8) {
            /* lea r64, [r64+r64*2] with SIB: REX.W + 8D /r + SIB */
            uint8_t rex = 0x48 | reg_rex[rd] | (reg_rex[rd]<<2);
            emit(rex); emit(0x8d);
            emit((uint8_t)(0x04 | (reg_rm[rd]<<3))); /* mod=00, r/m=4 SIB */
            /* SIB: scale=2 (10), index=rd, base=rd */
            emit((uint8_t)(0x40 | (reg_rm[rd]<<3) | reg_rm[rd]));
        } else {
            enc_mov_ri(1, 3);
            enc_binop(2, rd, 1);
        }
        commit_dst(vd, rd);
        return;
    }
    if (!strcmp(a, "lea3a")) {
        /* lea rd, [rd+rd*2+imm8] — fused *3+add small imm (GCC-style mix) */
        int vd = reg_id(b);
        if (vd < 0) die("line %d: lea3a", lineno);
        int rd = use_dst(vd);
        int64_t imm = parse_imm(c);
        if (rd == 0 && imm >= -128 && imm <= 127) {
            emit(0x48); emit(0x8d); emit(0x44); emit(0x40); emit((uint8_t)(int8_t)imm);
            /* lea rax, [rax+rax*2+imm8] */
        } else if (rd == 0) {
            emit(0x48); emit(0x8d); emit(0x04); emit(0x40);
            enc_addi(0, imm);
        } else {
            if (rd == 0) { /* unreachable */ }
            enc_mov_ri(1, 3);
            enc_binop(2, rd, 1);
            enc_addi(rd, imm);
        }
        commit_dst(vd, rd);
        return;
    }

    if (!strcmp(a, "sari")) {
        int vd = reg_id(b);
        if (vd < 0) die("line %d: sari", lineno);
        int rd = use_dst(vd);
        if (vd >= NPHYS && vmap[vd] == -1) enc_load_slot(rd, spill_slot[vd]);
        enc_sari(rd, (int)parse_imm(c));
        commit_dst(vd, rd);
        return;
    }
    if (!strcmp(a, "shri")) {
        int vd = reg_id(b);
        if (vd < 0) die("line %d: shri", lineno);
        int rd = use_dst(vd);
        if (vd >= NPHYS && vmap[vd] == -1) enc_load_slot(rd, spill_slot[vd]);
        enc_shri(rd, (int)parse_imm(c));
        commit_dst(vd, rd);
        return;
    }
    if (!strcmp(a, "cmp")) {
        int va = reg_id(b), vb = reg_id(c);
        if (va < 0 || vb < 0) die("line %d: cmp regs", lineno);
        int ra = use_src(va);
        int rb = use_src(vb);
        if (ra == rb && va != vb) {
            /* both spilled to r0 — reload second into r1 */
            enc_load_slot(1, spill_slot[vb]);
            rb = 1;
        }
        enc_cmp_rr(ra, rb);
        return;
    }
    if (!strcmp(a, "cmpi")) {
        int va = reg_id(b);
        if (va < 0) die("line %d: cmpi reg", lineno);
        int ra = use_src(va);
        enc_cmp_ri(ra, parse_imm(c));
        return;
    }
    if (!strcmp(a, "je"))  { enc_jcc(0, b); return; }
    if (!strcmp(a, "jne")) { enc_jcc(1, b); return; }
    if (!strcmp(a, "jl"))  { enc_jcc(2, b); return; }
    if (!strcmp(a, "jle")) { enc_jcc(3, b); return; }
    if (!strcmp(a, "jg"))  { enc_jcc(4, b); return; }
    if (!strcmp(a, "jge")) { enc_jcc(5, b); return; }
    if (!strcmp(a, "jmp")) { enc_jmp(b); return; }
    if (!strcmp(a, "call")) {
        enc_call(runtime_map(b));
        return;
    }
    if (!strcmp(a, "arg")) {
        int ai = arg_id(b), vs = reg_id(c);
        if (ai < 0 || vs < 0) die("line %d: arg", lineno);
        int rs = use_src(vs);
        int pr = arg_to_reg(ai);
        if (pr != rs) enc_mov_rr(pr, rs);
        return;
    }
    if (!strcmp(a, "argi")) {
        int ai = arg_id(b);
        if (ai < 0) die("line %d: argi", lineno);
        enc_mov_ri(arg_to_reg(ai), parse_imm(c));
        return;
    }
    
    /* SIMD software: vadd4 rD, rS → rD += rS four times (unrolled) */
    if (!strcmp(a, "vadd4")) {
        int d = reg_id(b), sr = reg_id(c);
        int pd = use_dst(d), ps = use_src(sr);
        for (int k = 0; k < 4; k++) {
            enc_binop(0, pd, ps);
        }
        commit_dst(d, pd);
        return;
    }
    /* AVX2 vpaddq ymm0,ymm0,ymm1 when both "xmm/ymm" named — optional path */
    if (!strcmp(a, "vpaddq")) {
        /* encode: VEX.256.66.0F.W1 D4 /r — simplified: 4x scalar add on low regs */
        int d = reg_id(b), sr = reg_id(c);
        int pd = use_dst(d), ps = use_src(sr);
        enc_binop(0, pd, ps);
        commit_dst(d, pd);
        return;
    }
if (!strcmp(a, "print_int")) {
        int vs = reg_id(b);
        if (vs < 0) die("line %d: print_int", lineno);
        int rs = use_src(vs);
        if (rs != 4) enc_mov_rr(4, rs);
        enc_call("basalt_print_int");
        return;
    }
    if (!strcmp(a, "print_str")) {
        int rs = reg_id(b);
        if (rs < 0) die("line %d: print_str", lineno);
        if (rs != 4) enc_mov_rr(4, rs);
        enc_call("basalt_print_str");
        return;
    }
    if (!strcmp(a, "loadg")) {
        int rd = reg_id(b);
        if (rd < 0) die("line %d: loadg", lineno);
        enc_lea_str(rd, c);
        return;
    }
    if (!strcmp(a, "argload")) {
        int vd = reg_id(b);
        int ai = arg_id(c);
        if (vd < 0 || ai < 0) die("line %d: argload", lineno);
        int pr = arg_to_reg(ai);
        int rd = use_dst(vd);
        if (pr != rd) enc_mov_rr(rd, pr);
        commit_dst(vd, rd);
        return;
    }
    if (!strcmp(a, "neg")) {
        int rd = reg_id(b);
        if (rd < 0) die("line %d: neg", lineno);
        uint8_t rex = 0x48 | reg_rex[rd];
        emit(rex); emit(0xf7); emit(0xd8 | reg_rm[rd]);
        return;
    }
    die("line %d: unknown opcode '%s'", lineno, a);
}

/* ---- patch relocs ---- */
static void merge_rodata_into_code(void) {
    /* append rodata after code so lea is same-section PC-relative */
    int32_t base = code_len;
    /* align 4 */
    while (code_len & 3) emit(0);
    base = code_len;
    for (int i = 0; i < nlabels; i++) {
        if (!(labels[i].off & 0x40000000)) continue;
        int32_t roff = labels[i].off & 0x3fffffff;
        labels[i].off = code_len; /* now in code */
        /* copy string from rodata[roff..] until NUL — length unknown: scan */
        int32_t p = roff;
        while (p < rodata_len) {
            emit(rodata[p]);
            if (rodata[p] == 0) { p++; break; }
            p++;
        }
    }
    /* clear separate rodata */
    rodata_len = 0;
}

static void patch_relocs(void) {
    merge_rodata_into_code();
    for (int i = 0; i < nrelocs; i++) {
        Reloc *r = &relocs[i];
        Label *L = NULL;
        for (int j = 0; j < nlabels; j++)
            if (!strcmp(labels[j].name, r->name)) { L = &labels[j]; break; }
        if (!L || !L->defined) {
            continue;
        }
        int32_t target = L->off;
        int32_t at = r->at;
        int32_t rel = target - (at + 4);
        code[at]   = (uint8_t)rel;
        code[at+1] = (uint8_t)(rel>>8);
        code[at+2] = (uint8_t)(rel>>16);
        code[at+3] = (uint8_t)(rel>>24);
    }
}

/* ---- minimal ELF64 ET_EXEC, links against runtime via dynamic — 
 * For self-contained demo we produce ET_REL-like blob then use system gcc to
 * final-link with runtime.o OR produce static with unresolved as relative 0.
 *
 * Practical path: write .o (ELF relocatable) with text+rodata+symbols+relas,
 * user links: ld/gcc with basalt_rt.o.
 * Also support -e to emit "linked" by invoking as external — here we write .o
 * and optionally a fully linked ELF if all symbols defined.
 */

static void w16(FILE *f, uint16_t v) { fputc(v,f); fputc(v>>8,f); }
static void w32(FILE *f, uint32_t v) {
    fputc(v,f); fputc(v>>8,f); fputc(v>>16,f); fputc(v>>24,f);
}
static void w64(FILE *f, uint64_t v) { w32(f,(uint32_t)v); w32(f,(uint32_t)(v>>32)); }

/* Write ELF64 relocatable object */
static int write_elf_obj(const char *path) {
    /* Layout:
     *  ELF header
     *  .text
     *  .rodata
     *  .shstrtab
     *  .strtab
     *  .symtab
     *  .rela.text
     *  section headers
     */
    patch_relocs();

    /* Build string tables and symbols */
    char shstr[256]; int shstr_len = 0;
    #define ADDSH(s) do { int _l=strlen(s)+1; memcpy(shstr+shstr_len,s,_l); shstr_len+=_l; } while(0)
    int off_null, off_text, off_rodata, off_shstr, off_str, off_sym, off_rela;
    off_null = 0; shstr[0]=0; shstr_len=1;
    off_text = shstr_len; ADDSH(".text");
    off_rodata = shstr_len; ADDSH(".rodata");
    off_shstr = shstr_len; ADDSH(".shstrtab");
    off_str = shstr_len; ADDSH(".strtab");
    off_sym = shstr_len; ADDSH(".symtab");
    off_rela = shstr_len; ADDSH(".rela.text");
    #undef ADDSH

    char strtab[65536]; int str_len = 1; strtab[0]=0;
    #define ADDSYM(s) ({\
        int _o=str_len; int _l=strlen(s)+1;\
        if (str_len+_l < (int)sizeof(strtab)) { memcpy(strtab+str_len,s,_l); str_len+=_l; }\
        _o; })

    typedef struct { uint32_t name; uint8_t info; uint8_t other; uint16_t shndx; uint64_t value; uint64_t size; } Sym;
    Sym syms[4096]; int nsym = 0;
    memset(syms, 0, sizeof(syms[0])); nsym = 1; /* undef */

    /* FILE symbol */
    /* text section symbol */
    /* each .func → GLOBAL FUNC */
    for (int i = 0; i < nfuncs; i++) {
        Sym *s = &syms[nsym++];
        s->name = ADDSYM(funcs[i].name);
        s->info = (1<<4) | 2; /* GLOBAL FUNC */
        s->shndx = 1; /* .text */
        s->value = (uint64_t)(uint32_t)funcs[i].addr;
        s->size = 0;
    }
    /* externs referenced */
    int *reloc_sym = calloc(nrelocs, sizeof(int));
    for (int i = 0; i < nrelocs; i++) {
        Reloc *r = &relocs[i];
        int found = 0;
        for (int j = 1; j < nsym; j++) {
            /* compare by looking up name in strtab — simplified: check funcs */
            for (int k = 0; k < nfuncs; k++)
                if (!strcmp(funcs[k].name, r->name)) {
                    reloc_sym[i] = 1 + k; /* after null: funcs start at 1 */
                    found = 1; break;
                }
            if (found) break;
        }
        /* labels local */
        if (!found) {
            for (int j = 0; j < nlabels; j++) {
                if (!strcmp(labels[j].name, r->name) && labels[j].defined) {
                    /* local section-relative — already patched for same section */
                    reloc_sym[i] = -1;
                    found = 1;
                    break;
                }
            }
        }
        if (!found) {
            /* undefined external */
            Sym *s = &syms[nsym++];
            s->name = ADDSYM(r->name);
            s->info = (1<<4) | 2;
            s->shndx = 0;
            s->value = 0;
            reloc_sym[i] = nsym - 1;
        }
    }

    /* Re-patch same-section jumps with correct relative */
    for (int i = 0; i < nrelocs; i++) {
        if (reloc_sym[i] != -1) continue;
        Reloc *r = &relocs[i];
        Label *L = NULL;
        for (int j = 0; j < nlabels; j++)
            if (!strcmp(labels[j].name, r->name)) { L = &labels[j]; break; }
        if (!L) continue;
        int32_t target = L->off;
        if (target & 0x40000000) continue;
        int32_t rel = target - (r->at + 4);
        code[r->at]   = (uint8_t)rel;
        code[r->at+1] = (uint8_t)(rel>>8);
        code[r->at+2] = (uint8_t)(rel>>16);
        code[r->at+3] = (uint8_t)(rel>>24);
    }

    /* Build rela entries for externals and rodata */
    typedef struct { uint64_t off; uint64_t info; int64_t addend; } Rela;
    Rela *rela = calloc(nrelocs + 8, sizeof(Rela));
    int nrela = 0;
    for (int i = 0; i < nrelocs; i++) {
        if (reloc_sym[i] == -1) continue;
        Reloc *r = &relocs[i];
        Label *L = NULL;
        for (int j = 0; j < nlabels; j++)
            if (!strcmp(labels[j].name, r->name)) { L = &labels[j]; break; }
        if (L && (L->off & 0x40000000)) {
            /* rodata ref: R_X86_64_PC32 against .rodata symbol — add synthetic */
            /* For simplicity emit R_X86_64_PC32 with section symbol — skip, embed absolute later */
            continue;
        }
        Rela *e = &rela[nrela++];
        e->off = (uint64_t)(uint32_t)r->at;
        /* R_X86_64_PLT32 = 4, PC32 = 2 */
        e->info = ((uint64_t)(uint32_t)reloc_sym[i] << 32) | 4;
        e->addend = -4;
    }

    /* Offsets in file */
    size_t ehsize = 64;
    size_t text_off = 256; /* aligned */
    size_t ro_off = text_off + ((code_len + 15) & ~15);
    size_t shstr_off = ro_off + ((rodata_len + 15) & ~15);
    size_t str_off = shstr_off + ((shstr_len + 15) & ~15);
    size_t sym_off = str_off + ((str_len + 15) & ~15);
    size_t rela_off = sym_off + nsym * 24;
    rela_off = (rela_off + 7) & ~7;
    size_t sh_off = rela_off + nrela * 24;
    sh_off = (sh_off + 7) & ~7;

    FILE *f = fopen(path, "wb");
    if (!f) die("cannot write %s", path);

    /* ELF header */
    uint8_t ehdr[64] = {0};
    ehdr[0]=0x7f; ehdr[1]='E'; ehdr[2]='L'; ehdr[3]='F';
    ehdr[4]=2; ehdr[5]=1; ehdr[6]=1;
    ehdr[16]=1; ehdr[17]=0; /* ET_REL */
    ehdr[18]=0x3e; ehdr[19]=0; /* EM_X86_64 */
    ehdr[20]=1; /* version */
    /* shoff */
    uint64_t shoff = sh_off;
    memcpy(ehdr+40, &shoff, 8);
    ehdr[54]=64; ehdr[55]=0; /* ehsize */
    ehdr[58]=64; ehdr[59]=0; /* shentsize */
    uint16_t shnum = 7;
    memcpy(ehdr+60, &shnum, 2);
    uint16_t shstrndx = 3;
    memcpy(ehdr+62, &shstrndx, 2);
    fwrite(ehdr, 1, 64, f);
    /* pad to text_off */
    for (size_t i = 64; i < text_off; i++) fputc(0, f);
    fwrite(code, 1, code_len, f);
    for (size_t i = code_len; i < ((code_len+15)&~15); i++) fputc(0, f);
    fwrite(rodata, 1, rodata_len, f);
    for (size_t i = rodata_len; i < ((rodata_len+15)&~15); i++) fputc(0, f);
    fwrite(shstr, 1, shstr_len, f);
    for (size_t i = shstr_len; i < ((shstr_len+15)&~15); i++) fputc(0, f);
    fwrite(strtab, 1, str_len, f);
    for (size_t i = str_len; i < ((str_len+15)&~15); i++) fputc(0, f);
    for (int i = 0; i < nsym; i++) {
        w32(f, syms[i].name);
        fputc(syms[i].info, f);
        fputc(syms[i].other, f);
        w16(f, syms[i].shndx);
        w64(f, syms[i].value);
        w64(f, syms[i].size);
    }
    /* pad to rela_off */
    {
        long cur = ftell(f);
        while (cur < (long)rela_off) { fputc(0, f); cur++; }
    }
    for (int i = 0; i < nrela; i++) {
        w64(f, rela[i].off);
        w64(f, rela[i].info);
        w64(f, (uint64_t)rela[i].addend);
    }
    {
        long cur = ftell(f);
        while (cur < (long)sh_off) { fputc(0, f); cur++; }
    }

    /* Section headers: 0 null, 1 .text, 2 .rodata, 3 .shstrtab, 4 .strtab, 5 .symtab, 6 .rela.text */
    /* null */
    for (int i = 0; i < 64; i++) fputc(0, f);
    /* .text PROGBITS AX */
    w32(f, off_text); w32(f, 1); w64(f, 6); /* SHF_ALLOC|EXEC */
    w64(f, 0); w64(f, text_off); w64(f, code_len);
    w32(f, 0); w32(f, 0); w64(f, 16); w64(f, 0);
    /* .rodata */
    w32(f, off_rodata); w32(f, 1); w64(f, 2); /* ALLOC */
    w64(f, 0); w64(f, ro_off); w64(f, rodata_len);
    w32(f, 0); w32(f, 0); w64(f, 4); w64(f, 0);
    /* .shstrtab */
    w32(f, off_shstr); w32(f, 3); w64(f, 0);
    w64(f, 0); w64(f, shstr_off); w64(f, shstr_len);
    w32(f, 0); w32(f, 0); w64(f, 1); w64(f, 0);
    /* .strtab */
    w32(f, off_str); w32(f, 3); w64(f, 0);
    w64(f, 0); w64(f, str_off); w64(f, str_len);
    w32(f, 0); w32(f, 0); w64(f, 1); w64(f, 0);
    /* .symtab */
    w32(f, off_sym); w32(f, 2); w64(f, 0);
    w64(f, 0); w64(f, sym_off); w64(f, nsym * 24);
    w32(f, 4); w32(f, 1); w64(f, 8); w64(f, 24);
    /* .rela.text */
    w32(f, off_rela); w32(f, 4); w64(f, 0);
    w64(f, 0); w64(f, rela_off); w64(f, nrela * 24);
    w32(f, 5); w32(f, 1); w64(f, 8); w64(f, 24);

    fclose(f);
    free(rela);
    free(reloc_sym);
    return 0;
}

/* Also write raw .bin + companion GAS for debug */
static int write_gas(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "/* generated from BASM — debug dump */\n.text\n");
    for (int i = 0; i < nfuncs; i++)
        fprintf(f, ".globl %s\n%s:\n", funcs[i].name, funcs[i].name);
    fprintf(f, "/* %d bytes code, %d rodata */\n", code_len, rodata_len);
    fclose(f);
    return 0;
}


static void usage(const char *prog) {
    fprintf(stderr,
        "basm — Basalt Assembly (Stage2: multi-file, fast link)\n"
        "usage: %s [opts] <file.basm|file.o>...\n"
        "  -c            object only (.o)\n"
        "  -e            link executable\n"
        "  -o FILE       output path\n"
        "  -r PATH       runtime .o (default: /tmp/basalt_rt.o)\n"
        "  -L DIR        add search dir for .o\n"
        "  -j N          reserved (parallel jobs)\n"
        "  -v            verbose\n"
        "  -h            help\n"
        "Link is a single gcc -no-pie invocation over all objects (fast path).\n",
        prog);
}

static char *objs[256];
static int nobjs;
static char *basms[64];
static int nbasms;
static const char *runtime_o = "/tmp/basalt_rt.o";
static int do_bare = 0;
static int link_gpu = 0;
static const char *runtime_bare_o = "/tmp/basalt_rt_bare.o";
static char *libdirs[32];
static int nlibdirs;

static int ends_with(const char *s, const char *suf) {
    size_t n = strlen(s), m = strlen(suf);
    return n >= m && !strcmp(s + n - m, suf);
}

static int ensure_runtime(void) {
    if (access(runtime_o, R_OK) == 0) return 0;
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "gcc -O2 -c -o '%s' /home/workdir/artifacts/basaltc/runtime/runtime_all.c 2>/dev/null",
        runtime_o);
    return system(cmd);
}

/* Fast multi-object link: one process, all .o + runtime */
/* Prefer mold > lld > gold > ld. gcc only as last-resort driver. */
static const char *pick_linker(void) {
    static const char *cands[] = {
        "mold", "ld.mold", "ld.lld", "lld", "ld.gold", "gold", "ld", NULL
    };
    for (int i = 0; cands[i]; i++) {
        char chk[160];
        snprintf(chk, sizeof(chk), "command -v %s >/dev/null 2>&1", cands[i]);
        if (system(chk) == 0) return cands[i];
    }
    return "ld";
}


static int link_bare(const char *exe) {
    /* Freestanding + Multiboot2 bootloader + bare runtime (no libc) */
    const char *boot = "/tmp/bootloader.o";
    const char *rt = "/tmp/basalt_rt_bare.o";
    if (access(boot, R_OK) != 0) {
        system("as --64 -o /tmp/bootloader.o /home/workdir/artifacts/basaltc/runtime/bootloader.s 2>/dev/null");
        system("as --64 -o /tmp/bootloader.o runtime/bootloader.s 2>/dev/null");
        system("as --64 -o /tmp/bootloader.o bin/../runtime/bootloader.s 2>/dev/null");
    }
    if (access(rt, R_OK) != 0) {
        system("as --64 -o /tmp/basalt_rt_bare.o /home/workdir/artifacts/basaltc/runtime/runtime_bare.s 2>/dev/null");
        system("as --64 -o /tmp/basalt_rt_bare.o runtime/runtime_bare.s 2>/dev/null");
    }
    /* linker script: place .multiboot early */
    const char *lds = "/tmp/basalt_bare.ld";
    {
        FILE *lf = fopen(lds, "w");
        if (lf) {
            fputs(
                "ENTRY(_start)\n"
                "SECTIONS {\n"
                "  . = 1M;\n"
                "  .boot : { KEEP(*(.multiboot)) }\n"
                "  .text : { *(.text .text.*) }\n"
                "  .rodata : { *(.rodata .rodata.*) }\n"
                "  .data : { *(.data .data.*) }\n"
                "  .bss : { *(.bss .bss.*) *(COMMON) }\n"
                "}\n", lf);
            fclose(lf);
        }
    }
    size_t cap = 8192 + (size_t)nobjs * 320;
    char *cmd = malloc(cap);
    if (!cmd) die("OOM");
    size_t pos = 0;
    pos += (size_t)snprintf(cmd + pos, cap - pos,
        "ld -nostdlib -static -o '%s' -T '%s'", exe, lds);
    /* bootloader first */
    if (access(boot, R_OK) == 0)
        pos += (size_t)snprintf(cmd + pos, cap - pos, " '%s'", boot);
    for (int i = 0; i < nobjs; i++)
        pos += (size_t)snprintf(cmd + pos, cap - pos, " '%s'", objs[i]);
    if (access(rt, R_OK) == 0)
        pos += (size_t)snprintf(cmd + pos, cap - pos, " '%s'", rt);
    if (verbose) fprintf(stderr, "basm: bare+boot: %s\n", cmd);
    int rc = system(cmd);
    free(cmd);
    return rc;
}

static int link_fast(const char *exe) {
    if (do_bare) return link_bare(exe);
    if (ensure_runtime() != 0)
        fprintf(stderr, "basm: warning: runtime .o missing\n");
    const char *ld = pick_linker();
    size_t cap = 8192 + (size_t)nobjs * 320;
    char *cmd = malloc(cap);
    if (!cmd) die("OOM");
    size_t pos = 0;
    pos += (size_t)snprintf(cmd + pos, cap - pos, "%s -o '%s' -m elf_x86_64", ld, exe);
    for (int i = 0; i < nobjs; i++)
        pos += (size_t)snprintf(cmd + pos, cap - pos, " '%s'", objs[i]);
    if (access(runtime_o, R_OK) == 0)
        pos += (size_t)snprintf(cmd + pos, cap - pos, " '%s'", runtime_o);
    /* GPU runtime only if -gpu / -fgpu or BASALT_LINK_GPU=1 */
    if (link_gpu || (getenv("BASALT_LINK_GPU") && getenv("BASALT_LINK_GPU")[0]=='1')) {
        const char *gpu_paths[] = {
            "/tmp/basalt_gpu.o", "bin/basalt_gpu.o",
            "/tmp/basalt/bin/basalt_gpu.o", NULL
        };
        for (int gi = 0; gpu_paths[gi]; gi++) {
            if (access(gpu_paths[gi], R_OK) == 0) {
                pos += (size_t)snprintf(cmd + pos, cap - pos, " '%s'", gpu_paths[gi]);
                break;
            }
        }
    }
    pos += (size_t)snprintf(cmd + pos, cap - pos,
        " -lc -z noexecstack --gc-sections --as-needed -z now 2>/dev/null");
    if (verbose) fprintf(stderr, "basm: blink-link (%s)\n", ld);
    int rc = system(cmd);
    if (rc != 0) {
        /* gcc as linker driver only */
        pos = 0;
        pos += (size_t)snprintf(cmd + pos, cap - pos, "gcc -no-pie -o '%s'", exe);
        for (int i = 0; i < nobjs; i++)
            pos += (size_t)snprintf(cmd + pos, cap - pos, " '%s'", objs[i]);
        if (access(runtime_o, R_OK) == 0)
            pos += (size_t)snprintf(cmd + pos, cap - pos, " '%s'", runtime_o);
        pos += (size_t)snprintf(cmd + pos, cap - pos,
            " -lc -Wl,-z,noexecstack -Wl,--gc-sections");
        if (verbose) fprintf(stderr, "basm: link fallback gcc\n");
        rc = system(cmd);
    }
    free(cmd);
    return rc;
}

static int assemble_file(const char *path, const char *objpath) {
    /* reset assembler state for each translation unit */
    code_len = 0; rodata_len = 0; nlabels = 0; nfuncs = 0; nslots = 0;
    cur_frame = 256;
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "basm: cannot open %s\n", path); return 1; }
    char line[MAX_LINE];
    int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        size_t n = strlen(line);
        while (n && (line[n-1]=='\n'||line[n-1]=='\r')) line[--n]=0;
        assemble_line(line, lineno);
    }
    fclose(f);
    if (verbose)
        fprintf(stderr, "basm: %s -> %d code bytes\n", path, code_len);
    return write_elf_obj(objpath);
}

int main(int argc, char **argv) {
    const char *out = NULL;
    int do_link = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i+1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "-r") && i+1 < argc) runtime_o = argv[++i];
        else if (!strcmp(argv[i], "-L") && i+1 < argc) {
            if (nlibdirs < 32) libdirs[nlibdirs++] = argv[++i];
        }
        else if (!strcmp(argv[i], "-c")) do_link = 0;
        else if (!strcmp(argv[i], "-e")) do_link = 1;
        else if (!strcmp(argv[i], "-bare")) do_bare = 1;
        else if (!strcmp(argv[i], "-gpu") || !strcmp(argv[i], "-fgpu")) link_gpu = 1;
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "-j") && i+1 < argc) { (void)argv[++i]; /* reserved */ }
        else if (!strcmp(argv[i], "-h")) { usage(argv[0]); return 0; }
        else if (!strcmp(argv[i], "-O0") || !strcmp(argv[i], "-O1") ||
                 !strcmp(argv[i], "-O2") || !strcmp(argv[i], "-O3") ||
                 !strcmp(argv[i], "-blink-O0") || !strcmp(argv[i], "-blink-O1") ||
                 !strcmp(argv[i], "-blink-O2") || !strcmp(argv[i], "-blink-O3")) {
            /* consumed later by bopt level scan */
        }
        else if (argv[i][0] != '-') {
            if (ends_with(argv[i], ".o")) {
                if (nobjs < 256) objs[nobjs++] = argv[i];
            } else if (ends_with(argv[i], ".basm") || ends_with(argv[i], ".s")) {
                if (nbasms < 64) basms[nbasms++] = argv[i];
            } else {
                /* treat as basm source */
                if (nbasms < 64) basms[nbasms++] = argv[i];
            }
        }
        else die("unknown option %s", argv[i]);
    }
    if (nbasms == 0 && nobjs == 0) { usage(argv[0]); return 1; }
    if (!out) out = (do_link == 0) ? "a.o" : "a.out";

    int want_exe = do_link;
    if (want_exe < 0) {
        size_t ol = strlen(out);
        want_exe = !(ol >= 2 && out[ol-2]=='.' && out[ol-1]=='o');
    }

    /* Assemble each .basm to temp/named .o */
    for (int i = 0; i < nbasms; i++) {
        char objpath[1024];
        if (!want_exe && nbasms == 1 && nobjs == 0)
            snprintf(objpath, sizeof(objpath), "%s", out);
        else
            snprintf(objpath, sizeof(objpath), "%s.o", basms[i]);
        {
            /* Blink BPO: optimize BASM before encode (level from -O if present, default 2) */
            char optpath[512];
            snprintf(optpath, sizeof(optpath), "%s.opt.basm", basms[i]);
            int blvl = 0;
            for (int j = 1; j < argc; j++) {
                if (!strcmp(argv[j], "-O0") || !strcmp(argv[j], "-blink-O0")) blvl = 0;
                if (!strcmp(argv[j], "-O1") || !strcmp(argv[j], "-blink-O1")) blvl = 1;
                if (!strcmp(argv[j], "-O2") || !strcmp(argv[j], "-blink-O2")) blvl = 2;
                if (!strcmp(argv[j], "-O3") || !strcmp(argv[j], "-blink-O3")) blvl = 3;
            }
            if (bopt_file(basms[i], optpath, blvl) != 0) {
                fprintf(stderr, "basm: bopt failed\n");
                return 1;
            }
            if (assemble_file(optpath, objpath) != 0) return 1;
        }
        fprintf(stderr, "basm: wrote %s\n", objpath);
        if (nobjs < 256) objs[nobjs++] = strdup(objpath);
    }

    if (!want_exe) {
        /* -c: done after assemble */
        return 0;
    }
    if (link_fast(out) != 0) die("link failed");
    fprintf(stderr, "basm: linked %s\n", out);
    return 0;
}
