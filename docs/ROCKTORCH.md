# Rocktorch

Librería oficial de tensores / entrenamiento de Basalt (`@import "Rocktorch"`, prefijo `rt.`).

## Dispositivos
| kind | device | pool tier |
|------|--------|-----------|
| 0 | CPU | ram |
| 1 | GPU | vram |
| 2 | TPU | tpu |
| 3 | NPU (AMD XDNA) | npu |
| 4 | LPU | lpu |

Auto-dispatch: **LPU → NPU → TPU → GPU → CPU**.

## API (resumen)
- Tensores: `rt.tensor`, `rt.tensor2d`, `rt.zeros`, `rt.ones`, RTLE1 header 80B
- Ops: `add/sub/mul/scale/axpy/dot/sum/max/clamp`
- Matmul tiled, `linear`, `conv1d`
- Activaciones: `relu`, `leaky_relu`, `sigmoid_approx`, `softmax_rows`
- Optimizers: `sgd`, `sgd_momentum`, `adam_step`
- Kernels `@gpu/@tpu/@npu/@lpu` + `*_auto`
- IO: checkpoint RTLE1 / secciones, weights, grads
- Loss: `mse_loss`, `grad_mse`

## Runtime
`runtime/accel.c`: probes Linux/Windows, buffers alineados, colas launch/sync, `accel_best/alloc/free/sync`.
