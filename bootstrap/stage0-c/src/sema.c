#include "basalt.h"

/* Very simple symbol table for Stage 0 */
typedef struct Sym {
    char *name;
    Type *type;
    Node *decl;
    bool  is_null;
    struct Sym *next;
} Sym;

typedef struct Scope {
    Sym *syms;
    struct Scope *parent;
} Scope;

static Scope *scope_push(Scope *parent) {
    Scope *s = xmalloc(sizeof(Scope));
    s->syms = NULL;
    s->parent = parent;
    return s;
}

static void scope_pop(Scope *s) {
    /* leak for Stage 0 simplicity */
    (void)s;
}

static void scope_add(Scope *sc, char *name, Type *ty, Node *decl) {
    Sym *sym = xmalloc(sizeof(Sym));
    sym->name = name;
    sym->type = ty;
    sym->decl = decl;
    sym->is_null = false;
    sym->next = sc->syms;
    sc->syms = sym;
}

static Sym *scope_find(Scope *sc, const char *name) {
    for (Scope *s = sc; s; s = s->parent) {
        for (Sym *sym = s->syms; sym; sym = sym->next) {
            if (!strcmp(sym->name, name)) return sym;
        }
    }
    return NULL;
}

static void sema_expr(Node *n, Scope *sc);
static void sema_stmt(Node *n, Scope *sc, Type *ret_expected);

static void sema_expr(Node *n, Scope *sc) {
    if (!n) return;
    switch (n->kind) {
        case NODE_INT_LIT:
            if (!n->type) n->type = ty_int64;
            break;
        case NODE_FLOAT_LIT:
            if (!n->type) n->type = ty_float64;
            break;
        case NODE_STRING_LIT:
            n->type = ty_string;
            break;
        case NODE_IDENT: {
            Sym *sym = scope_find(sc, n->ident.name);
            if (!sym) {
                /* allow builtins that will be resolved at call site */
                const char *nm = n->ident.name;
                if (!strcmp(nm, "print_int") || !strcmp(nm, "PrintC") ||
                    !strcmp(nm, "print") || !strcmp(nm, "print_str") ||
                    !strcmp(nm, "print_nl") || !strcmp(nm, "panic") ||
                    !strcmp(nm, "read_file") || !strcmp(nm, "write_file") ||
                    !strcmp(nm, "strlen") || !strcmp(nm, "file_size") ||
                    !strcmp(nm, "alloc") || !strcmp(nm, "char_at") || !strcmp(nm, "strcmp") || !strcmp(nm, "substr") || !strcmp(nm, "str_concat") || !strcmp(nm, "itoa") || !strcmp(nm, "exit") || !strcmp(nm, "argc") || !strcmp(nm, "argv") || !strcmp(nm, "link_pe") || !strcmp(nm, "write_pe") || !strcmp(nm, "system") || !strcmp(nm, "getenv") || !strcmp(nm, "str_phase_begin") || !strcmp(nm, "str_phase_end") || !strcmp(nm, "str_persist") || !strcmp(nm, "pool_create") || !strcmp(nm, "pool_free") || !strcmp(nm, "pool_alloc")) {
                    n->type = ty_void;
                    break;
                }
                error_at(n->line, 0, "undefined identifier '%s'", n->ident.name);
            }
            n->type = sym->type;
            n->ident.decl = sym->decl;
            if (sym->is_null)
                error_at(n->line, 0, "use of null / freed variable '%s'", n->ident.name);
            break;
        }
        case NODE_BINARY:
            sema_expr(n->bin.lhs, sc);
            if (n->bin.rhs) sema_expr(n->bin.rhs, sc);
            /* simple: result type = lhs type for now */
            n->type = n->bin.lhs->type;
            break;
        case NODE_UNARY:
            sema_expr(n->bin.rhs, sc);
            n->type = n->bin.rhs->type;
            break;
        case NODE_CALL:
            sema_expr(n->call.callee, sc);
            for (int i = 0; i < n->call.nargs; i++)
                sema_expr(n->call.args[i], sc);
            /* builtins / runtime */
            if (n->call.callee->kind == NODE_IDENT) {
                const char *nm = n->call.callee->ident.name;
                if (!strcmp(nm, "print_int") || !strcmp(nm, "PrintC") ||
                    !strcmp(nm, "print") || !strcmp(nm, "print_str") ||
                    !strcmp(nm, "print_nl") || !strcmp(nm, "panic") ||
                    !strcmp(nm, "read_file") || !strcmp(nm, "write_file") ||
                    !strcmp(nm, "strlen") || !strcmp(nm, "file_size") ||
                    !strcmp(nm, "alloc") || !strcmp(nm, "char_at") || !strcmp(nm, "strcmp") || !strcmp(nm, "substr") || !strcmp(nm, "str_concat") || !strcmp(nm, "itoa") || !strcmp(nm, "exit") || !strcmp(nm, "argc") || !strcmp(nm, "argv") || !strcmp(nm, "link_pe") || !strcmp(nm, "write_pe") || !strcmp(nm, "system") || !strcmp(nm, "getenv") || !strcmp(nm, "str_phase_begin") || !strcmp(nm, "str_phase_end") || !strcmp(nm, "str_persist") || !strcmp(nm, "pool_create") || !strcmp(nm, "pool_free") || !strcmp(nm, "pool_alloc")) {
                    n->type = ty_void;
                    break;
                }
                Sym *sym = scope_find(sc, nm);
                if (sym && sym->type && sym->type->kind == TY_FUNC)
                    n->type = sym->type->ret;
                else
                    n->type = ty_int32;
            } else {
                n->type = ty_int32;
            }
            break;
        default:
            break;
    }
}

