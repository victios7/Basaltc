/* Stage0 C backend — stable bootstrap via gcc */
#include "basalt.h"
#include "codegen.h"
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

typedef struct {
    char  *buf;
    size_t len, cap;
    int    tmp;
    Options *opt;
} COut;

typedef struct {
    char *name;
    Type *type;
} Local;

typedef struct {
    Local *locals;
    int nlocals;
} FCtx;

static FCtx *fctx;

static void co_init(COut *o, Options *opt) {
    memset(o, 0, sizeof(*o));
    o->cap = 1 << 16;
    o->buf = xmalloc(o->cap);
    o->buf[0] = 0;
    o->opt = opt;
}

static void co_printf(COut *o, const char *fmt, ...) {
    char tmp[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    while (o->len + (size_t)n + 1 > o->cap) {
        o->cap *= 2;
        o->buf = xrealloc(o->buf, o->cap);
    }
    memcpy(o->buf + o->len, tmp, (size_t)n);
    o->len += (size_t)n;
    o->buf[o->len] = 0;
}

static int new_tmp(COut *o) { return o->tmp++; }

static const char *cname(const char *name) {
    if (!name) return "unnamed";
    /* C keywords / reserved that Basalt uses as idents */
    static const char *kw[] = {
        "asm","auto","break","case","char","const","continue","default","do",
        "double","else","enum","extern","float","for","goto","if","inline",
        "int","long","register","restrict","return","short","signed","sizeof",
        "static","struct","switch","typedef","union","unsigned","void","volatile",
        "while","_Bool","_Complex","main", NULL
    };
    /* note: we still emit main as function name specially */
    for (int i = 0; kw[i]; i++) {
        if (!strcmp(name, kw[i]) && strcmp(name, "main") != 0) {
            static char buf[128];
            snprintf(buf, sizeof(buf), "bd_%s", name);
            return buf;
        }
    }
    return name;
}

static void add_local(const char *name, Type *ty) {
    fctx->locals = xrealloc(fctx->locals, (fctx->nlocals + 1) * sizeof(Local));
    fctx->locals[fctx->nlocals].name = xstrdup(name);
    fctx->locals[fctx->nlocals].type = ty;
    fctx->nlocals++;
}

static int is_local(const char *name) {
    for (int i = 0; i < fctx->nlocals; i++)
        if (!strcmp(fctx->locals[i].name, name)) return 1;
    return 0;
}

static const char *ctype(Type *t) {
    if (!t) return "int64_t";
    if (t->kind == TY_STRING || (t->name && !strcmp(t->name, "string"))) return "char*";
    if (t->kind == TY_PTR) return "void*";
    if (t->kind == TY_VOID) return "void";
    if (t->kind == TY_FLOAT) return t->bits <= 32 ? "float" : "double";
    return "int64_t";
}

static void emit_expr(COut *o, Node *n);
static void emit_stmt(COut *o, Node *n);

static void emit_expr(COut *o, Node *n) {
    if (!n) { co_printf(o, "0"); return; }
    switch (n->kind) {
    case NODE_INT_LIT:
        co_printf(o, "%lldLL", (long long)n->i64);
        break;
    case NODE_FLOAT_LIT:
        co_printf(o, "%g", n->f64);
        break;
    case NODE_STRING_LIT: {
        co_printf(o, "\"");
        for (const char *p = n->str ? n->str : ""; *p; p++) {
            if (*p == '"' || *p == '\\') co_printf(o, "\\%c", *p);
            else if (*p == '\n') co_printf(o, "\\n");
            else if (*p == '\t') co_printf(o, "\\t");
            else if ((unsigned char)*p < 32) co_printf(o, "\\x%02x", (unsigned char)*p);
            else co_printf(o, "%c", *p);
        }
        co_printf(o, "\"");
        break;
    }
    case NODE_IDENT:
        co_printf(o, "%s", cname(n->ident.name));
        break;
    case NODE_BINARY: {
        const char *op = "+";
        switch (n->bin.op) {
        case TOK_PLUS: op = "+"; break;
        case TOK_MINUS: op = "-"; break;
        case TOK_STAR: op = "*"; break;
        case TOK_SLASH: op = "/"; break;
        case TOK_PERCENT: op = "%"; break;
        case TOK_EQ: op = "=="; break;
        case TOK_NEQ: op = "!="; break;
        case TOK_LT: op = "<"; break;
        case TOK_GT: op = ">"; break;
        case TOK_LE: op = "<="; break;
        case TOK_GE: op = ">="; break;
        case TOK_AND: op = "&&"; break;
        case TOK_OR: op = "||"; break;
        default: break;
        }
        co_printf(o, "(");
        emit_expr(o, n->bin.lhs);
        co_printf(o, " %s ", op);
        emit_expr(o, n->bin.rhs);
        co_printf(o, ")");
        break;
    }
    case NODE_UNARY:
        if (n->bin.op == TOK_MINUS) {
            co_printf(o, "(-(");
            emit_expr(o, n->bin.lhs);
            co_printf(o, "))");
        } else if (n->bin.op == TOK_NOT) {
            co_printf(o, "(!(");
            emit_expr(o, n->bin.lhs);
            co_printf(o, "))");
        } else {
            emit_expr(o, n->bin.lhs);
        }
        break;
    case NODE_CALL: {
        const char *name = n->call.callee && n->call.callee->kind == NODE_IDENT
            ? n->call.callee->ident.name : "unknown";
        /* map builtins */
        if (!strcmp(name, "print_int")) {
            co_printf(o, "basalt_print_int((int64_t)(");
            if (n->call.nargs > 0) emit_expr(o, n->call.args[0]);
            else co_printf(o, "0");
            co_printf(o, "))");
        } else if (!strcmp(name, "print_str")) {
            co_printf(o, "basalt_print_str((const char*)(");
            if (n->call.nargs > 0) emit_expr(o, n->call.args[0]);
            else co_printf(o, "\"\"");
            co_printf(o, "))");
        } else if (!strcmp(name, "print_nl")) {
            co_printf(o, "basalt_print_nl()");
        } else if (!strcmp(name, "strlen")) {
            co_printf(o, "basalt_strlen((const char*)(");
            if (n->call.nargs > 0) emit_expr(o, n->call.args[0]);
            co_printf(o, "))");
        } else if (!strcmp(name, "char_at")) {
            co_printf(o, "basalt_char_at((const char*)(");
            if (n->call.nargs > 0) emit_expr(o, n->call.args[0]);
            co_printf(o, "), (int64_t)(");
            if (n->call.nargs > 1) emit_expr(o, n->call.args[1]);
            co_printf(o, "))");
        } else if (!strcmp(name, "substr")) {
            co_printf(o, "basalt_substr((const char*)(");
            if (n->call.nargs > 0) emit_expr(o, n->call.args[0]);
            co_printf(o, "), (int64_t)(");
            if (n->call.nargs > 1) emit_expr(o, n->call.args[1]);
            co_printf(o, "), (int64_t)(");
            if (n->call.nargs > 2) emit_expr(o, n->call.args[2]);
            co_printf(o, "))");
        } else if (!strcmp(name, "str_concat")) {
            co_printf(o, "basalt_str_concat((const char*)(");
            if (n->call.nargs > 0) emit_expr(o, n->call.args[0]);
            else co_printf(o, "\"\"");
            co_printf(o, "), (const char*)(");
            if (n->call.nargs > 1) emit_expr(o, n->call.args[1]);
            else co_printf(o, "\"\"");
            co_printf(o, "))");
        } else if (!strcmp(name, "strcmp")) {
            co_printf(o, "basalt_strcmp((const char*)(");
            if (n->call.nargs > 0) emit_expr(o, n->call.args[0]);
            co_printf(o, "), (const char*)(");
            if (n->call.nargs > 1) emit_expr(o, n->call.args[1]);
            co_printf(o, "))");
        } else if (!strcmp(name, "itoa")) {
            co_printf(o, "basalt_itoa((int64_t)(");
            if (n->call.nargs > 0) emit_expr(o, n->call.args[0]);
            co_printf(o, "))");
        } else if (!strcmp(name, "read_file")) {
            co_printf(o, "basalt_read_file((const char*)(");
            if (n->call.nargs > 0) emit_expr(o, n->call.args[0]);
            co_printf(o, "))");
        } else if (!strcmp(name, "write_file")) {
            co_printf(o, "basalt_write_file((const char*)(");
            if (n->call.nargs > 0) emit_expr(o, n->call.args[0]);
            co_printf(o, "), (const char*)(");
            if (n->call.nargs > 1) emit_expr(o, n->call.args[1]);
            else co_printf(o, "\"\"");
            co_printf(o, "))");
        } else if (!strcmp(name, "alloc")) {
            co_printf(o, "basalt_alloc((int64_t)(");
            if (n->call.nargs > 0) emit_expr(o, n->call.args[0]);
            co_printf(o, "))");
        } else if (!strcmp(name, "free")) {
            co_printf(o, "basalt_free((void*)(");
            if (n->call.nargs > 0) emit_expr(o, n->call.args[0]);
            co_printf(o, "))");
        } else if (!strcmp(name, "panic")) {
            co_printf(o, "basalt_panic((const char*)(");
            if (n->call.nargs > 0) emit_expr(o, n->call.args[0]);
            else co_printf(o, "\"panic\"");
            co_printf(o, "))");
        } else if (!strcmp(name, "argc")) {
            co_printf(o, "basalt_argc()");
        } else if (!strcmp(name, "argv")) {
            co_printf(o, "basalt_argv((int64_t)(");
            if (n->call.nargs > 0) emit_expr(o, n->call.args[0]);
            co_printf(o, "))");
        } else if (!strcmp(name, "getenv")) {
            co_printf(o, "basalt_getenv((const char*)(");
            if (n->call.nargs > 0) emit_expr(o, n->call.args[0]);
            else co_printf(o, "\"\"");
            co_printf(o, "))");
        } else if (!strcmp(name, "str_gc")) {
            co_printf(o, "basalt_str_gc()");
        } else if (!strcmp(name, "str_clear_roots")) {
            co_printf(o, "basalt_str_clear_roots()");
        } else if (!strcmp(name, "str_add_root")) {
            co_printf(o, "basalt_str_add_root((const char*)(");
            if (n->call.nargs > 0) emit_expr(o, n->call.args[0]);
            else co_printf(o, "\"\"");
            co_printf(o, "))");
        } else if (!strcmp(name, "str_owned_count")) {
            co_printf(o, "basalt_str_owned_count()");
        } else if (!strcmp(name, "str_phase_begin")) {
            co_printf(o, "basalt_str_phase_begin()");
        } else if (!strcmp(name, "str_phase_end")) {
            co_printf(o, "basalt_str_phase_end()");
        } else if (!strcmp(name, "str_persist")) {
            co_printf(o, "basalt_str_persist((const char*)(");
            if (n->call.nargs > 0) emit_expr(o, n->call.args[0]);
            else co_printf(o, "\"\"");
            co_printf(o, "))");
        } else if (!strcmp(name, "system")) {
            co_printf(o, "basalt_system((const char*)(");
            if (n->call.nargs > 0) emit_expr(o, n->call.args[0]);
            co_printf(o, "))");
        } else if (!strcmp(name, "link_pe")) {
            co_printf(o, "0"); /* stub */
        } else {
            co_printf(o, "%s(", name);
            for (int i = 0; i < n->call.nargs; i++) {
                if (i) co_printf(o, ", ");
                emit_expr(o, n->call.args[i]);
            }
            co_printf(o, ")");
        }
        break;
    }
    default:
        co_printf(o, "0");
        break;
    }
}

static void collect_locals(Node *n) {
    if (!n) return;
    switch (n->kind) {
    case NODE_BOX_DECL:
        add_local(n->box.name, n->box.decl_type ? n->box.decl_type : n->type);
        if (n->box.init) collect_locals(n->box.init);
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
    case NODE_FOR:
        collect_locals(n->ctrl.then_body);
        break;
    default:
        break;
    }
}

static void emit_stmt(COut *o, Node *n) {
    if (!n) return;
    switch (n->kind) {
    case NODE_BLOCK:
    case NODE_SAFE_BLOCK:
        for (int i = 0; i < n->block.nstmts; i++)
            emit_stmt(o, n->block.stmts[i]);
        break;
    case NODE_BOX_DECL: {
        const char *ty = "int64_t";
        if (n->box.decl_type) ty = ctype(n->box.decl_type);
        else if (n->type) ty = ctype(n->type);
        /* heuristic: name patterns */
        co_printf(o, "    %s %s = ", ty, cname(n->box.name));
        if (n->box.init)
            emit_expr(o, n->box.init);
        else
            co_printf(o, "0");
        co_printf(o, ";\n");
        break;
    }
    case NODE_ASSIGN:
        co_printf(o, "    ");
        emit_expr(o, n->assign.lhs);
        co_printf(o, " = ");
        emit_expr(o, n->assign.rhs);
        co_printf(o, ";\n");
        break;
    case NODE_EXPR_STMT:
        co_printf(o, "    ");
        emit_expr(o, n->expr);
        co_printf(o, ";\n");
        break;
    case NODE_RETURN:
        co_printf(o, "    return ");
        if (n->expr) emit_expr(o, n->expr);
        else co_printf(o, "0");
        co_printf(o, ";\n");
        break;
    case NODE_IF:
        co_printf(o, "    if (");
        emit_expr(o, n->ctrl.cond);
        co_printf(o, ") {\n");
        emit_stmt(o, n->ctrl.then_body);
        co_printf(o, "    }\n");
        if (n->ctrl.else_body) {
            co_printf(o, "    else {\n");
            emit_stmt(o, n->ctrl.else_body);
            co_printf(o, "    }\n");
        }
        break;
    case NODE_WHILE:
        co_printf(o, "    while (");
        emit_expr(o, n->ctrl.cond);
        co_printf(o, ") {\n");
        emit_stmt(o, n->ctrl.then_body);
        co_printf(o, "    }\n");
        break;
    case NODE_FOR:
        /* desugar-ish: init; while (cond) { body; step } */
        if (n->ctrl.init) emit_stmt(o, n->ctrl.init);
        co_printf(o, "    while (");
        if (n->ctrl.cond) emit_expr(o, n->ctrl.cond);
        else co_printf(o, "1");
        co_printf(o, ") {\n");
        emit_stmt(o, n->ctrl.then_body);
        if (n->ctrl.step) emit_stmt(o, n->ctrl.step);
        co_printf(o, "    }\n");
        break;
    case NODE_FREE:
        co_printf(o, "    basalt_free((void*)(");
        if (n->expr) emit_expr(o, n->expr);
        co_printf(o, "));\n");
        break;
    case NODE_PANIC:
        co_printf(o, "    basalt_panic(");
        if (n->expr) emit_expr(o, n->expr);
        else co_printf(o, "\"panic\"");
        co_printf(o, ");\n");
        break;
    default:
        break;
    }
}

static void emit_func(COut *o, Node *f) {
    FCtx fc = {0};
    fctx = &fc;
    if (f->func.body) collect_locals(f->func.body);

    const char *ret = "int64_t";
    if (f->func.ret_type) ret = ctype(f->func.ret_type);
    if (!strcmp(f->func.name, "main")) ret = "int";

    co_printf(o, "%s %s(", ret, f->func.name);
    for (int i = 0; i < f->func.nparams; i++) {
        if (i) co_printf(o, ", ");
        const char *pt = "int64_t";
        if (f->func.param_types && f->func.param_types[i])
            pt = ctype(f->func.param_types[i]);
        /* string params common */
        if (f->func.param_names[i]) {
            /* heuristic from self-host: name src, path, s often string */
            const char *pn = f->func.param_names[i];
            if (!strcmp(pn, "src") || !strcmp(pn, "s") || !strcmp(pn, "path") ||
                !strcmp(pn, "name") || !strcmp(pn, "msg") || !strcmp(pn, "asm") ||
                !strcmp(pn, "bir") || !strcmp(pn, "a") || !strcmp(pn, "b") ||
                !strcmp(pn, "kw") || !strcmp(pn, "cmd") || !strcmp(pn, "out") ||
                !strcmp(pn, "d") || !strcmp(pn, "id") || !strcmp(pn, "line_s"))
                pt = "char*";
        }
        if (f->func.param_types && f->func.param_types[i] &&
            f->func.param_types[i]->kind == TY_STRING)
            pt = "char*";
        co_printf(o, "%s %s", pt, cname(f->func.param_names[i]));
        add_local(f->func.param_names[i], f->func.param_types ? f->func.param_types[i] : NULL);
    }
    if (f->func.nparams == 0) co_printf(o, "void");
    co_printf(o, ") {\n");

    /* declare locals that aren't params — box decls emit with init */
    if (f->func.body)
        emit_stmt(o, f->func.body);

    if (!strcmp(f->func.name, "main"))
        co_printf(o, "    return 0;\n");
    else
        co_printf(o, "    return 0;\n");
    co_printf(o, "}\n\n");
    fctx = NULL;
    free(fc.locals);
}

static void emit_forward(COut *o, Node *prog) {
    for (int i = 0; i < prog->block.nstmts; i++) {
        Node *n = prog->block.stmts[i];
        if (n->kind != NODE_FUNC) continue;
        if (n->func.is_gpu) continue;
        const char *ret = "int64_t";
        if (n->func.ret_type) ret = ctype(n->func.ret_type);
        if (!strcmp(n->func.name, "main")) ret = "int";
        co_printf(o, "%s %s(", ret, n->func.name);
        for (int j = 0; j < n->func.nparams; j++) {
            if (j) co_printf(o, ", ");
            const char *pt = "int64_t";
            if (n->func.param_types && n->func.param_types[j])
                pt = ctype(n->func.param_types[j]);
            if (n->func.param_types && n->func.param_types[j] &&
                n->func.param_types[j]->kind == TY_STRING)
                pt = "char*";
            const char *pn = n->func.param_names[j];
            if (pn && (!strcmp(pn, "src") || !strcmp(pn, "s") || !strcmp(pn, "path") ||
                !strcmp(pn, "name") || !strcmp(pn, "msg") || !strcmp(pn, "asm") ||
                !strcmp(pn, "bir") || !strcmp(pn, "a") || !strcmp(pn, "b") ||
                !strcmp(pn, "kw") || !strcmp(pn, "cmd") || !strcmp(pn, "out") ||
                !strcmp(pn, "d") || !strcmp(pn, "id")))
                pt = "char*";
            co_printf(o, "%s", pt);
        }
        if (n->func.nparams == 0) co_printf(o, "void");
        co_printf(o, ");\n");
    }
    co_printf(o, "\n");
}

char *codegen_c(Node *prog, Options *opt) {
    COut o;
    co_init(&o, opt);
    co_printf(&o,
        "/* generated by basaltc Stage0 C backend */\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <stdint.h>\n"
        "#include <unistd.h>\n"
        "extern void basalt_print_int(int64_t);\n"
        "extern void basalt_print_str(const char*);\n"
        "extern void basalt_print_nl(void);\n"
        "extern void basalt_panic(const char*);\n"
        "extern void *basalt_alloc(int64_t);\n"
        "extern void basalt_free(void*);\n"
        "extern char *basalt_read_file(const char*);\n"
        "extern int64_t basalt_write_file(const char*, const char*);\n"
        "extern int64_t basalt_strlen(const char*);\n"
        "extern int64_t basalt_char_at(const char*, int64_t);\n"
        "extern int64_t basalt_strcmp(const char*, const char*);\n"
        "extern char *basalt_substr(const char*, int64_t, int64_t);\n"
        "extern char *basalt_str_concat(const char*, const char*);\n"
        "extern char *basalt_itoa(int64_t);\n"
        "extern void basalt_set_args(int, char**);\n"
        "extern int64_t basalt_argc(void);\n"
        "extern char *basalt_argv(int64_t);\n"
        "extern int64_t basalt_system(const char*);\n"
        "extern char *basalt_getenv(const char*);\n"
        "extern void basalt_str_phase_begin(void);\n"
        "extern void basalt_str_phase_end(void);\n"
        "extern char *basalt_str_persist(const char*);\n"
        "extern void basalt_str_gc(void);\n"
        "extern void basalt_str_clear_roots(void);\n"
        "extern void basalt_str_add_root(const char*);\n"
        "extern int64_t basalt_str_owned_count(void);\n"
        "extern void basalt_flush(void);\n"
        "extern void basalt_exit(int64_t);\n"
        "extern void *basalt_allocator(int64_t);\n"
        "extern void basalt_store_i64(void*, int64_t, int64_t);\n"
        "extern int64_t basalt_load_i64(void*, int64_t);\n"
        "\n");
    emit_forward(&o, prog);
    for (int i = 0; i < prog->block.nstmts; i++) {
        Node *n = prog->block.stmts[i];
        if (n->kind == NODE_FUNC && !n->func.is_gpu)
            emit_func(&o, n);
    }
    /* wrap main to pass argc/argv */
    co_printf(&o,
        "int main(int argc, char **argv) {\n"
        "    basalt_set_args(argc, argv);\n"
        "    return main_bd();\n"
        "}\n");
    /* rename generated main to main_bd — fix by renaming in emit_func */
    return o.buf;
}

/* rename: emit main as main_bd */
static char *fix_main_name(char *src) {
    /* replace function definition "int main(" with "int main_bd(" but not main(int argc */
    size_t n = strlen(src);
    char *out = xmalloc(n + 64);
    out[0] = 0;
    const char *p = src;
    const char *key = "int main(";
    const char *key2 = "int64_t main(";
    while (*p) {
        if (!strncmp(p, key, strlen(key)) && strncmp(p, "int main(int", 12) != 0) {
            strcat(out, "int main_bd(");
            p += strlen(key);
        } else if (!strncmp(p, key2, strlen(key2))) {
            strcat(out, "int64_t main_bd(");
            p += strlen(key2);
        } else if (!strncmp(p, "int main();", 11)) {
            strcat(out, "int main_bd();");
            p += 11;
        } else {
            size_t len = strlen(out);
            out[len] = *p++;
            out[len + 1] = 0;
        }
    }
    free(src);
    return out;
}

int emit_and_link(Node *prog, Options *opt, const char *input_path) {
    char *csrc = codegen_c(prog, opt);
    csrc = fix_main_name(csrc);

    char cpath[512], out_path[512], errpath[512];
    const char *base = input_path;
    {
        const char *p;
        for (p = input_path; *p; p++) {
            if (*p == '/' || *p == '\\') base = p + 1;
        }
    }
    char name[256];
    strncpy(name, base, sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
    char *dot = strrchr(name, '.');
    if (dot) *dot = 0;

    if (opt->output)
        snprintf(out_path, sizeof(out_path), "%s", opt->output);
    else
        snprintf(out_path, sizeof(out_path), "%s", name);

    snprintf(cpath, sizeof(cpath), "%s/%s.bd.c", basalt_tmpdir(), name);
    snprintf(errpath, sizeof(errpath), "%s/bd_gcc_err.txt", basalt_tmpdir());

    FILE *f = fopen(cpath, "w");
    if (!f) die("cannot write %s", cpath);
    fputs(csrc, f);
    fclose(f);
    if (opt->verbose) fprintf(stderr, "  wrote %s (C)\n", cpath);

    if (opt->emit_asm) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", cpath, out_path);
        system(cmd);
        free(csrc);
        return 0;
    }

    /* locate runtime */
    char rt_tmp[512];
    snprintf(rt_tmp, sizeof(rt_tmp), "%s/basalt_rt.o", basalt_tmpdir());
    const char *rt_candidates[] = {
        rt_tmp,
        "bin/basalt_rt.o",
        "bin\\basalt_rt.o",
        NULL
    };
    const char *rt = NULL;
    for (int i = 0; rt_candidates[i]; i++) {
        if (access(rt_candidates[i], R_OK) == 0) { rt = rt_candidates[i]; break; }
    }

    char cmd[4096];
    if (rt)
        snprintf(cmd, sizeof(cmd),
            "gcc -O2 -o \"%s\" \"%s\" \"%s\" 2>\"%s\"",
            out_path, cpath, rt, errpath);
    else
        snprintf(cmd, sizeof(cmd),
            "gcc -O2 -o \"%s\" \"%s\" 2>\"%s\"",
            out_path, cpath, errpath);

    if (opt->verbose) fprintf(stderr, "  %s\n", cmd);
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "stage0: gcc failed\n");
        FILE *e = fopen(errpath, "r");
        if (e) {
            char line[512];
            while (fgets(line, sizeof(line), e)) fputs(line, stderr);
            fclose(e);
        }
        free(csrc);
        return 1;
    }
    if (opt->verbose) fprintf(stderr, "  produced %s\n", out_path);
    free(csrc);
    return 0;
}

/* keep old name for basm path unused */
char *codegen(Node *prog, Options *opt) {
    return codegen_c(prog, opt);
}
