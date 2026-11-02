/* Basalt memory tiers:
 * 0 ram | 1 ssd | 2 vram(gpu) | 3 cpu | 4 tpu | 5 npu | 6 lpu
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_POOLS 64
#define POOL_NAME 64

typedef enum {
    TIER_RAM = 0,
    TIER_SSD = 1,
    TIER_VRAM = 2,
    TIER_CPU = 3,
    TIER_TPU = 4,
    TIER_NPU = 5,
    TIER_LPU = 6
} Tier;

typedef struct {
    char name[POOL_NAME];
    Tier tier;
    uint8_t *base;
    int64_t size;
    int64_t used;
    int alive;
} Pool;

static Pool g_pools[MAX_POOLS];
static int g_npools;

static void *ssd_map(const char *name, int64_t size) {
    char path[256];
    snprintf(path, sizeof path, "/tmp/basalt_ssd_%s.bin", name);
    FILE *f = fopen(path, "r+b");
    if (!f) {
        f = fopen(path, "w+b");
        if (!f) return malloc((size_t)size);
        fseek(f, (long)size - 1, SEEK_SET);
        fputc(0, f);
        fflush(f);
    }
    void *p = malloc((size_t)size);
    if (!p) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    (void)fread(p, 1, (size_t)size, f);
    fclose(f);
    return p;
}

static void ssd_sync(const char *name, void *p, int64_t size) {
    char path[256];
    snprintf(path, sizeof path, "/tmp/basalt_ssd_%s.bin", name);
    FILE *f = fopen(path, "w+b");
    if (!f) return;
    fwrite(p, 1, (size_t)size, f);
    fclose(f);
}

extern void *basalt_vram_alloc(int64_t n);
extern void basalt_vram_free(void *p);
extern void *basalt_tpu_alloc(int64_t n);
extern void basalt_tpu_free(void *p);
extern void *basalt_npu_alloc(int64_t n);
extern void basalt_npu_free(void *p);
extern void *basalt_lpu_alloc(int64_t n);
extern void basalt_lpu_free(void *p);

static Pool *pool_find(const char *name) {
    for (int i = 0; i < g_npools; i++)
        if (g_pools[i].alive && strcmp(g_pools[i].name, name) == 0)
            return &g_pools[i];
    return NULL;
}

int64_t basalt_pool_create(const char *name, int64_t bytes, int64_t tier) {
    if (!name || bytes <= 0 || g_npools >= MAX_POOLS) return -1;
    if (pool_find(name)) return -2;
    Pool *p = &g_pools[g_npools++];
    memset(p, 0, sizeof(*p));
    snprintf(p->name, sizeof p->name, "%s", name);
    p->tier = (Tier)tier;
    p->size = bytes;
    p->used = 0;
    p->alive = 1;
    if (tier == TIER_SSD)
        p->base = (uint8_t *)ssd_map(name, bytes);
    else if (tier == TIER_VRAM)
        p->base = (uint8_t *)basalt_vram_alloc(bytes);
    else if (tier == TIER_TPU)
        p->base = (uint8_t *)basalt_tpu_alloc(bytes);
    else if (tier == TIER_NPU)
        p->base = (uint8_t *)basalt_npu_alloc(bytes);
    else if (tier == TIER_LPU)
        p->base = (uint8_t *)basalt_lpu_alloc(bytes);
    else
        p->base = (uint8_t *)malloc((size_t)bytes);
    if (!p->base) { p->alive = 0; g_npools--; return -3; }
    memset(p->base, 0, (size_t)bytes);
    if (tier == TIER_SSD) ssd_sync(p->name, p->base, bytes);
    return 0;
}

int64_t basalt_pool_alloc(const char *name, int64_t nbytes) {
    Pool *p = pool_find(name);
    if (!p || nbytes <= 0) return -1;
    int64_t align = (nbytes + 7) & ~7;
    if (p->used + align > p->size) return -1;
    int64_t off = p->used;
    p->used += align;
    return off;
}

void *basalt_pool_ptr(const char *name, int64_t off) {
    Pool *p = pool_find(name);
    if (!p || off < 0 || off >= p->size) return NULL;
    return p->base + off;
}

int64_t basalt_pool_free(const char *name) {
    Pool *p = pool_find(name);
    if (!p) return -1;
    if (p->tier == TIER_SSD)
        ssd_sync(p->name, p->base, p->size);
    if (p->tier == TIER_VRAM)
        basalt_vram_free(p->base);
    else if (p->tier == TIER_TPU)
        basalt_tpu_free(p->base);
    else if (p->tier == TIER_NPU)
        basalt_npu_free(p->base);
    else if (p->tier == TIER_LPU)
        basalt_lpu_free(p->base);
    else
        free(p->base);
    p->base = NULL;
    p->alive = 0;
    p->used = 0;
    return 0;
}

int64_t basalt_pool_used(const char *name) {
    Pool *p = pool_find(name);
    return p ? p->used : -1;
}
int64_t basalt_pool_size(const char *name) {
    Pool *p = pool_find(name);
    return p ? p->size : -1;
}
