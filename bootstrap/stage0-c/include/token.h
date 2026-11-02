#ifndef BASALT_TOKEN_H
#define BASALT_TOKEN_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    TOK_EOF = 0,
    TOK_ERROR,

    /* Literals */
    TOK_INT,
    TOK_FLOAT,
    TOK_STRING,
    TOK_CHAR,
    TOK_IDENT,

    /* Keywords */
    TOK_BOX,
    TOK_CONST_BOX,
    TOK_SAFE,
    TOK_FREE,
    TOK_RETURN,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_SWITCH,
    TOK_CASE,
    TOK_DEFAULT,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_PANIC,
    TOK_IMPORT,
    TOK_TRUE,
    TOK_FALSE,

    /* Decorators / special */
    TOK_AT_CPU,      /* @cpu */
    TOK_AT_GPU,      /* @gpu */
    TOK_AT_SAFE,     /* @safe */
    TOK_AT_TRUSTED,  /* @trusted */
    TOK_AT_MACRO,    /* @macro */
    TOK_AT_IMPORT,   /* @import */

    /* Types */
    TOK_INT_TYPE,    /* int / int<N> */
    TOK_FLOAT_TYPE,  /* float / float<N> */
    TOK_STRING_TYPE,
    TOK_VOID,

    /* Operators & punctuation */
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_AMP, TOK_PIPE, TOK_CARET, TOK_TILDE,
    TOK_LSHIFT, TOK_RSHIFT,
    TOK_EQ, TOK_NEQ, TOK_LT, TOK_GT, TOK_LE, TOK_GE,
    TOK_AND, TOK_OR, TOK_NOT,
    TOK_ASSIGN,
    TOK_ARROW,       /* -> */
    TOK_INTO,        /* -into */
    TOK_COLON,
    TOK_COMMA,
    TOK_DOT,
    TOK_LPAREN, TOK_RPAREN,
    TOK_LBRACK, TOK_RBRACK,
    TOK_LBRACE, TOK_RBRACE,
    TOK_SEMI,        /* rare, mostly for compatibility */

    TOK_COUNT
} TokenKind;

typedef struct {
    TokenKind kind;
    const char *start;   /* pointer into source */
    size_t     len;
    int        line;
    int        col;

    /* For literals */
    union {
        int64_t  i64;
        double   f64;
        char    *str;    /* owned for TOK_STRING */
    } val;
} Token;

const char *token_kind_name(TokenKind k);

#endif
