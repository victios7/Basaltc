#include "basalt.h"

typedef struct {
    Token *toks;
    int    ntoks;
    int    cur;
    Source *src;
} Parser;

static Token *peek(Parser *P) {
    return &P->toks[P->cur];
}

static Token *advance(Parser *P) {
    return &P->toks[P->cur++];
}

static bool check(Parser *P, TokenKind k) {
    return peek(P)->kind == k;
}

static bool match(Parser *P, TokenKind k) {
    if (check(P, k)) { advance(P); return true; }
    return false;
}

static Token *expect(Parser *P, TokenKind k) {
    if (check(P, k)) return advance(P);
    error_at(peek(P)->line, peek(P)->col,
             "expected %s, got %s", token_kind_name(k), token_kind_name(peek(P)->kind));
    return NULL;
}

Node *node_new(NodeKind k, int line) {
    Node *n = xmalloc(sizeof(Node));
    memset(n, 0, sizeof(*n));
    n->kind = k;
    n->line = line;
    return n;
}

void node_free(Node *n) {
    /* TODO recursive free */
    (void)n;
}

static void ast_print_indent(int indent) {
    for (int i = 0; i < indent; i++) fputs("  ", stderr);
}

void ast_print(Node *n, int indent) {
    if (!n) { ast_print_indent(indent); fprintf(stderr, "(null)\n"); return; }
    ast_print_indent(indent);
    switch (n->kind) {
        case NODE_PROGRAM:
            fprintf(stderr, "PROGRAM\n");
            for (int i = 0; i < n->block.nstmts; i++)
                ast_print(n->block.stmts[i], indent + 1);
            break;
        case NODE_FUNC:
            fprintf(stderr, "FUNC %s%s%s (%s)\n",
                    n->func.is_cpu ? "@cpu_" : n->func.is_gpu ? "@gpu_" : "",
                    n->func.name,
                    n->func.is_safe ? " @safe" : "",
                    n->func.ret_type ? type_str(n->func.ret_type) : "?");
            if (n->func.body) ast_print(n->func.body, indent + 1);
            break;
        case NODE_BLOCK:
            fprintf(stderr, "BLOCK\n");
            for (int i = 0; i < n->block.nstmts; i++)
                ast_print(n->block.stmts[i], indent + 1);
            break;
        case NODE_BOX_DECL:
            fprintf(stderr, "BOX %s%s : %s\n",
                    n->box.is_const ? "const " : "",
                    n->box.name,
                    n->box.decl_type ? type_str(n->box.decl_type) : "?");
            if (n->box.init) ast_print(n->box.init, indent + 1);
            break;
        case NODE_INT_LIT:
            fprintf(stderr, "INT %lld\n", (long long)n->i64);
            break;
        case NODE_STRING_LIT:
            fprintf(stderr, "STRING \"%s\"\n", n->str ? n->str : "");
            break;
        case NODE_IDENT:
            fprintf(stderr, "IDENT %s\n", n->ident.name);
            break;
        case NODE_BINARY:
            fprintf(stderr, "BIN %s\n", token_kind_name(n->bin.op));
            ast_print(n->bin.lhs, indent + 1);
            if (n->bin.rhs) ast_print(n->bin.rhs, indent + 1);
            break;
        case NODE_CALL:
            fprintf(stderr, "CALL\n");
            ast_print(n->call.callee, indent + 1);
            for (int i = 0; i < n->call.nargs; i++)
                ast_print(n->call.args[i], indent + 1);
            break;
        case NODE_RETURN:
            fprintf(stderr, "RETURN\n");
            if (n->expr) ast_print(n->expr, indent + 1);
            break;
        case NODE_IF:
            fprintf(stderr, "IF\n");
            ast_print(n->ctrl.cond, indent + 1);
            ast_print(n->ctrl.then_body, indent + 1);
            if (n->ctrl.else_body) ast_print(n->ctrl.else_body, indent + 1);
            break;
        case NODE_WHILE:
            fprintf(stderr, "WHILE\n");
            ast_print(n->ctrl.cond, indent + 1);
            ast_print(n->ctrl.then_body, indent + 1);
            break;
        default:
            fprintf(stderr, "NODE %d\n", n->kind);
            break;
    }
}

