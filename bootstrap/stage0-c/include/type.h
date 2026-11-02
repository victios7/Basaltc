#ifndef BASALT_TYPE_H
#define BASALT_TYPE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    TY_INVALID = 0,
    TY_VOID,
    TY_INT,          /* int<N> */
    TY_FLOAT,        /* float<N> */
    TY_STRING,
    TY_PTR,          /* T* */
    TY_FUNC,
    TY_POOL,         /* for arenas */
} TypeKind;

typedef struct Type Type;

struct Type {
    TypeKind kind;
    int      bits;           /* for INT/FLOAT */
    Type    *pointee;        /* for PTR */
    bool     is_const;

    /* function */
    Type   **params;
    int      nparams;
    Type    *ret;

    /* canonical name for debugging */
    char    *name;
};

/* Predefined types */
extern Type *ty_void;
extern Type *ty_int8;
extern Type *ty_int16;
extern Type *ty_int32;
extern Type *ty_int64;
extern Type *ty_float32;
extern Type *ty_float64;
extern Type *ty_string;
extern Type *ty_ptr_void;

void type_init(void);
Type *type_int(int bits);
Type *type_float(int bits);
Type *type_ptr(Type *pointee, bool is_const);
Type *type_func(Type **params, int nparams, Type *ret);
bool  type_equal(Type *a, Type *b);
bool  type_compatible(Type *a, Type *b); /* for assignment / -into size check */
int   type_size(Type *t);                /* bytes */
int   type_align(Type *t);
const char *type_str(Type *t);

#endif
