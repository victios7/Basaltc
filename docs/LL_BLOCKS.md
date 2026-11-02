# Bloques de bajo nivel

| Directiva | Aviso | Optimiza | Significado |
|-----------|-------|----------|-------------|
| `@basm[...]` | sí | **no** (NO_OPT) | BASM crudo |
| `@asm[...]` | sí | **no** | asm máquina |
| `@bir[...]` | sí | **sí** | BIR/SSA; se lowerea y **sí pasa por el optimizador** |
| `@asl[...]` | sí | **no** | **Assembler Super-Layer**: ops ABI/máquina expertas |
| `@_no_op[...]` | sí | **no** | fuerza NO_OPT sobre un fragmento |

## Aceleradores de función
`@cpu` `@gpu` `@tpu` `@npu` — emiten marcadores de kernel; tiers de pool 2=vram 4=tpu 5=npu.
