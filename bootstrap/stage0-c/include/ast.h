#ifndef BASALT_AST_H
#define BASALT_AST_H

#include "token.h"
#include "type.h"
#include <stdbool.h>

typedef enum {
    NODE_INVALID = 0,

    /* Expressions */
    NODE_INT_LIT,
    NODE_FLOAT_LIT,
    NODE_STRING_LIT,
    NODE_CHAR_LIT,
    NODE_IDENT,
    NODE_BINARY,
    NODE_UNARY,
    NODE_CALL,
    NODE_INDEX,
    NODE_MEMBER,
    NODE_CAST,
    NODE_ADDR,       /* &x */
    NODE_DEREF,      /* *x */

    /* Statements */
    NODE_BLOCK,
    NODE_EXPR_STMT,
    NODE_BOX_DECL,       /* box / const_box */
    NODE_ASSIGN,
    NODE_IF,
    NODE_WHILE,
    NODE_FOR,
    NODE_SWITCH,
    NODE_CASE,
    NODE_RETURN,
    NODE_FREE,
    NODE_PANIC,
    NODE_SAFE_BLOCK,
    NODE_BREAK,
    NODE_CONTINUE,

    /* Top-level */
    NODE_FUNC,
    NODE_IMPORT,
    NODE_MACRO_DEF,
    NODE_PROGRAM,
} NodeKind;

typedef struct Node Node;

struct Node {
    NodeKind kind;
    int      line;
    Type    *type;           /* filled by sema */

    union {
        /* literals */
        int64_t  i64;
        double   f64;
        char    *str;

        /* ident */
        struct {
            char *name;
            /* resolved later */
            Node *decl;      /* points to BOX_DECL or param */
        } ident;

        /* binary / unary */
        struct {
            TokenKind op;
            Node *lhs;
            Node *rhs;       /* NULL for unary */
        } bin;

        /* call */
        struct {
            Node  *callee;
            Node **args;
            int    nargs;
        } call;

        /* box / const_box */
        struct {
            char  *name;
            bool   is_const;
            Node  *init;
            Type  *decl_type;
            Node  *into_target;  /* for -into */
            /* semantic */
            bool   is_null;      /* after free */
            int    slot_size;    /* for placement */
        } box;

        /* assign */
        struct {
            Node *lhs;
            Node *rhs;
        } assign;

        /* if / while / for */
        struct {
            Node *cond;
            Node *then_body;
            Node *else_body;     /* if */
            Node *init;          /* for */
            Node *step;          /* for */
        } ctrl;

        /* block / program / safe */
        struct {
            Node **stmts;
            int    nstmts;
        } block;

        /* function */
        struct {
            char   *name;
            bool    is_cpu;
            bool    is_gpu;
            bool    is_safe;
            bool    is_trusted;
            Type  **param_types;
            char  **param_names;
            int     nparams;
            Type   *ret_type;
            Node   *body;
        } func;

        /* return / free / panic */
        Node *expr;

        /* import */
        char *import_path;

        /* switch */
        struct {
            Node  *expr;
            Node **cases;
            int    ncases;
            Node  *default_case;
        } sw;
    };
};

/* Helpers */
Node *node_new(NodeKind k, int line);
void  node_free(Node *n);
void  ast_print(Node *n, int indent);

#endif
