#include "basalt.h"

Type *ty_void;
Type *ty_int8;
Type *ty_int16;
Type *ty_int32;
Type *ty_int64;
Type *ty_float32;
Type *ty_float64;
Type *ty_string;
Type *ty_ptr_void;

static Type *make_type(TypeKind k, int bits) {
    Type *t = xmalloc(sizeof(Type));
    memset(t, 0, sizeof(*t));
    t->kind = k;
    t->bits = bits;
    return t;
}

void type_init(void) {
    ty_void    = make_type(TY_VOID, 0);
    ty_int8    = make_type(TY_INT, 8);
    ty_int16   = make_type(TY_INT, 16);
    ty_int32   = make_type(TY_INT, 32);
    ty_int64   = make_type(TY_INT, 64);
    ty_float32 = make_type(TY_FLOAT, 32);
    ty_float64 = make_type(TY_FLOAT, 64);
    ty_string  = make_type(TY_STRING, 0);
    ty_ptr_void = type_ptr(ty_void, false);

    ty_void->name    = xstrdup("void");
    ty_int8->name    = xstrdup("int8");
    ty_int16->name   = xstrdup("int16");
    ty_int32->name   = xstrdup("int32");
    ty_int64->name   = xstrdup("int64");
    ty_float32->name = xstrdup("float32");
    ty_float64->name = xstrdup("float64");
    ty_string->name  = xstrdup("string");
}

Type *type_int(int bits) {
    if (bits == 8)  return ty_int8;
    if (bits == 16) return ty_int16;
    if (bits == 32) return ty_int32;
    if (bits == 64) return ty_int64;
    /* arbitrary width: create new */
    Type *t = make_type(TY_INT, bits);
    char buf[32];
    snprintf(buf, sizeof(buf), "int%d", bits);
    t->name = xstrdup(buf);
    return t;
}

Type *type_float(int bits) {
    if (bits == 32) return ty_float32;
    if (bits == 64) return ty_float64;
    Type *t = make_type(TY_FLOAT, bits);
    char buf[32];
    snprintf(buf, sizeof(buf), "float%d", bits);
    t->name = xstrdup(buf);
    return t;
}

Type *type_ptr(Type *pointee, bool is_const) {
    Type *t = make_type(TY_PTR, 64);
    t->pointee = pointee;
    t->is_const = is_const;
    return t;
}

Type *type_func(Type **params, int nparams, Type *ret) {
    Type *t = make_type(TY_FUNC, 0);
    t->params = params;
    t->nparams = nparams;
    t->ret = ret;
    return t;
}

bool type_equal(Type *a, Type *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    if (a->kind == TY_INT || a->kind == TY_FLOAT)
        return a->bits == b->bits;
    if (a->kind == TY_PTR)
        return type_equal(a->pointee, b->pointee) && a->is_const == b->is_const;
    return false;
}

bool type_compatible(Type *a, Type *b) {
    if (type_equal(a, b)) return true;
    /* for -into size check we use size */
    return false;
}

int type_size(Type *t) {
    if (!t) return 0;
    switch (t->kind) {
        case TY_VOID:   return 0;
        case TY_INT:
        case TY_FLOAT:  return (t->bits + 7) / 8;
        case TY_PTR:    return 8;
        case TY_STRING: return 16; /* len + ptr */
        default:        return 8;
    }
}

int type_align(Type *t) {
    int s = type_size(t);
    if (s >= 8) return 8;
    if (s >= 4) return 4;
    if (s >= 2) return 2;
    return 1;
}

const char *type_str(Type *t) {
    if (!t) return "<null>";
    if (t->name) return t->name;
    return "?";
}