/* ========== Type parsing ========== */
static Type *parse_type(Parser *P) {
    if (match(P, TOK_VOID)) return ty_void;
    if (match(P, TOK_STRING_TYPE)) return ty_string;

    if (match(P, TOK_INT_TYPE)) {
        int bits = 32; /* default */
        if (match(P, TOK_LT)) {
            Token *n = expect(P, TOK_INT);
            bits = (int)n->val.i64;
            expect(P, TOK_GT);
        } else if (check(P, TOK_INT)) {
            /* int32 style written as int32? we treat "int32" as ident later */
        }
        /* also accept int8, int16... as keywords? currently only "int" */
        return type_int(bits);
    }
    if (match(P, TOK_FLOAT_TYPE)) {
        int bits = 64;
        if (match(P, TOK_LT)) {
            Token *n = expect(P, TOK_INT);
            bits = (int)n->val.i64;
            expect(P, TOK_GT);
        }
        return type_float(bits);
    }

    /* int32 / int64 / int7 / uintN / float32 etc */
    if (check(P, TOK_IDENT)) {
        Token *t = peek(P);
        if (t->len == 5 && !memcmp(t->start, "int32", 5)) { advance(P); return ty_int32; }
        if (t->len == 5 && !memcmp(t->start, "int64", 5)) { advance(P); return ty_int64; }
        if (t->len == 4 && !memcmp(t->start, "int8", 4))  { advance(P); return ty_int8; }
        if (t->len == 5 && !memcmp(t->start, "int16", 5)) { advance(P); return ty_int16; }
        if (t->len == 7 && !memcmp(t->start, "float32", 7)) { advance(P); return ty_float32; }
        if (t->len == 7 && !memcmp(t->start, "float64", 7)) { advance(P); return ty_float64; }
        /* intN / uintN for any digit suffix */
        if (t->len >= 4 && t->start[0]=='i' && t->start[1]=='n' && t->start[2]=='t') {
            int ok = 1, bits = 0;
            for (int k = 3; k < t->len; k++) {
                if (t->start[k] < '0' || t->start[k] > '9') { ok = 0; break; }
                bits = bits * 10 + (t->start[k] - '0');
            }
            if (ok && bits > 0 && bits <= 64) { advance(P); return type_int(bits); }
        }
        if (t->len >= 5 && t->start[0]=='u' && t->start[1]=='i' && t->start[2]=='n' && t->start[3]=='t') {
            int ok = 1, bits = 0;
            for (int k = 4; k < t->len; k++) {
                if (t->start[k] < '0' || t->start[k] > '9') { ok = 0; break; }
                bits = bits * 10 + (t->start[k] - '0');
            }
            if (ok && bits > 0 && bits <= 64) { advance(P); return type_int(bits); /* signed storage */ }
        }
    }

    error_at(peek(P)->line, peek(P)->col, "expected type");
    return ty_void;
}

/* ========== Expressions (Pratt) ========== */
static Node *parse_expr(Parser *P);
static Node *parse_primary(Parser *P);

static int prec_of(TokenKind k) {
    switch (k) {
        case TOK_OR: return 1;
        case TOK_AND: return 2;
        case TOK_EQ: case TOK_NEQ: return 3;
        case TOK_LT: case TOK_GT: case TOK_LE: case TOK_GE: return 4;
        case TOK_PLUS: case TOK_MINUS: return 5;
        case TOK_STAR: case TOK_SLASH: case TOK_PERCENT: return 6;
        case TOK_LSHIFT: case TOK_RSHIFT: return 7;
        case TOK_AMP: case TOK_PIPE: case TOK_CARET: return 8;
        default: return -1;
    }
}

static Node *parse_unary(Parser *P) {
    if (match(P, TOK_MINUS) || match(P, TOK_NOT) || match(P, TOK_TILDE) ||
        match(P, TOK_STAR) || match(P, TOK_AMP)) {
        TokenKind op = P->toks[P->cur - 1].kind;
        Node *rhs = parse_unary(P);
        Node *n = node_new(NODE_UNARY, rhs->line);
        n->bin.op = op;
        n->bin.rhs = rhs;
        return n;
    }
    return parse_primary(P);
}

