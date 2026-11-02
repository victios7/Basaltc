#include "basalt.h"
#include "codegen.h"
#include <stdio.h>
#include <stdarg.h>

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
    int    label_id;
    Options *opt;

    /* string pool */
    char **str_lits;
    int   *str_labs;
    int    nstrs;
} Asm;

typedef struct {
    char *name;
    int   offset;
    Type *type;
} Local;

typedef struct {
    Local *locals;
    int    nlocals;
    int    frame_size;
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

static int new_label(Asm *a) {
    return a->label_id++;
}

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

static int local_offset(const char *name) {
    for (int i = 0; i < ctx->nlocals; i++)
        if (!strcmp(ctx->locals[i].name, name))
            return ctx->locals[i].offset;
    return 0;
}

static void add_local(const char *name, Type *ty) {
    int sz = type_size(ty);
    if (sz < 8) sz = 8;
    ctx->frame_size += sz;
    if (ctx->frame_size % 8) ctx->frame_size += 8 - (ctx->frame_size % 8);
    Local l = { xstrdup(name), -ctx->frame_size, ty };
    ctx->locals = xrealloc(ctx->locals, (ctx->nlocals + 1) * sizeof(Local));
    ctx->locals[ctx->nlocals++] = l;
}

static void gen_expr(Asm *a, Node *n);
static void gen_stmt(Asm *a, Node *n);

static void gen_expr(Asm *a, Node *n) {
    if (!n) return;
    switch (n->kind) {
        case NODE_INT_LIT:
            asm_printf(a, "    mov $%lld, %%rax\n", (long long)n->i64);
            break;
        case NODE_FLOAT_LIT:
            asm_printf(a, "    mov $0, %%rax\n");
            break;
        case NODE_STRING_LIT: {
            int lab = add_string(a, n->str ? n->str : "");
            asm_printf(a, "    leaq .Lstr%d(%%rip), %%rax\n", lab);
            break;
        }
        case NODE_IDENT: {
            int off = local_offset(n->ident.name);
            if (off)
                asm_printf(a, "    mov %d(%%rbp), %%rax\n", off);
            else
                asm_printf(a, "    mov $0, %%rax\n");
            break;
        }
        case NODE_BINARY: {
            gen_expr(a, n->bin.lhs);
            asm_printf(a, "    push %%rax\n");
            gen_expr(a, n->bin.rhs);
            asm_printf(a, "    mov %%rax, %%rcx\n");
            asm_printf(a, "    pop %%rax\n");
            switch (n->bin.op) {
                case TOK_PLUS:  asm_printf(a, "    add %%rcx, %%rax\n"); break;
                case TOK_MINUS: asm_printf(a, "    sub %%rcx, %%rax\n"); break;
                case TOK_STAR:  asm_printf(a, "    imul %%rcx, %%rax\n"); break;
                case TOK_SLASH:
                    asm_printf(a, "    cqo\n    idiv %%rcx\n"); break;
                case TOK_PERCENT:
                    asm_printf(a, "    cqo\n    idiv %%rcx\n    mov %%rdx, %%rax\n"); break;
                case TOK_EQ:
                    asm_printf(a, "    cmp %%rcx, %%rax\n    sete %%al\n    movzb %%al, %%rax\n"); break;
                case TOK_NEQ:
                    asm_printf(a, "    cmp %%rcx, %%rax\n    setne %%al\n    movzb %%al, %%rax\n"); break;
                case TOK_LT:
                    asm_printf(a, "    cmp %%rcx, %%rax\n    setl %%al\n    movzb %%al, %%rax\n"); break;
                case TOK_GT:
                    asm_printf(a, "    cmp %%rcx, %%rax\n    setg %%al\n    movzb %%al, %%rax\n"); break;
                case TOK_LE:
                    asm_printf(a, "    cmp %%rcx, %%rax\n    setle %%al\n    movzb %%al, %%rax\n"); break;
                case TOK_GE:
                    asm_printf(a, "    cmp %%rcx, %%rax\n    setge %%al\n    movzb %%al, %%rax\n"); break;
                case TOK_AND: asm_printf(a, "    and %%rcx, %%rax\n"); break;
                case TOK_OR:  asm_printf(a, "    or %%rcx, %%rax\n"); break;
                default: break;
            }
            break;
        }
        case NODE_UNARY:
            gen_expr(a, n->bin.rhs);
            if (n->bin.op == TOK_MINUS) asm_printf(a, "    neg %%rax\n");
            else if (n->bin.op == TOK_NOT)
                asm_printf(a, "    test %%rax, %%rax\n    sete %%al\n    movzb %%al, %%rax\n");
            break;
        case NODE_CALL: {
            const char *argregs[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
            int nargs = n->call.nargs;
            if (nargs > 6) nargs = 6;
            for (int i = nargs - 1; i >= 0; i--) {
                gen_expr(a, n->call.args[i]);
                asm_printf(a, "    push %%rax\n");
            }
            for (int i = 0; i < nargs; i++)
                asm_printf(a, "    pop %s\n", argregs[i]);

            if (n->call.callee->kind == NODE_IDENT) {
                const char *name = n->call.callee->ident.name;
                if (!strcmp(name, "print_int"))
                    asm_printf(a, "    call basalt_print_int\n");
                else if (!strcmp(name, "print_str") || !strcmp(name, "PrintC") || !strcmp(name, "print"))
                    asm_printf(a, "    call basalt_print_str\n");
                else if (!strcmp(name, "print_nl"))
                    asm_printf(a, "    call basalt_print_nl\n");
                else if (!strcmp(name, "panic"))
                    asm_printf(a, "    call basalt_panic\n");
                else if (!strcmp(name, "read_file"))
                    asm_printf(a, "    call basalt_read_file\n");
                else if (!strcmp(name, "write_file"))
                    asm_printf(a, "    call basalt_write_file\n");
                else if (!strcmp(name, "strlen"))
                    asm_printf(a, "    call basalt_strlen\n");
                else if (!strcmp(name, "file_size"))
                    asm_printf(a, "    call basalt_file_size\n");
                else if (!strcmp(name, "char_at"))
                    asm_printf(a, "    call basalt_char_at\n");
                else if (!strcmp(name, "alloc"))
                    asm_printf(a, "    call basalt_alloc\n");
                else if (!strcmp(name, "strcmp"))
                    asm_printf(a, "    call basalt_strcmp\n");
                else if (!strcmp(name, "substr"))
                    asm_printf(a, "    call basalt_substr\n");
                else if (!strcmp(name, "str_concat"))
                    asm_printf(a, "    call basalt_str_concat\n");
                else if (!strcmp(name, "itoa"))
                    asm_printf(a, "    call basalt_itoa\n");
                else if (!strcmp(name, "exit"))
                    asm_printf(a, "    call basalt_exit\n");
                else if (!strcmp(name, "argc"))
                    asm_printf(a, "    call basalt_argc\n");
                else if (!strcmp(name, "argv"))
                    asm_printf(a, "    call basalt_argv\n");
                else if (!strcmp(name, "link_pe"))
                    asm_printf(a, "    call basalt_link_pe\n");
                else if (!strcmp(name, "pool_create"))
                    asm_printf(a, "    call basalt_pool_create\n");
                else if (!strcmp(name, "pool_free"))
                    asm_printf(a, "    call basalt_pool_free\n");
                else if (!strcmp(name, "pool_alloc"))
                    asm_printf(a, "    call basalt_pool_alloc\n");
                else if (!strcmp(name, "write_pe"))
                    asm_printf(a, "    call basalt_write_pe\n");
                else if (!strcmp(name, "system"))
                    asm_printf(a, "    call basalt_system\n");
                else
                    asm_printf(a, "    call %s\n", name);
            }
            break;
        }
        default:
            asm_printf(a, "    mov $0, %%rax\n");
            break;
    }
}

static void gen_stmt(Asm *a, Node *n) {
    if (!n) return;
    switch (n->kind) {
        case NODE_BOX_DECL:
            if (n->box.init) {
                gen_expr(a, n->box.init);
                int off = local_offset(n->box.name);
                asm_printf(a, "    mov %%rax, %d(%%rbp)\n", off);
            }
            break;
        case NODE_ASSIGN:
            gen_expr(a, n->assign.rhs);
            if (n->assign.lhs->kind == NODE_IDENT) {
                int off = local_offset(n->assign.lhs->ident.name);
                asm_printf(a, "    mov %%rax, %d(%%rbp)\n", off);
            }
            break;
        case NODE_EXPR_STMT:
            gen_expr(a, n->expr);
            break;
        case NODE_RETURN:
            if (n->expr) gen_expr(a, n->expr);
            else asm_printf(a, "    mov $0, %%rax\n");
            asm_printf(a, "    leave\n    ret\n");
            break;
        case NODE_IF: {
            int l_else = new_label(a);
            int l_end  = new_label(a);
            gen_expr(a, n->ctrl.cond);
            asm_printf(a, "    test %%rax, %%rax\n    jz .L%d\n", l_else);
            gen_stmt(a, n->ctrl.then_body);
            asm_printf(a, "    jmp .L%d\n.L%d:\n", l_end, l_else);
            if (n->ctrl.else_body) gen_stmt(a, n->ctrl.else_body);
            asm_printf(a, ".L%d:\n", l_end);
            break;
        }
        case NODE_WHILE: {
            int l_cond = new_label(a);
            int l_end  = new_label(a);
            asm_printf(a, ".L%d:\n", l_cond);
            gen_expr(a, n->ctrl.cond);
            asm_printf(a, "    test %%rax, %%rax\n    jz .L%d\n", l_end);
            gen_stmt(a, n->ctrl.then_body);
            asm_printf(a, "    jmp .L%d\n.L%d:\n", l_cond, l_end);
            break;
        }
        case NODE_BLOCK:
        case NODE_SAFE_BLOCK:
            for (int i = 0; i < n->block.nstmts; i++)
                gen_stmt(a, n->block.stmts[i]);
            break;
        case NODE_FREE:
            gen_expr(a, n->expr);
            asm_printf(a, "    mov %%rax, %%rdi\n    call basalt_free\n");
            break;
        case NODE_PANIC:
            if (n->expr) { gen_expr(a, n->expr); asm_printf(a, "    mov %%rax, %%rdi\n"); }
            else asm_printf(a, "    xor %%rdi, %%rdi\n");
            asm_printf(a, "    call basalt_panic\n");
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
    ctx = &fc;

    char *asm_name = f->func.name;
    if (!strcmp(asm_name, "main"))
        asm_printf(a, ".globl main\n");
    asm_printf(a, "%s:\n", asm_name);
    asm_printf(a, "    push %%rbp\n    mov %%rsp, %%rbp\n");
    if (!strcmp(asm_name, "main")) {
        /* System V: rdi=argc, rsi=argv on entry before our push... 
           actually after push rbp, args are still in rdi/rsi */
        asm_printf(a, "    /* save argc/argv */\n");
        asm_printf(a, "    mov %%rdi, %%r12\n");  /* argc */
        asm_printf(a, "    mov %%rsi, %%r13\n");  /* argv */
        asm_printf(a, "    mov %%r12, %%rdi\n");
        asm_printf(a, "    mov %%r13, %%rsi\n");
        asm_printf(a, "    call basalt_set_args\n");
    }

    for (int i = 0; i < f->func.nparams; i++)
        add_local(f->func.param_names[i], f->func.param_types[i]);
    /* collect all box decls recursively */
    collect_locals(f->func.body);

    int frame = ctx->frame_size;
    if (frame % 16) frame += 16 - (frame % 16);
    if (frame > 0) asm_printf(a, "    sub $%d, %%rsp\n", frame);

    const char *argregs[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
    for (int i = 0; i < f->func.nparams && i < 6; i++) {
        int off = local_offset(f->func.param_names[i]);
        asm_printf(a, "    mov %s, %d(%%rbp)\n", argregs[i], off);
    }

    gen_stmt(a, f->func.body);

    asm_printf(a, "    mov $0, %%rax\n    leave\n    ret\n\n");
    ctx = NULL;
}

char *codegen(Node *prog, Options *opt) {
    Asm a;
    asm_init(&a, opt);

    asm_printf(&a, "    .text\n");

    for (int i = 0; i < prog->block.nstmts; i++) {
        Node *n = prog->block.stmts[i];
        if (n->kind == NODE_FUNC && (n->func.is_cpu || !n->func.is_gpu))
            gen_function(&a, n);
    }

    /* emit string literals */
    if (a.nstrs > 0) {
        asm_printf(&a, "    .section .rodata\n");
        for (int i = 0; i < a.nstrs; i++) {
            asm_printf(&a, ".Lstr%d:\n    .string \"", a.str_labs[i]);
            /* escape */
            for (const char *p = a.str_lits[i]; *p; p++) {
                if (*p == '"' || *p == '\\') asm_printf(&a, "\\%c", *p);
                else if (*p == '\n') asm_printf(&a, "\\n");
                else if (*p == '\t') asm_printf(&a, "\\t");
                else asm_printf(&a, "%c", *p);
            }
            asm_printf(&a, "\"\n");
        }
    }

    return a.buf;
}

int emit_and_link(Node *prog, Options *opt, const char *input_path) {
    char *asm_src = codegen(prog, opt);

    char asm_path[512], out_path[512];
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

    snprintf(asm_path, sizeof(asm_path), "/tmp/%s.s", name);

    FILE *f = fopen(asm_path, "w");
    if (!f) die("cannot write %s", asm_path);
    fputs(asm_src, f);
    fclose(f);
    free(asm_src);

    if (opt->verbose) fprintf(stderr, "  wrote %s\n", asm_path);

    if (opt->emit_asm) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", asm_path, out_path);
        system(cmd);
        return 0;
    }

    system("gcc -c -o /tmp/basalt_rt.o /home/workdir/artifacts/basaltc/runtime/runtime_all.c 2>/dev/null");

    char cmd[2048];
    if (opt->emit_obj)
        snprintf(cmd, sizeof(cmd), "gcc -c -o '%s' '%s'", out_path, asm_path);
    else
        snprintf(cmd, sizeof(cmd), "gcc -no-pie -o '%s' '%s' /tmp/basalt_rt.o -lc", out_path, asm_path);

    if (opt->verbose) fprintf(stderr, "  %s\n", cmd);
    int rc = system(cmd);
    if (rc != 0) die("assemble/link failed (rc=%d)", rc);
    if (opt->verbose) fprintf(stderr, "  produced %s\n", out_path);
    if (opt->target == TARGET_PE64 && !opt->emit_obj && !opt->emit_asm) {
        char pe_path[600];
        snprintf(pe_path, sizeof(pe_path), "%s.exe", out_path);
        char pe_cmd[2500];
        snprintf(pe_cmd, sizeof(pe_cmd),
            "gcc -c -o '%s.pe.o' '%s' 2>/dev/null && "
            "objcopy -O binary -j .text '%s.pe.o' '%s.pe.bin' 2>/dev/null && "
            "/tmp/pewrap '%s.pe.bin' '%s' && "
            "rm -f '%s.pe.o' '%s.pe.bin'",
            out_path, asm_path, out_path, out_path, out_path, pe_path, out_path, out_path);
        if (system(pe_cmd) != 0)
            fprintf(stderr, "basaltc: PE write failed\n");
        else if (opt->verbose)
            fprintf(stderr, "  PE written %s\n", pe_path);
    }
    return 0;
}

