#include <stdlib.h>
#include "basalt.h"

#include <stdlib.h>
#ifdef _WIN32
#include <direct.h>
#define basalt_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define basalt_mkdir(p) mkdir(p, 0755)
#endif

/* Portable temp directory (no trailing slash). Creates if missing. */
const char *basalt_tmpdir(void) {
    static char buf[512];
    const char *e = getenv("BASALT_TMP");
    if (!e || !e[0]) e = getenv("TMPDIR");
    if (!e || !e[0]) e = getenv("TEMP");
    if (!e || !e[0]) e = getenv("TMP");
#ifdef _WIN32
    if (!e || !e[0]) e = ".";
#else
    if (!e || !e[0]) e = "/tmp";
#endif
    /* copy and strip trailing slash/backslash */
    size_t n = strlen(e);
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    memcpy(buf, e, n);
    buf[n] = 0;
    while (n > 1 && (buf[n-1] == '/' || buf[n-1] == '\\')) {
        buf[--n] = 0;
    }
    basalt_mkdir(buf);
    return buf;
}


void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) die("out of memory");
    return p;
}

void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n);
    if (!q) die("out of memory");
    return q;
}

char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

char *xstrndup(const char *s, size_t n) {
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "basaltc: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

void error_at(int line, int col, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "error:%d:%d: ", line, col);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

void warning_at(int line, int col, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "warning:%d:%d: ", line, col);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

Source *source_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) die("cannot open '%s'", path);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) die("cannot stat '%s'", path);

    Source *s = xmalloc(sizeof(Source));
    s->path = xstrdup(path);
    s->src  = xmalloc((size_t)sz + 1);
    s->len  = (size_t)sz;
    if (fread(s->src, 1, s->len, f) != s->len)
        die("cannot read '%s'", path);
    s->src[s->len] = 0;
    fclose(f);
    return s;
}

void source_free(Source *s) {
    if (!s) return;
    free(s->path);
    free(s->src);
    free(s);
}
