# Aceleradores — Linux / Windows / bare

## Hosted (Linux y Windows)

| Device | Linux | Windows |
|--------|-------|---------|
| GPU | `/dev/dri`, OpenCL | `OpenCL.dll` |
| NPU AMD | `amdxdna`, `/dev/accel`, XRT | DirectML / `BASALT_NPU=1` |
| TPU Coral | `/dev/apex_*`, `libedgetpu` | `edgetpu.dll` / `BASALT_TPU=1` |
| LPU | `/dev/lpu0`, `BASALT_LPU=1` | `lpu.dll` / `BASALT_LPU=1` |

Runtime: `runtime/accel.c` (`#ifdef _WIN32`).  
`BASALT_BARE=1` o `-DBASALT_BARE_METAL` → todo `*_available` = 0.

## Bare-metal (`basaltc … -bare`)

**No soportado.** El compilador aborta si el código usa:

- `@gpu` / `@tpu` / `@npu` / `@lpu`
- `vram_*`, `tpu_*`, `npu_*`, `lpu_*` availability APIs

Mensaje:

```
basaltc: error: accelerators (GPU/TPU/NPU/LPU/VRAM) are not available on bare-metal (-bare)
basaltc: note: use a hosted Linux or Windows target with the OS runtime
```

Pools `-vram|-tpu|-npu|-lpu` requieren runtime hosted; en bare no hay driver de dispositivo.
