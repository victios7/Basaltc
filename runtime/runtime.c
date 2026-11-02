#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

void basalt_print_int(int64_t v) { printf("%lld", (long long)v); }
void basalt_print_str(const char *s) { if (s) fputs(s, stdout); }
void basalt_print_nl(void) { putchar('\n'); }
void basalt_print(int64_t v) { basalt_print_int(v); }
void basalt_flush(void) { fflush(stdout); }

void basalt_panic(const char *msg) {
    fprintf(stderr, "panic: %s\n", msg ? msg : "");
    abort();
}

void *basalt_alloc(int64_t size) {
    void *p = malloc((size_t)size);
    if (!p) basalt_panic("OOM");
    return p;
}
void basalt_free(void *p) { free(p); }

int64_t basalt_file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (int64_t)st.st_size;
}

char *basalt_read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    buf[sz] = 0;
    fclose(f);
    return buf;
}

int64_t basalt_write_file(const char *path, const char *data) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (!data) { fclose(f); return -1; }
    size_t nlen = strlen(data);
    size_t n = fwrite(data, 1, nlen, f);
    fclose(f);
    return (int64_t)n;
}

int64_t basalt_strlen(const char *s) { return s ? (int64_t)strlen(s) : 0; }

int64_t basalt_char_at(const char *s, int64_t i) {
    if (!s || i < 0) return -1;
    size_t n = strlen(s);
    if ((size_t)i >= n) return -1;
    return (unsigned char)s[i];
}

int64_t basalt_strcmp(const char *a, const char *b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    return (int64_t)strcmp(a, b);
}

/* ---- Phase arenas (no GC): bump alloc, free whole arena at phase end ----
 * str_phase_begin()  → new/reset arena for this compiler phase
 * str_concat/substr/itoa allocate from current arena
 * str_persist(s)     → malloc copy that survives phase_end
 * str_phase_end()    → free entire arena in one shot
 */
typedef struct PhaseChunk {
    struct PhaseChunk *next;
    size_t cap;
    size_t used;
    char data[];
} PhaseChunk;

static PhaseChunk *g_phase = NULL;
static char *g_persist_list[4096];
static int g_npersist = 0;

static PhaseChunk *phase_chunk_new(size_t need) {
    size_t cap = 1 << 20; /* 1MB slabs */
    if (cap < need + 64) cap = need + 64;
    PhaseChunk *c = (PhaseChunk *)malloc(sizeof(PhaseChunk) + cap);
    if (!c) abort();
    c->next = NULL;
    c->cap = cap;
    c->used = 0;
    return c;
}

void basalt_str_phase_begin(void) {
    /* end previous if any */
    while (g_phase) {
        PhaseChunk *n = g_phase->next;
        free(g_phase);
        g_phase = n;
    }
    g_phase = phase_chunk_new(1 << 20);
}

void basalt_str_phase_end(void) {
    while (g_phase) {
        PhaseChunk *n = g_phase->next;
        free(g_phase);
        g_phase = n;
    }
}

static char *phase_alloc(size_t n) {
    if (!g_phase) basalt_str_phase_begin();
    size_t need = (n + 7) & ~(size_t)7;
    if (g_phase->used + need > g_phase->cap) {
        PhaseChunk *c = phase_chunk_new(need);
        c->next = g_phase;
        g_phase = c;
    }
    char *p = g_phase->data + g_phase->used;
    g_phase->used += need;
    return p;
}

/* Survive past phase_end (results to keep: final asm, paths, …) */
char *basalt_str_persist(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *o = (char *)malloc(n + 1);
    if (!o) abort();
    memcpy(o, s, n + 1);
    if (g_npersist < 4096) g_persist_list[g_npersist++] = o;
    return o;
}

void basalt_str_persist_free_all(void) {
    for (int i = 0; i < g_npersist; i++) free(g_persist_list[i]);
    g_npersist = 0;
}

char *basalt_substr(const char *s, int64_t start, int64_t len) {
    if (!s || start < 0 || len < 0) {
        char *e = phase_alloc(1);
        e[0] = 0;
        return e;
    }
    size_t n = strlen(s);
    if ((size_t)start >= n) {
        char *e = phase_alloc(1);
        e[0] = 0;
        return e;
    }
    if ((size_t)start + (size_t)len > n) len = (int64_t)(n - (size_t)start);
    char *o = phase_alloc((size_t)len + 1);
    memcpy(o, s + (size_t)start, (size_t)len);
    o[len] = 0;
    return o;
}

char *basalt_str_concat(const char *a, const char *b) {
    size_t na = a ? strlen(a) : 0;
    size_t nb = b ? strlen(b) : 0;
    char *o = phase_alloc(na + nb + 1);
    if (na && a) memcpy(o, a, na);
    if (nb && b) memcpy(o + na, b, nb);
    o[na + nb] = 0;
    return o;
}

char *basalt_itoa(int64_t v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)v);
    size_t n = strlen(buf);
    char *o = phase_alloc(n + 1);
    memcpy(o, buf, n + 1);
    return o;
}

static int g_argc;
static char **g_argv;
void basalt_set_args(int argc, char **argv) { g_argc = argc; g_argv = argv; }
int64_t basalt_argc(void) { return (int64_t)g_argc; }
char *basalt_argv(int64_t i) {
    if (i < 0 || i >= g_argc) return NULL;
    return g_argv[i];
}

void basalt_exit(int64_t code) { exit((int)code); }

int64_t basalt_system(const char *cmd) {
    if (!cmd) return -1;
    return (int64_t)system(cmd);
}

