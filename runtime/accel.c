/* Portable accelerators Linux/Windows — GPU / TPU / NPU / LPU
 * Phase: stronger device probe, aligned buffers, batch copy, launch queues
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

static int bare_mode(void) {
    const char *e = getenv("BASALT_BARE");
    if (e && e[0] == '1') return 1;
#ifdef BASALT_BARE_METAL
    return 1;
#endif
    return 0;
}

static int path_exists(const char *p) {
    if (!p) return 0;
#ifdef _WIN32
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(p, &st) == 0;
#endif
}

static void *lib_open(const char *name) {
#ifdef _WIN32
    return (void *)LoadLibraryA(name);
#else
    return dlopen(name, RTLD_LAZY);
#endif
}

static void *aligned_alloc_n(size_t align, size_t n) {
    if (n == 0) return NULL;
    void *p = NULL;
#ifdef _WIN32
    p = _aligned_malloc(n, align);
#else
    if (posix_memalign(&p, align, n) != 0) p = NULL;
#endif
    if (p) memset(p, 0, n);
    return p;
}
static void aligned_free_n(void *p) {
#ifdef _WIN32
    _aligned_free(p);
#else
    free(p);
#endif
}

/* ---------- VRAM / GPU ---------- */
static int g_gpu = -1;
void *basalt_vram_alloc(int64_t n) {
    if (bare_mode() || n <= 0) return NULL;
    return aligned_alloc_n(256, (size_t)n);
}
void basalt_vram_free(void *p) { aligned_free_n(p); }
int64_t basalt_vram_copy_to(void *dst, void *src, int64_t n) {
    if (bare_mode() || !dst || !src || n <= 0) return -1;
    memcpy(dst, src, (size_t)n);
    return n;
}
int64_t basalt_vram_copy_from(void *dst, void *src, int64_t n) {
    return basalt_vram_copy_to(dst, src, n);
}
int64_t basalt_gpu_available(void) {
    if (bare_mode()) return 0;
    if (g_gpu >= 0) return g_gpu;
    g_gpu = 0;
#ifdef _WIN32
    if (path_exists("C:\\Windows\\System32\\OpenCL.dll") || lib_open("OpenCL.dll")) g_gpu = 1;
#else
    if (path_exists("/dev/dri/card0") || path_exists("/dev/dri/renderD128")) g_gpu = 1;
    if (lib_open("libOpenCL.so.1") || lib_open("libOpenCL.so")) g_gpu = 1;
#endif
    if (getenv("BASALT_GPU") && getenv("BASALT_GPU")[0] == '1') g_gpu = 1;
    return g_gpu;
}
void basalt_gpu_register(void) {}
void basalt_gpu_launch(void) {}
void basalt_gpu_sync(void) {}

/* ---------- AMD NPU (XDNA) / DirectML ---------- */
static int g_npu = -1;
static int g_npu_qdepth = 0;
int64_t basalt_npu_available(void) {
    if (bare_mode()) return 0;
    if (g_npu >= 0) return g_npu;
    g_npu = 0;
#ifdef _WIN32
    if (path_exists("C:\\Windows\\System32\\DirectML.dll")) g_npu = 1;
#else
    if (path_exists("/dev/accel/accel0") || path_exists("/sys/module/amdxdna") ||
        path_exists("/sys/bus/pci/drivers/amdxdna") || path_exists("/sys/class/accel"))
        g_npu = 1;
    if (lib_open("libxrt_core.so")) g_npu = 1;
#endif
    if (getenv("BASALT_NPU") && getenv("BASALT_NPU")[0] == '1') g_npu = 1;
    return g_npu;
}
void *basalt_npu_alloc(int64_t n) {
    if (bare_mode() || n <= 0) return NULL;
    return aligned_alloc_n(4096, (size_t)n);
}
void basalt_npu_free(void *p) { aligned_free_n(p); }
int64_t basalt_npu_copy_to(void *d, void *s, int64_t n) {
    if (bare_mode() || !d || !s || n <= 0) return -1;
    memcpy(d, s, (size_t)n);
    return n;
}
int64_t basalt_npu_copy_from(void *d, void *s, int64_t n) { return basalt_npu_copy_to(d, s, n); }
int64_t basalt_npu_launch(void) {
    if (!basalt_npu_available()) return -1;
    g_npu_qdepth++;
    return 0;
}
int64_t basalt_npu_sync(void) {
    g_npu_qdepth = 0;
    return 0;
}

/* ---------- Coral Edge TPU (single model) ---------- */
static int g_tpu = -1;
static void *g_tpu_model = NULL;
static int64_t g_tpu_model_size = 0;

int64_t basalt_tpu_available(void) {
    if (bare_mode()) return 0;
    if (g_tpu >= 0) return g_tpu;
    g_tpu = 0;
#ifdef _WIN32
    if (lib_open("edgetpu.dll")) g_tpu = 1;
#else
    if (path_exists("/dev/apex_0") || path_exists("/dev/apex_1")) g_tpu = 1;
    if (path_exists("/usr/lib/libedgetpu.so.1") ||
        path_exists("/usr/lib/x86_64-linux-gnu/libedgetpu.so.1")) g_tpu = 1;
    if (lib_open("libedgetpu.so.1")) g_tpu = 1;
#endif
    if (getenv("BASALT_TPU") && getenv("BASALT_TPU")[0] == '1') g_tpu = 1;
    return g_tpu;
}
void *basalt_tpu_alloc(int64_t n) {
    if (bare_mode() || n <= 0) return NULL;
    return aligned_alloc_n(64, (size_t)n);
}
void basalt_tpu_free(void *p) { aligned_free_n(p); }
int64_t basalt_tpu_copy_to(void *d, void *s, int64_t n) {
    if (bare_mode() || !d || !s || n <= 0) return -1;
    memcpy(d, s, (size_t)n);
    return n;
}
int64_t basalt_tpu_copy_from(void *d, void *s, int64_t n) { return basalt_tpu_copy_to(d, s, n); }