static Node *parse_binary(Parser *P, int min_prec) {
    Node *lhs = parse_unary(P);
    for (;;) {
        TokenKind op = peek(P)->kind;
        int prec = prec_of(op);
        if (prec < min_prec) break;
        advance(P);
        Node *rhs = parse_binary(P, prec + 1);
        Node *n = node_new(NODE_BINARY, lhs->line);
        n->bin.op = op;
        n->bin.lhs = lhs;
        n->bin.rhs = rhs;
        lhs = n;
    }
    return lhs;
}

static Node *parse_expr(Parser *P) {
    return parse_binary(P, 0);
}

static Node *parse_primary(Parser *P) {
    Token *t = peek(P);

    if (match(P, TOK_INT) || match(P, TOK_CHAR)) {
        Node *n = node_new(NODE_INT_LIT, t->line);
        n->i64 = t->val.i64;
        n->type = ty_int64; /* will be refined */
        return n;
    }
    if (match(P, TOK_FLOAT)) {
        Node *n = node_new(NODE_FLOAT_LIT, t->line);
        n->f64 = t->val.f64;
        n->type = ty_float64;
        return n;
    }
    if (match(P, TOK_STRING)) {
        Node *n = node_new(NODE_STRING_LIT, t->line);
        n->str = t->val.str;
        n->type = ty_string;
        return n;
    }
    if (match(P, TOK_TRUE) || match(P, TOK_FALSE)) {
        Node *n = node_new(NODE_INT_LIT, t->line);
        n->i64 = (t->kind == TOK_TRUE);
        n->type = ty_int8;
        return n;
    }
    if (match(P, TOK_IDENT)) {
        Node *n = node_new(NODE_IDENT, t->line);
        n->ident.name = xstrndup(t->start, t->len);

        /* call? */
        if (match(P, TOK_LPAREN)) {
            Node *call = node_new(NODE_CALL, n->line);
            call->call.callee = n;
            call->call.args = NULL;
            call->call.nargs = 0;
            if (!check(P, TOK_RPAREN)) {
                do {
                    Node *arg = parse_expr(P);
                    call->call.args = xrealloc(call->call.args, (call->call.nargs + 1) * sizeof(Node *));
                    call->call.args[call->call.nargs++] = arg;
                } while (match(P, TOK_COMMA));
            }
            expect(P, TOK_RPAREN);
            return call;
        }
        return n;
    }
    if (match(P, TOK_LPAREN)) {
        Node *e = parse_expr(P);
        expect(P, TOK_RPAREN);
        return e;
    }

    error_at(t->line, t->col, "expected expression, got %s", token_kind_name(t->kind));
    return NULL;
}

/* ========== Statements ========== */
static Node *parse_stmt(Parser *P);
static Node *parse_block(Parser *P);

static Node *parse_box(Parser *P, bool is_const) {
    Token *name = expect(P, TOK_IDENT);
    expect(P, TOK_ASSIGN);
    Node *init = parse_expr(P);
    Type *ty = parse_type(P);

    Node *into = NULL;
    if (match(P, TOK_INTO)) {
        expect(P, TOK_LPAREN);
        into = parse_expr(P); /* target pool or var */
        expect(P, TOK_RPAREN);
    }

    Node *n = node_new(NODE_BOX_DECL, name->line);
    n->box.name = xstrndup(name->start, name->len);
    n->box.is_const = is_const;
    n->box.init = init;
    n->box.decl_type = ty;
    n->box.into_target = into;
    return n;
}


/* :std:pool("name", bytes) [-vram|-ssd]  →  call pool_create(name, bytes, tier) */
static Node *parse_std_pool(Parser *P) {
    /* already consumed leading ':' */
    Token *mod = expect(P, TOK_IDENT);
    if (mod->len != 3 || memcmp(mod->start, "std", 3))
        error_at(mod->line, mod->col, "expected std after :");
    expect(P, TOK_COLON);
    Token *fn = expect(P, TOK_IDENT);
    if (fn->len != 4 || memcmp(fn->start, "pool", 4))
        error_at(fn->line, fn->col, "expected pool");
    expect(P, TOK_LPAREN);
    Node *name = parse_expr(P);
    expect(P, TOK_COMMA);
    Node *bytes = parse_expr(P);
    expect(P, TOK_RPAREN);
    int64_t tier = 0; /* RAM */
    if (match(P, TOK_MINUS)) {
        Token *t = expect(P, TOK_IDENT);
        if (t->len == 4 && !memcmp(t->start, "vram", 4)) tier = 2;
        else if (t->len == 3 && !memcmp(t->start, "ssd", 3)) tier = 1;
        else if (t->len == 3 && !memcmp(t->start, "ram", 3)) tier = 0;
        else error_at(t->line, t->col, "expected ram|ssd|vram");
    }
    Node *tier_n = node_new(NODE_INT_LIT, fn->line);
    tier_n->i64 = tier;
    Node *cal = node_new(NODE_IDENT, fn->line);
    cal->ident.name = xstrdup("pool_create");
    Node *call = node_new(NODE_CALL, fn->line);
    call->call.callee = cal;
    call->call.nargs = 3;
    call->call.args = xmalloc(3 * sizeof(Node *));
    call->call.args[0] = name;
    call->call.args[1] = bytes;
    call->call.args[2] = tier_n;
    Node *st = node_new(NODE_EXPR_STMT, fn->line);
    st->expr = call;
    return st;
}