void basalt_fprint(double x) { printf("%g", x); }
int64_t basalt_float_pack(double x) { union { double d; int64_t i; } u; u.d = x; return u.i; }
double basalt_float_unpack(int64_t i) { union { double d; int64_t i; } u; u.i = i; return u.d; }
int64_t basalt_fadd(int64_t a, int64_t b) {
    return basalt_float_pack(basalt_float_unpack(a) + basalt_float_unpack(b));
}
int64_t basalt_fsub(int64_t a, int64_t b) {
    return basalt_float_pack(basalt_float_unpack(a) - basalt_float_unpack(b));
}
int64_t basalt_fmul(int64_t a, int64_t b) {
    return basalt_float_pack(basalt_float_unpack(a) * basalt_float_unpack(b));
}
int64_t basalt_fdiv(int64_t a, int64_t b) {
    return basalt_float_pack(basalt_float_unpack(a) / basalt_float_unpack(b));
}
int64_t basalt_fadd_w(int64_t a, int64_t b) { return basalt_fadd(a, b); }
int64_t basalt_fmul_w(int64_t a, int64_t b) { return basalt_fmul(a, b); }

void *basalt_allocator(int64_t n) { return basalt_alloc(n); }
void *basalt_heap_alloc(int64_t n) { return basalt_alloc(n); }
void basalt_box_free(void *p) { basalt_free(p); }

/* named pools: see arena.c */

int64_t basalt_link_pe(const char *a, const char *b) { (void)a; (void)b; return -1; }

#define ALIAS(new, old) void new(void) __attribute__((alias(#old)))
/* dual bedrock_* names for older basm encodings */
void bedrock_print_int(int64_t v) { basalt_print_int(v); }
void bedrock_print_str(const char *s) { basalt_print_str(s); }
void bedrock_print_nl(void) { basalt_print_nl(); }
void bedrock_print(int64_t v) { basalt_print(v); }
void bedrock_set_args(int argc, char **argv) { basalt_set_args(argc, argv); }
void bedrock_panic(const char *m) { basalt_panic(m); }
void *bedrock_alloc(int64_t n) { return basalt_alloc(n); }
void bedrock_free(void *p) { basalt_free(p); }
char *bedrock_read_file(const char *p) { return basalt_read_file(p); }
int64_t bedrock_write_file(const char *p, const char *d) { return basalt_write_file(p, d); }
int64_t bedrock_strlen(const char *s) { return basalt_strlen(s); }
int64_t bedrock_char_at(const char *s, int64_t i) { return basalt_char_at(s, i); }
int64_t bedrock_strcmp(const char *a, const char *b) { return basalt_strcmp(a, b); }
char *bedrock_substr(const char *s, int64_t a, int64_t b) { return basalt_substr(s, a, b); }
char *bedrock_str_concat(const char *a, const char *b) { return basalt_str_concat(a, b); }
char *bedrock_itoa(int64_t v) { return basalt_itoa(v); }
int64_t bedrock_argc(void) { return basalt_argc(); }
char *bedrock_argv(int64_t i) { return basalt_argv(i); }
void bedrock_exit(int64_t c) { basalt_exit(c); }
int64_t bedrock_system(const char *c) { return basalt_system(c); }

/* text write (NUL-terminated) already: basalt_write_file */
int64_t basalt_write_file_text(const char *path, const char *data) {
    return basalt_write_file(path, data);
}

int64_t basalt_write_file_bin(const char *path, const void *data, int64_t n) {
    if (!path || !data || n < 0) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(data, 1, (size_t)n, f);
    fclose(f);
    return (int64_t)w;
}

int64_t basalt_append_file_bin(const char *path, const void *data, int64_t n) {
    if (!path || !data || n < 0) return -1;
    FILE *f = fopen(path, "ab");
    if (!f) return -1;
    size_t w = fwrite(data, 1, (size_t)n, f);
    fclose(f);
    return (int64_t)w;
}

int64_t basalt_append_file_text(const char *path, const char *data) {
    if (!path || !data) return -1;
    FILE *f = fopen(path, "ab");
    if (!f) return -1;
    size_t nlen = strlen(data);
    size_t w = fwrite(data, 1, nlen, f);
    fclose(f);
    return (int64_t)w;
}

void *basalt_read_file_bin(const char *path, int64_t *out_n) {
    if (out_n) *out_n = -1;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    void *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    if (out_n) *out_n = (int64_t)sz;
    return buf;
}


/* std memory cell access (systems) */
int64_t basalt_load_i64(void *p, int64_t off) {
    if (!p || off < 0) return 0;
    int64_t v;
    memcpy(&v, (char *)p + off, 8);
    return v;
}
void basalt_store_i64(void *p, int64_t off, int64_t v) {
    if (!p || off < 0) return;
    memcpy((char *)p + off, &v, 8);
}
int64_t basalt_load_u8(void *p, int64_t off) {
    if (!p || off < 0) return 0;
    return (unsigned char)((char *)p)[off];
}
void basalt_store_u8(void *p, int64_t off, int64_t v) {
    if (!p || off < 0) return;
    ((unsigned char *)p)[off] = (unsigned char)v;
}
void *basalt_ptr_add(void *p, int64_t off) {
    if (!p) return NULL;
    return (char *)p + off;
}
int64_t basalt_memcpy_n(void *dst, void *src, int64_t n) {
    if (!dst || !src || n <= 0) return -1;
    memcpy(dst, src, (size_t)n);
    return n;
}
int64_t basalt_memset_n(void *dst, int64_t byte, int64_t n) {
    if (!dst || n <= 0) return -1;
    memset(dst, (int)byte, (size_t)n);
    return n;
}



char *basalt_getenv(const char *k) {
  if (!k) return 0; char *v=getenv(k); if(!v) return 0;
  size_t n=strlen(v); char *c=malloc(n+1); if(c) memcpy(c,v,n+1); return c;
}
