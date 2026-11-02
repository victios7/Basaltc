/* Basalt GPU runtime — OpenCL on Linux/Windows when BASALT_USE_OPENCL; else host */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#define BASALT_WIN 1
#else
#define BASALT_WIN 0
#include <pthread.h>
#endif

#ifdef BASALT_USE_OPENCL
#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif
#endif

typedef struct {
    char name[64];
    void *fn;
} GpuKernel;

static GpuKernel g_kernels[256];
static int g_nk;
static int g_gpu_ok = -1;

#ifdef BASALT_USE_OPENCL
static cl_context g_ctx;
static cl_command_queue g_q;
static cl_device_id g_dev;
static cl_program g_prog;
#endif

int64_t basalt_gpu_available(void) {
    if (g_gpu_ok >= 0) return g_gpu_ok;
    g_gpu_ok = 0;
#ifdef BASALT_USE_OPENCL
    cl_uint nplat = 0;
    if (clGetPlatformIDs(0, NULL, &nplat) != CL_SUCCESS || nplat == 0)
        return g_gpu_ok;
    cl_platform_id plat;
    clGetPlatformIDs(1, &plat, NULL);
    cl_uint ndev = 0;
    if (clGetDeviceIDs(plat, CL_DEVICE_TYPE_GPU, 0, NULL, &ndev) != CL_SUCCESS || ndev == 0) {
        /* try any device (iGPU / CPU device) */
        if (clGetDeviceIDs(plat, CL_DEVICE_TYPE_ALL, 0, NULL, &ndev) != CL_SUCCESS || ndev == 0)
            return g_gpu_ok;
        clGetDeviceIDs(plat, CL_DEVICE_TYPE_ALL, 1, &g_dev, NULL);
    } else {
        clGetDeviceIDs(plat, CL_DEVICE_TYPE_GPU, 1, &g_dev, NULL);
    }
    cl_int err = 0;
    g_ctx = clCreateContext(NULL, 1, &g_dev, NULL, NULL, &err);
    if (err != CL_SUCCESS) return g_gpu_ok;
#if defined(CL_VERSION_2_0)
    g_q = clCreateCommandQueueWithProperties(g_ctx, g_dev, NULL, &err);
#else
    g_q = clCreateCommandQueue(g_ctx, g_dev, 0, &err);
#endif
    if (err == CL_SUCCESS) g_gpu_ok = 1;
#endif
    return g_gpu_ok;
}

void *basalt_vram_alloc(int64_t n) {
    if (n <= 0) return NULL;
#ifdef BASALT_USE_OPENCL
    if (basalt_gpu_available()) {
        cl_int err = 0;
        cl_mem m = clCreateBuffer(g_ctx, CL_MEM_READ_WRITE, (size_t)n, NULL, &err);
        if (err == CL_SUCCESS) return (void *)(uintptr_t)m;
    }
#endif
    return malloc((size_t)n);
}

void basalt_vram_free(void *p) {
    if (!p) return;
#ifdef BASALT_USE_OPENCL
    if (basalt_gpu_available()) {
        clReleaseMemObject((cl_mem)(uintptr_t)p);
        return;
    }
#endif
    free(p);
}

int64_t basalt_vram_copy_to(void *dst, const void *src, int64_t n) {
    if (!dst || !src || n <= 0) return -1;
#ifdef BASALT_USE_OPENCL
    if (basalt_gpu_available()) {
        cl_int err = clEnqueueWriteBuffer(g_q, (cl_mem)(uintptr_t)dst, CL_TRUE, 0, (size_t)n, src, 0, NULL, NULL);
        return err == CL_SUCCESS ? n : -1;
    }
#endif
    memcpy(dst, src, (size_t)n);
    return n;
}

int64_t basalt_vram_copy_from(void *dst, const void *src, int64_t n) {
    if (!dst || !src || n <= 0) return -1;
#ifdef BASALT_USE_OPENCL
    if (basalt_gpu_available()) {
        cl_int err = clEnqueueReadBuffer(g_q, (cl_mem)(uintptr_t)src, CL_TRUE, 0, (size_t)n, dst, 0, NULL, NULL);
        return err == CL_SUCCESS ? n : -1;
    }
#endif
    memcpy(dst, src, (size_t)n);
    return n;
}

int64_t basalt_gpu_register(const char *name, void *fn) {
    if (!name || g_nk >= 256) return -1;
    snprintf(g_kernels[g_nk].name, sizeof g_kernels[g_nk].name, "%s", name);
    g_kernels[g_nk].fn = fn;
    g_nk++;
    return 0;
}

/* Optional: compile OpenCL C source as named program (real device path) */
int64_t basalt_gpu_build(const char *src, const char *options) {
#ifdef BASALT_USE_OPENCL
    if (!basalt_gpu_available() || !src) return -1;
    cl_int err = 0;
    size_t len = strlen(src);
    g_prog = clCreateProgramWithSource(g_ctx, 1, &src, &len, &err);
    if (err != CL_SUCCESS) return -1;
    err = clBuildProgram(g_prog, 1, &g_dev, options ? options : "", NULL, NULL);
    return err == CL_SUCCESS ? 0 : -1;
#else
    (void)src; (void)options;
    return -1;
#endif
}

int64_t basalt_gpu_launch(const char *name, void *args) {
#ifdef BASALT_USE_OPENCL
    if (basalt_gpu_available() && g_prog && name) {
        cl_int err = 0;
        cl_kernel k = clCreateKernel(g_prog, name, &err);
        if (err == CL_SUCCESS) {
            /* args: optional cl_mem list not fully typed here — host callback still primary */
            clReleaseKernel(k);
        }
    }
#endif
    for (int i = 0; i < g_nk; i++) {
        if (strcmp(g_kernels[i].name, name) == 0 && g_kernels[i].fn) {
            typedef int64_t (*K)(void *);
            return ((K)g_kernels[i].fn)(args);
        }
    }
    fprintf(stderr, "basalt_gpu_launch: kernel '%s' not registered\n", name ? name : "?");
    return -1;
}

int64_t basalt_gpu_sync(void) {
#ifdef BASALT_USE_OPENCL
    if (basalt_gpu_available())
        return clFinish(g_q) == CL_SUCCESS ? 0 : -1;
#endif
    return 0;
}

