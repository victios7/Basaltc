/*
 * Basalt Stage 0 codegen — BASM backend + basm link
 */
#include "basalt.h"
#include "codegen.h"
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
    int    label_id;
    Options *opt;
    char **str_lits;
    int   *str_labs;
    int    nstrs;
} Asm;

typedef struct {
    char *name;
    int   slot;
    Type *type;
} Local;

typedef struct {
    Local *locals;
    int    nlocals;
    int    nslots;
    int    expr_sp;   /* expression temp stack depth */
} FuncCtx;

static FuncCtx *ctx;

static void asm_init(Asm *a, Options *opt) {
    memset(a, 0, sizeof(*a));
    a->cap = 8192;
    a->buf = xmalloc(a->cap);
    a->buf[0] = 0;
    a->opt = opt;
}

static void asm_printf(Asm *a, const char *fmt, ...) {
    char tmp[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    while (a->len + (size_t)n + 1 > a->cap) {
        a->cap *= 2;
        a->buf = xrealloc(a->buf, a->cap);
    }
    memcpy(a->buf + a->len, tmp, (size_t)n);
    a->len += (size_t)n;
    a->buf[a->len] = 0;
}

static int new_label(Asm *a) { return a->label_id++; }

static int add_string(Asm *a, const char *s) {
    for (int i = 0; i < a->nstrs; i++)
        if (!strcmp(a->str_lits[i], s)) return a->str_labs[i];
    int lab = new_label(a);
    a->str_lits = xrealloc(a->str_lits, (a->nstrs + 1) * sizeof(char *));
    a->str_labs = xrealloc(a->str_labs, (a->nstrs + 1) * sizeof(int));
    a->str_lits[a->nstrs] = xstrdup(s);
    a->str_labs[a->nstrs] = lab;
    a->nstrs++;
    return lab;
}

static int local_slot(const char *name) {
    for (int i = 0; i < ctx->nlocals; i++)
        if (!strcmp(ctx->locals[i].name, name))
            return ctx->locals[i].slot;
    return -1;
}

static void add_local(const char *name, Type *ty) {
    Local l = { xstrdup(name), ctx->nslots++, ty };
    ctx->locals = xrealloc(ctx->locals, (ctx->nlocals + 1) * sizeof(Local));
    ctx->locals[ctx->nlocals++] = l;
}

static const char *rt_name(const char *name) {
    static char buf[128];
    static const char *rt[] = {
        "print_int","print_str","print_nl","panic","strlen","char_at",
        "substr","str_concat","strcmp","itoa","read_file","write_file",
        "alloc","free","argc","argv","file_size","exit","link_pe","write_pe",
        "set_args","pool_create","pool_free","pool_alloc", NULL
    };
    for (int i = 0; rt[i]; i++)
        if (!strcmp(name, rt[i])) {
            snprintf(buf, sizeof(buf), "basalt_%s", name);
            return buf;
        }
    return name;
}

static void gen_expr(Asm *a, Node *n);
static void gen_stmt(Asm *a, Node *n);

static void gen_expr(Asm *a, Node *n) {
    if (!n) return;
    switch (n->kind) {
        case NODE_INT_LIT:
            asm_printf(a, "    li      r0, %lld\n", (long long)n->i64);
            break;
        case NODE_FLOAT_LIT:
            asm_printf(a, "    li      r0, 0\n");
            break;
        case NODE_STRING_LIT: {
            int lab = add_string(a, n->str ? n->str : "");
            asm_printf(a, "    loadg   r0, S%d\n", lab);
            break;
        }
        case NODE_IDENT: {
            int s = local_slot(n->ident.name);
            if (s >= 0)
                asm_printf(a, "    load    r0, s%d\n", s);
            else
                asm_printf(a, "    li      r0, 0\n");
            break;
        }
        case NODE_BINARY: {
            /* Spill LHS to stack — r2 is clobbered by nested gen_expr */
            gen_expr(a, n->bin.lhs);
            {
                int ts = 2000 + ctx->expr_sp++;
                if (ts + 1 > ctx->nslots) ctx->nslots = ts + 1;
                asm_printf(a, "    store   s%d, r0\n", ts);
                gen_expr(a, n->bin.rhs);
                asm_printf(a, "    mov     r1, r0\n");
                asm_printf(a, "    load    r0, s%d\n", ts);
                ctx->expr_sp--;
            }
            switch (n->bin.op) {
                case TOK_PLUS:  asm_printf(a, "    add     r0, r1\n"); break;
                case TOK_MINUS: asm_printf(a, "    sub     r0, r1\n"); break;
                case TOK_STAR:  asm_printf(a, "    mul     r0, r1\n"); break;
                case TOK_SLASH: asm_printf(a, "    div     r0, r1\n"); break;
                case TOK_PERCENT: asm_printf(a, "    div     r0, r1\n"); break;
                case TOK_EQ: {
                    int l = new_label(a);
                    asm_printf(a, "    cmp     r0, r1\n    li      r0, 0\n    jne     .L%d\n    li      r0, 1\n.L%d:\n", l, l);
                    break;
                }
                case TOK_NEQ: {
                    int l = new_label(a);
                    asm_printf(a, "    cmp     r0, r1\n    li      r0, 0\n    je      .L%d\n    li      r0, 1\n.L%d:\n", l, l);
                    break;
                }
                case TOK_LT: {
                    int l = new_label(a);
                    asm_printf(a, "    cmp     r0, r1\n    li      r0, 0\n    jge     .L%d\n    li      r0, 1\n.L%d:\n", l, l);
                    break;
                }
                case TOK_GT: {
                    int l = new_label(a);
                    asm_printf(a, "    cmp     r0, r1\n    li      r0, 0\n    jle     .L%d\n    li      r0, 1\n.L%d:\n", l, l);
                    break;
                }
                case TOK_LE: {
                    int l = new_label(a);
                    asm_printf(a, "    cmp     r0, r1\n    li      r0, 0\n    jg      .L%d\n    li      r0, 1\n.L%d:\n", l, l);
                    break;
                }
                case TOK_GE: {
                    int l = new_label(a);
                    asm_printf(a, "    cmp     r0, r1\n    li      r0, 0\n    jl      .L%d\n    li      r0, 1\n.L%d:\n", l, l);
                    break;
                }
                default: break;
            }
            break;
        }
        case NODE_UNARY:
            gen_expr(a, n->bin.lhs);
            if (n->bin.op == TOK_MINUS)
                asm_printf(a, "    neg     r0\n");
            break;
        case NODE_CALL: {
            int nargs = n->call.nargs;
            if (nargs > 6) nargs = 6;
            for (int i = 0; i < nargs; i++) {
                gen_expr(a, n->call.args[i]);
                asm_printf(a, "    arg     a%d, r0\n", i);
            }
            const char *name = "?";
            if (n->call.callee && n->call.callee->kind == NODE_IDENT)
                name = n->call.callee->ident.name;
            if (!strcmp(name, "print_int"))
                asm_printf(a, "    print_int r0\n");
            else if (!strcmp(name, "print_str"))
                asm_printf(a, "    print_str r0\n");
            else
                asm_printf(a, "    call    %s\n", rt_name(name));
            break;
        }
        default:
            asm_printf(a, "    li      r0, 0\n");
            break;
    }
}

static void gen_stmt(Asm *a, Node *n) {
    if (!n) return;
    switch (n->kind) {
        case NODE_BOX_DECL: {
            int s = local_slot(n->box.name);
            if (n->box.init) {
                gen_expr(a, n->box.init);
                if (s >= 0) asm_printf(a, "    store   s%d, r0\n", s);
            } else if (s >= 0) {
                asm_printf(a, "    li      r0, 0\n    store   s%d, r0\n", s);
            }
            break;
        }
        case NODE_ASSIGN: {
            gen_expr(a, n->assign.rhs);
            if (n->assign.lhs && n->assign.lhs->kind == NODE_IDENT) {
                int s = local_slot(n->assign.lhs->ident.name);
                if (s >= 0) asm_printf(a, "    store   s%d, r0\n", s);
            }
            break;
        }
        case NODE_RETURN:
            if (n->expr) {
                gen_expr(a, n->expr);
                asm_printf(a, "    retv    r0\n");
            } else asm_printf(a, "    reti    0\n");
            break;
        case NODE_EXPR_STMT:
            gen_expr(a, n->expr);
            break;
        case NODE_IF: {
            int l_else = new_label(a);
            int l_end  = new_label(a);
            gen_expr(a, n->ctrl.cond);
            asm_printf(a, "    cmpi    r0, 0\n    je      .L%d\n", l_else);
            gen_stmt(a, n->ctrl.then_body);
            asm_printf(a, "    jmp     .L%d\n.L%d:\n", l_end, l_else);
            if (n->ctrl.else_body) gen_stmt(a, n->ctrl.else_body);
            asm_printf(a, ".L%d:\n", l_end);
            break;
        }
        case NODE_WHILE: {
            int l_cond = new_label(a);
            int l_end  = new_label(a);
            asm_printf(a, ".L%d:\n", l_cond);
            gen_expr(a, n->ctrl.cond);
            asm_printf(a, "    cmpi    r0, 0\n    je      .L%d\n", l_end);
            gen_stmt(a, n->ctrl.then_body);
            asm_printf(a, "    jmp     .L%d\n.L%d:\n", l_cond, l_end);
            break;
        }
        case NODE_BLOCK:
        case NODE_SAFE_BLOCK:
            for (int i = 0; i < n->block.nstmts; i++)
                gen_stmt(a, n->block.stmts[i]);
            break;
        case NODE_FREE:
            gen_expr(a, n->expr);
            asm_printf(a, "    arg     a0, r0\n    call    basalt_free\n");
            break;
        case NODE_PANIC:
            if (n->expr) {
                gen_expr(a, n->expr);
                asm_printf(a, "    arg     a0, r0\n");
            } else asm_printf(a, "    argi    a0, 0\n");
            asm_printf(a, "    call    basalt_panic\n");
            break;
        default: break;
    }
}

static void collect_locals(Node *n) {
    if (!n) return;
    switch (n->kind) {
        case NODE_BOX_DECL:
            add_local(n->box.name, n->box.decl_type);
            break;
        case NODE_BLOCK:
        case NODE_SAFE_BLOCK:
            for (int i = 0; i < n->block.nstmts; i++)
                collect_locals(n->block.stmts[i]);
            break;
        case NODE_IF:
            collect_locals(n->ctrl.then_body);
            if (n->ctrl.else_body) collect_locals(n->ctrl.else_body);
            break;
        case NODE_WHILE:
            collect_locals(n->ctrl.then_body);
            break;
        default: break;
    }
}

static void gen_function(Asm *a, Node *f) {
    FuncCtx fc = {0};
    fc.expr_sp = 0;
    ctx = &fc;
    for (int i = 0; i < f->func.nparams; i++)
        add_local(f->func.param_names[i], f->func.param_types[i]);
    collect_locals(f->func.body);
    /* Reserve s900..s931 for expression temps (nested binary ops) */
    int max_slot = fc.nslots;
    if (max_slot < 932) max_slot = 932;
    int frame = (max_slot + 8) * 8;
    if (frame < 4096) frame = 4096;
    if (frame % 16) frame += 16 - (frame % 16);
    asm_printf(a, ".func %s\n.frame %d\n", f->func.name, frame);
    for (int i = 0; i < fc.nslots; i++)
        asm_printf(a, ".slot s%d\n", i);
    for (int i = 900; i < 932; i++)
        asm_printf(a, ".slot s%d\n", i);
    asm_printf(a, "    enter\n");
    for (int i = 0; i < f->func.nparams && i < 6; i++) {
        asm_printf(a, "    argload r0, a%d\n", i);
        asm_printf(a, "    store   s%d, r0\n", i);
    }
    if (!strcmp(f->func.name, "main"))
        asm_printf(a, "    call    basalt_set_args\n");
    if (f->func.body)
        gen_stmt(a, f->func.body);
    asm_printf(a, "    reti    0\n.end\n\n");
    ctx = NULL;
}

char *codegen(Node *prog, Options *opt) {
    Asm a;
    asm_init(&a, opt);
    asm_printf(&a, "// generated by basaltc — BASM backend\n");
    for (int i = 0; i < prog->block.nstmts; i++) {
        Node *n = prog->block.stmts[i];
        if (n->kind == NODE_FUNC && (n->func.is_cpu || !n->func.is_gpu))
            gen_function(&a, n);
    }
    for (int i = 0; i < a.nstrs; i++) {
        asm_printf(&a, ".str S%d \"", a.str_labs[i]);
        for (const char *p = a.str_lits[i]; *p; p++) {
            if (*p == '"' || *p == '\\') asm_printf(&a, "\\%c", *p);
            else if (*p == '\n') asm_printf(&a, "\\n");
            else if (*p == '\t') asm_printf(&a, "\\t");
            else asm_printf(&a, "%c", *p);
        }
        asm_printf(&a, "\"\n");
    }
    return a.buf;
}

int emit_and_link(Node *prog, Options *opt, const char *input_path) {
    char *asm_src = codegen(prog, opt);
    char basm_path[512], out_path[512];
    const char *base = strrchr(input_path, '/');
    base = base ? base + 1 : input_path;
    char name[256];
    strncpy(name, base, sizeof(name)-1);
    name[sizeof(name)-1] = 0;
    char *dot = strrchr(name, '.');
    if (dot) *dot = 0;
    if (opt->output)
        snprintf(out_path, sizeof(out_path), "%s", opt->output);
    else
        snprintf(out_path, sizeof(out_path), "%s", name);
    snprintf(basm_path, sizeof(basm_path), "/tmp/%s.basm", name);

    FILE *f = fopen(basm_path, "w");
    if (!f) die("cannot write %s", basm_path);
    fputs(asm_src, f);
    fclose(f);
    free(asm_src);
    if (opt->verbose) fprintf(stderr, "  wrote %s (BASM)\n", basm_path);

    if (opt->emit_asm) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", basm_path, out_path);
        system(cmd);
        return 0;
    }

    if (access("basm", X_OK) != 0)
        system("gcc -O2 -o basm basm/basm.c 2>/dev/null");

    char cmd[2048];
    if (opt->emit_obj)
        snprintf(cmd, sizeof(cmd), "basm '%s' -c -o '%s'%s",
                 basm_path, out_path, opt->verbose ? " -v" : "");
    else
        snprintf(cmd, sizeof(cmd), "basm '%s' -e -o '%s'%s",
                 basm_path, out_path, opt->verbose ? " -v" : "");
    if (opt->verbose) fprintf(stderr, "  %s\n", cmd);
    int rc = system(cmd);
    if (rc != 0) die("basm/link failed (rc=%d)", rc);
    if (opt->verbose) fprintf(stderr, "  produced %s\n", out_path);
    return 0;
}