static Node *parse_stmt(Parser *P) {
    if (match(P, TOK_COLON)) return parse_std_pool(P);
    if (match(P, TOK_BOX)) return parse_box(P, false);
    if (match(P, TOK_CONST_BOX)) return parse_box(P, true);

    if (match(P, TOK_RETURN)) {
        expect(P, TOK_LPAREN);
        Node *e = NULL;
        if (!check(P, TOK_RPAREN)) e = parse_expr(P);
        expect(P, TOK_RPAREN);
        Node *n = node_new(NODE_RETURN, peek(P)->line);
        n->expr = e;
        return n;
    }

    if (match(P, TOK_IF)) {
        Node *cond = parse_expr(P);
        Node *then_b = parse_block(P);
        Node *else_b = NULL;
        if (match(P, TOK_ELSE)) {
            if (check(P, TOK_IF)) else_b = parse_stmt(P);
            else else_b = parse_block(P);
        }
        Node *n = node_new(NODE_IF, cond->line);
        n->ctrl.cond = cond;
        n->ctrl.then_body = then_b;
        n->ctrl.else_body = else_b;
        return n;
    }

    if (match(P, TOK_WHILE)) {
        Node *cond = parse_expr(P);
        Node *body = parse_block(P);
        Node *n = node_new(NODE_WHILE, cond->line);
        n->ctrl.cond = cond;
        n->ctrl.then_body = body;
        return n;
    }

    if (match(P, TOK_FREE)) {
        expect(P, TOK_LPAREN);
        Node *e = parse_expr(P);
        expect(P, TOK_RPAREN);
        Node *n = node_new(NODE_FREE, e->line);
        n->expr = e;
        return n;
    }

    if (match(P, TOK_PANIC)) {
        Node *n = node_new(NODE_PANIC, peek(P)->line);
        if (match(P, TOK_LPAREN)) {
            if (!check(P, TOK_RPAREN)) n->expr = parse_expr(P);
            expect(P, TOK_RPAREN);
        }
        return n;
    }

    if (match(P, TOK_SAFE)) {
        expect(P, TOK_LBRACK);
        Node *b = node_new(NODE_SAFE_BLOCK, peek(P)->line);
        b->block.stmts = NULL;
        b->block.nstmts = 0;
        while (!check(P, TOK_RBRACK) && !check(P, TOK_EOF)) {
            Node *s = parse_stmt(P);
            b->block.stmts = xrealloc(b->block.stmts, (b->block.nstmts + 1) * sizeof(Node *));
            b->block.stmts[b->block.nstmts++] = s;
        }
        expect(P, TOK_RBRACK);
        return b;
    }

    if (match(P, TOK_BREAK)) {
        return node_new(NODE_BREAK, peek(P)->line);
    }
    if (match(P, TOK_CONTINUE)) {
        return node_new(NODE_CONTINUE, peek(P)->line);
    }

    /* expression statement or assignment */
    Node *e = parse_expr(P);
    if (match(P, TOK_ASSIGN)) {
        Node *rhs = parse_expr(P);
        Node *n = node_new(NODE_ASSIGN, e->line);
        n->assign.lhs = e;
        n->assign.rhs = rhs;
        return n;
    }
    Node *s = node_new(NODE_EXPR_STMT, e->line);
    s->expr = e;
    return s;
}