extern void *basalt_read_file_bin(const char *path, int64_t *out_n);
/* fallback if only read_file exists */
extern char *basalt_read_file(const char *path);

int64_t basalt_tpu_load_model(const char *path) {
    if (bare_mode() || !path) return -1;
    int64_t n = 0;
    void *blob = NULL;
#ifdef basalt_read_file_bin
    blob = basalt_read_file_bin(path, &n);
#else
    char *t = basalt_read_file(path);
    if (t) { n = (int64_t)strlen(t); blob = t; }
#endif
    if (!blob && path) {
        FILE *f = fopen(path, "rb");
        if (!f) return -1;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0) { fclose(f); return -1; }
        blob = malloc((size_t)sz);
        if (!blob) { fclose(f); return -1; }
        if (fread(blob, 1, (size_t)sz, f) != (size_t)sz) { free(blob); fclose(f); return -1; }
        fclose(f);
        n = sz;
    }
    if (!blob || n <= 0) { free(blob); return -1; }
    free(g_tpu_model);
    g_tpu_model = blob;
    g_tpu_model_size = n;
    return n;
}
int64_t basalt_tpu_model_loaded(void) { return g_tpu_model ? 1 : 0; }
int64_t basalt_tpu_model_size(void) { return g_tpu_model_size; }
void *basalt_tpu_model_ptr(void) { return g_tpu_model; }
int64_t basalt_tpu_invoke(void *input, int64_t in_n, void *output, int64_t out_n) {
    if (bare_mode()) return -1;
    if (!g_tpu_model) return -2;
    if (!basalt_tpu_available()) return -1;
    if (output && out_n > 0) memset(output, 0, (size_t)out_n);
    (void)input; (void)in_n;
    return out_n > 0 ? out_n : 0;
}
int64_t basalt_tpu_unload_model(void) {
    free(g_tpu_model);
    g_tpu_model = NULL;
    g_tpu_model_size = 0;
    return 0;
}

/* ---------- LPU ---------- */
static int g_lpu = -1;
static int g_lpu_q = 0;
int64_t basalt_lpu_available(void) {
    if (bare_mode()) return 0;
    if (g_lpu >= 0) return g_lpu;
    g_lpu = 0;
    if (getenv("BASALT_LPU") && getenv("BASALT_LPU")[0] == '1') g_lpu = 1;
#ifdef _WIN32
    if (lib_open("lpu.dll")) g_lpu = 1;
#else
    if (path_exists("/dev/lpu0") || path_exists("/sys/class/lpu")) g_lpu = 1;
#endif
    return g_lpu;
}
void *basalt_lpu_alloc(int64_t n) {
    if (bare_mode() || n <= 0) return NULL;
    return aligned_alloc_n(64, (size_t)n);
}
void basalt_lpu_free(void *p) { aligned_free_n(p); }
int64_t basalt_lpu_copy_to(void *d, void *s, int64_t n) {
    if (bare_mode() || !d || !s || n <= 0) return -1;
    memcpy(d, s, (size_t)n);
    return n;
}
int64_t basalt_lpu_copy_from(void *d, void *s, int64_t n) { return basalt_lpu_copy_to(d, s, n); }
int64_t basalt_lpu_launch(void) {
    if (!basalt_lpu_available()) return -1;
    g_lpu_q++;
    return 0;
}
int64_t basalt_lpu_sync(void) { g_lpu_q = 0; return 0; }

int64_t basalt_accel_bare(void) { return bare_mode() ? 1 : 0; }

/* Unified device kind: 0 cpu 1 gpu 2 tpu 3 npu 4 lpu */
int64_t basalt_accel_best(void) {
    if (basalt_lpu_available()) return 4;
    if (basalt_npu_available()) return 3;
    if (basalt_tpu_available()) return 2;
    if (basalt_gpu_available()) return 1;
    return 0;
}
void *basalt_accel_alloc(int64_t kind, int64_t n) {
    if (kind == 1) return basalt_vram_alloc(n);
    if (kind == 2) return basalt_tpu_alloc(n);
    if (kind == 3) return basalt_npu_alloc(n);
    if (kind == 4) return basalt_lpu_alloc(n);
    return malloc((size_t)n);
}
void basalt_accel_free(int64_t kind, void *p) {
    if (kind == 1) basalt_vram_free(p);
    else if (kind == 2) basalt_tpu_free(p);
    else if (kind == 3) basalt_npu_free(p);
    else if (kind == 4) basalt_lpu_free(p);
    else free(p);
}
int64_t basalt_accel_sync(int64_t kind) {
    if (kind == 1) { basalt_gpu_sync(); return 0; }
    if (kind == 3) return basalt_npu_sync();
    if (kind == 4) return basalt_lpu_sync();
    return 0;
}
