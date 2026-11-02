#ifndef BASALT_H
#define BASALT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>
#include <stdarg.h>
#include <assert.h>

#include "token.h"
#include "type.h"
#include "ast.h"

/* ========== Common utilities ========== */
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
void  die(const char *fmt, ...);
void  error_at(int line, int col, const char *fmt, ...);
void  warning_at(int line, int col, const char *fmt, ...);

/* ========== Source ========== */
typedef struct {
    char  *path;
    char  *src;
    size_t len;
} Source;

Source *source_load(const char *path);
void    source_free(Source *s);

/* ========== Compiler options ========== */
typedef enum {
    TARGET_ELF64 = 0,
    TARGET_PE64  = 1,
} TargetKind;

typedef struct {
    TargetKind target;
    int        opt_level;     /* 0..3 */
    bool       emit_asm;      /* -S */
    bool       emit_obj;      /* -c */
    bool       verbose;
    bool       dump_ast;
    bool       dump_ir;
    char      *output;        /* -o */
    char     **inputs;
    int        ninputs;
} Options;

void options_parse(Options *opt, int argc, char **argv);
void options_usage(const char *prog);

/* ========== Pipeline stages ========== */
Token *lex(Source *src, int *ntoks);          /* returns array, last is TOK_EOF */
Node  *parse(Token *toks, int ntoks, Source *src);
void   sema(Node *prog, Source *src);
/* IR + opt + codegen come later */

#endif

const char *basalt_tmpdir(void);
