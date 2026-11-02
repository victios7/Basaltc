# GPU

## English

Runtime stubs and OpenCL-oriented helpers live under `runtime/gpu.c`. Rocktorch uses `vram_alloc`, `@gpu` kernels, and host fallbacks. Optimizer respects GPU markers (`NO_OPT` / kernel regions) where present.

## Español

Stubs y helpers orientados a OpenCL en `runtime/gpu.c`. Rocktorch usa `vram_alloc`, kernels `@gpu` y fallback host. El optimizador respeta marcadores GPU (`NO_OPT` / regiones kernel) cuando existen.