static Node *parse_block(Parser *P) {
    expect(P, TOK_LBRACK); /* Basalt uses [ ] for blocks as per draft */
    Node *b = node_new(NODE_BLOCK, peek(P)->line);
    b->block.stmts = NULL;
    b->block.nstmts = 0;
    while (!check(P, TOK_RBRACK) && !check(P, TOK_EOF)) {
        Node *s = parse_stmt(P);
        b->block.stmts = xrealloc(b->block.stmts, (b->block.nstmts + 1) * sizeof(Node *));
        b->block.stmts[b->block.nstmts++] = s;
    }
    expect(P, TOK_RBRACK);
    return b;
}

static Node *parse_function(Parser *P) {
    bool is_cpu = false, is_gpu = false, is_safe = false, is_trusted = false;

    while (check(P, TOK_AT_CPU) || check(P, TOK_AT_GPU) ||
           check(P, TOK_AT_SAFE) || check(P, TOK_AT_TRUSTED)) {
        if (match(P, TOK_AT_CPU)) is_cpu = true;
        else if (match(P, TOK_AT_GPU)) is_gpu = true;
        else if (match(P, TOK_AT_SAFE)) is_safe = true;
        else if (match(P, TOK_AT_TRUSTED)) is_trusted = true;
    }

    /* name is written right after decorator in draft: @cpu_main */
    /* In our lexer @cpu is a token, then IDENT for the name */
    Token *name = expect(P, TOK_IDENT);

    expect(P, TOK_LPAREN);
    char **pnames = NULL;
    Type **ptypes = NULL;
    int nparams = 0;
    if (!check(P, TOK_RPAREN)) {
        do {
            Token *pn = expect(P, TOK_IDENT);
            Type *pt = parse_type(P);
            pnames = xrealloc(pnames, (nparams + 1) * sizeof(char *));
            ptypes = xrealloc(ptypes, (nparams + 1) * sizeof(Type *));
            pnames[nparams] = xstrndup(pn->start, pn->len);
            ptypes[nparams] = pt;
            nparams++;
        } while (match(P, TOK_COMMA));
    }
    expect(P, TOK_RPAREN);
    expect(P, TOK_COLON);
    Type *ret = parse_type(P);

    Node *body = parse_block(P);

    Node *f = node_new(NODE_FUNC, name->line);
    f->func.name = xstrndup(name->start, name->len);
    f->func.is_cpu = is_cpu || (!is_gpu); /* default cpu */
    f->func.is_gpu = is_gpu;
    f->func.is_safe = is_safe;
    f->func.is_trusted = is_trusted;
    f->func.param_names = pnames;
    f->func.param_types = ptypes;
    f->func.nparams = nparams;
    f->func.ret_type = ret;
    f->func.body = body;
    return f;
}

Node *parse(Token *toks, int ntoks, Source *src) {
    Parser P = {toks, ntoks, 0, src};

    Node *prog = node_new(NODE_PROGRAM, 1);
    prog->block.stmts = NULL;
    prog->block.nstmts = 0;

    while (!check(&P, TOK_EOF)) {
        if (check(&P, TOK_AT_CPU) || check(&P, TOK_AT_GPU) ||
            check(&P, TOK_AT_SAFE) || check(&P, TOK_AT_TRUSTED) ||
            check(&P, TOK_IDENT)) {
            /* function */
            Node *f = parse_function(&P);
            prog->block.stmts = xrealloc(prog->block.stmts, (prog->block.nstmts + 1) * sizeof(Node *));
            prog->block.stmts[prog->block.nstmts++] = f;
        } else if (match(&P, TOK_AT_IMPORT)) {
            /* @import "path" or @import :std: */
            Node *imp = node_new(NODE_IMPORT, peek(&P)->line);
            if (check(&P, TOK_STRING)) {
                Token *s = advance(&P);
                imp->import_path = xstrdup(s->val.str);
            } else {
                /* :std: style — skip for now */
                while (!check(&P, TOK_EOF) && peek(&P)->line == imp->line)
                    advance(&P);
            }
            prog->block.stmts = xrealloc(prog->block.stmts, (prog->block.nstmts + 1) * sizeof(Node *));
            prog->block.stmts[prog->block.nstmts++] = imp;
        } else {
            error_at(peek(&P)->line, peek(&P)->col,
                     "unexpected token at top level: %s", token_kind_name(peek(&P)->kind));
        }
    }
    return prog;
}
