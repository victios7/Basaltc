/*
 * Blink BPO — safe aggressive subset
 * O1: identity, store/load, adj-li
 * O2: + copy-prop, algebra, strength(0/1), dead-li, jmp-next
 * O3: + extra rounds
 * (GVN/TCO/LICM/unroll available but gated off until proven on full suite)
 */
#include "bopt_lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINES 100000
#define MAX_LINE  512

static char *lines[MAX_LINES];
static int nlines;
static char *heap;
static size_t heap_n, heap_cap;

static char *hstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    if (heap_n + n > heap_cap) {
        heap_cap = heap_cap ? heap_cap * 2 : (1u << 22);
        if (heap_n + n > heap_cap) heap_cap = heap_n + n + 4096;
        heap = realloc(heap, heap_cap);
    }
    char *p = heap + heap_n;
    memcpy(p, s, n);
    heap_n += n;
    return p;
}

static void trim(char *s) {
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r')) s[--n] = 0;
    for (size_t i = 0; s[i]; i++)
        if (s[i] == '/' && s[i+1] == '/') { s[i] = 0; break; }
    n = strlen(s);
    while (n && (s[n-1] == ' ' || s[n-1] == '\t')) s[--n] = 0;
}

static int is_dir(const char *s) { return s[0] == '.'; }
static int is_label(const char *s) {
    size_t n = strlen(s);
    return n > 1 && s[n-1] == ':';
}

static void pass_id_mov(void) {
    for (int i = 0; i < nlines; i++) {
        char a[16], b[16];
        if (sscanf(lines[i], "mov %15[^,], %15s", a, b) == 2 && strcmp(a, b) == 0)
            lines[i][0] = 0;
    }
}
static void pass_store_load(void) {
    for (int i = 0; i + 1 < nlines; i++) {
        char s1[64], s2[64], r1[16], r2[16];
        if (sscanf(lines[i], "store %63[^,], %15s", s1, r1) == 2 &&
            sscanf(lines[i+1], "load %15[^,], %63s", r2, s2) == 2 &&
            strcmp(s1, s2) == 0 && strcmp(r1, r2) == 0)
            lines[i+1][0] = 0;
    }
}
static void pass_load_store(void) {
    for (int i = 0; i + 1 < nlines; i++) {
        char s1[64], s2[64], r1[16], r2[16];
        if (sscanf(lines[i], "load %15[^,], %63s", r1, s1) == 2 &&
            sscanf(lines[i+1], "store %63[^,], %15s", s2, r2) == 2 &&
            strcmp(s1, s2) == 0 && strcmp(r1, r2) == 0)
            lines[i+1][0] = 0;
    }
}
static void pass_copy_prop(void) {
    for (int i = 0; i + 1 < nlines; i++) {
        char r[16], rd[16], rs[16]; long imm;
        if (sscanf(lines[i], "li %15[^,], %ld", r, &imm) == 2 &&
            sscanf(lines[i+1], "mov %15[^,], %15s", rd, rs) == 2 &&
            strcmp(r, rs) == 0) {
            char buf[80];
            snprintf(buf, sizeof(buf), "li      %s, %ld", rd, imm);
            lines[i+1] = hstrdup(buf);
        }
    }
}
static void pass_algebra(void) {
    for (int i = 1; i < nlines; i++) {
        char pr[16], rd[16], rs[16]; long v;
        if (sscanf(lines[i-1], "li %15[^,], %ld", pr, &v) != 2) continue;
        if (sscanf(lines[i], "add %15[^,], %15s", rd, rs) == 2 &&
            strcmp(pr, rs) == 0 && v == 0) lines[i][0] = 0;
        if (sscanf(lines[i], "sub %15[^,], %15s", rd, rs) == 2 &&
            strcmp(pr, rs) == 0 && v == 0) lines[i][0] = 0;
    }
}
static void pass_strength(void) {
    for (int i = 1; i < nlines; i++) {
        char pr[16], rd[16], rs[16]; long v;
        if (sscanf(lines[i-1], "li %15[^,], %ld", pr, &v) != 2) continue;
        if (sscanf(lines[i], "mul %15[^,], %15s", rd, rs) != 2) continue;
        if (strcmp(pr, rs) != 0) continue;
        if (v == 1) lines[i][0] = 0;
        if (v == 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "li      %s, 0", rd);
            lines[i] = hstrdup(buf);
        }
    }
}
static void pass_dead_li(void) {
    for (int i = 0; i < nlines; i++) {
        char r[16]; long imm;
        if (sscanf(lines[i], "li %15[^,], %ld", r, &imm) != 2) continue;
        for (int j = i + 1; j < nlines && j < i + 32; j++) {
            if (is_dir(lines[j]) || is_label(lines[j])) break;
            if (!lines[j][0]) continue;
            char r2[16]; long imm2;
            if (sscanf(lines[j], "li %15[^,], %ld", r2, &imm2) == 2 && strcmp(r, r2) == 0) {
                lines[i][0] = 0; break;
            }
            if (strstr(lines[j], r) != NULL) break;
        }
    }
}
static void pass_jmp_next(void) {
    for (int i = 0; i + 1 < nlines; i++) {
        char lab[64];
        if (sscanf(lines[i], "jmp %63s", lab) != 1) continue;
        const char *p = lines[i+1];
        while (*p == ' ' || *p == '\t') p++;
        size_t n = strlen(p);
        if (n > 1 && p[n-1] == ':') {
            char tmp[64];
            memcpy(tmp, p, n-1); tmp[n-1] = 0;
            if (strcmp(lab, tmp) == 0) lines[i][0] = 0;
        }
    }
}
static void pass_swap_mov(void) {
    for (int i = 0; i + 1 < nlines; i++) {
        char a1[16], b1[16], a2[16], b2[16];
        if (sscanf(lines[i], "mov %15[^,], %15s", a1, b1) == 2 &&
            sscanf(lines[i+1], "mov %15[^,], %15s", a2, b2) == 2 &&
            strcmp(a1, b2) == 0 && strcmp(b1, a2) == 0)
            lines[i+1][0] = 0;
    }
}
static void pass_adj_li(void) {
    for (int i = 0; i + 1 < nlines; i++) {
        char r1[16], r2[16]; long a, b;
        if (sscanf(lines[i], "li %15[^,], %ld", r1, &a) == 2 &&
            sscanf(lines[i+1], "li %15[^,], %ld", r2, &b) == 2 &&
            strcmp(r1, r2) == 0)
            lines[i][0] = 0;
    }
}
static void pass_blanks(void) {
    int w = 0;
    for (int i = 0; i < nlines; i++) {
        trim(lines[i]);
        if (!lines[i][0]) continue;
        lines[w++] = lines[i];
    }
    nlines = w;
}