static void sema_stmt(Node *n, Scope *sc, Type *ret_expected) {
    if (!n) return;
    switch (n->kind) {
        case NODE_BOX_DECL: {
            if (n->box.init) sema_expr(n->box.init, sc);
            /* type already parsed */
            n->type = n->box.decl_type;
            scope_add(sc, n->box.name, n->box.decl_type, n);
            break;
        }
        case NODE_ASSIGN:
            sema_expr(n->assign.lhs, sc);
            sema_expr(n->assign.rhs, sc);
            break;
        case NODE_EXPR_STMT:
            sema_expr(n->expr, sc);
            break;
        case NODE_RETURN:
            if (n->expr) {
                sema_expr(n->expr, sc);
            }
            break;
        case NODE_IF:
            sema_expr(n->ctrl.cond, sc);
            sema_stmt(n->ctrl.then_body, sc, ret_expected);
            if (n->ctrl.else_body)
                sema_stmt(n->ctrl.else_body, sc, ret_expected);
            break;
        case NODE_WHILE:
            sema_expr(n->ctrl.cond, sc);
            sema_stmt(n->ctrl.then_body, sc, ret_expected);
            break;
        case NODE_BLOCK:
        case NODE_SAFE_BLOCK: {
            Scope *inner = scope_push(sc);
            for (int i = 0; i < n->block.nstmts; i++)
                sema_stmt(n->block.stmts[i], inner, ret_expected);
            scope_pop(inner);
            break;
        }
        case NODE_FREE: {
            sema_expr(n->expr, sc);
            if (n->expr->kind == NODE_IDENT) {
                Sym *sym = scope_find(sc, n->expr->ident.name);
                if (sym) {
                    if (sym->is_null)
                        error_at(n->line, 0, "double free of '%s'", sym->name);
                    sym->is_null = true;
                }
            }
            break;
        }
        case NODE_PANIC:
        case NODE_BREAK:
        case NODE_CONTINUE:
            break;
        default:
            break;
    }
}

void sema(Node *prog, Source *src) {
    (void)src;
    Scope *global = scope_push(NULL);

    /* first pass: register all functions */
    for (int i = 0; i < prog->block.nstmts; i++) {
        Node *n = prog->block.stmts[i];
        if (n->kind == NODE_FUNC) {
            Type *ft = type_func(n->func.param_types, n->func.nparams, n->func.ret_type);
            scope_add(global, n->func.name, ft, n);
        }
    }

    /* second pass: check bodies */
    for (int i = 0; i < prog->block.nstmts; i++) {
        Node *n = prog->block.stmts[i];
        if (n->kind == NODE_FUNC) {
            Scope *fs = scope_push(global);
            for (int p = 0; p < n->func.nparams; p++) {
                scope_add(fs, n->func.param_names[p], n->func.param_types[p], NULL);
            }
            sema_stmt(n->func.body, fs, n->func.ret_type);
            scope_pop(fs);
        }
    }
}