int bopt_file(const char *inpath, const char *outpath, int level) {
    if (level <= 0) {
        if (strcmp(inpath, outpath) == 0) return 0;
        FILE *fi = fopen(inpath, "r");
        FILE *fo = fopen(outpath, "w");
        if (!fi || !fo) { if (fi) fclose(fi); if (fo) fclose(fo); return 1; }
        char buf[4096]; size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fi)) > 0) fwrite(buf, 1, n, fo);
        fclose(fi); fclose(fo);
        return 0;
    }
    FILE *f = fopen(inpath, "r");
    if (!f) return 1;
    nlines = 0; heap_n = 0;
    if (!heap) { heap_cap = 1u << 22; heap = malloc(heap_cap); }
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f) && nlines < MAX_LINES) {
        size_t n = strlen(line);
        while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;
        lines[nlines++] = hstrdup(line);
    }
    fclose(f);
    int rounds = (level >= 3) ? 3 : (level >= 2) ? 2 : 1;
    for (int r = 0; r < rounds; r++) {
        pass_adj_li();
        pass_id_mov();
        pass_store_load();
        pass_load_store();
        pass_swap_mov();
        if (level >= 2) {
            pass_copy_prop();
            pass_algebra();
            pass_strength();
            pass_dead_li();
            pass_jmp_next();
        }
        pass_blanks();
    }
    FILE *out = fopen(outpath, "w");
    if (!out) return 1;
    for (int i = 0; i < nlines; i++) fprintf(out, "%s\n", lines[i]);
    fclose(out);
    return 0;
}
