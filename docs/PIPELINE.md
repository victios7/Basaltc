# Pipeline / Pipeline

## English

```
.bd source
  → preprocess (@import, macros, @lang / @rule)
  → typecheck
  → codegen → BASM text
  → BIR (SSA) passes (optional by -O)
  → BPO / BAM / BAL / POW (optimize BASM)
  → write file.basm
  → basm (compiler/basm.bd) encode + link → ELF64
  → optional: blink (compiler/blink.bd) or pe_tool for PE32+
```

**Compiler sources (Basalt-only in `compiler/`):**

| File | Role |
|------|------|
| `basaltc.bd` | Frontend, IR, optimizers, driver |
| `basm.bd` | Assembler + link driver (BASM → object → executable) |
| `blink.bd` | Linker front-end (delegates to basm) |

Runtime object: `bin/basalt_rt.o` (and `/tmp/basalt_rt.o` during builds).

## Español

```
fuente .bd
  → preproceso (@import, macros, @lang / @rule)
  → typecheck
  → codegen → texto BASM
  → pases BIR (SSA) (según -O)
  → BPO / BAM / BAL / POW (optimiza BASM)
  → escribe file.basm
  → basm (compiler/basm.bd) codifica + enlaza → ELF64
  → opcional: blink (compiler/blink.bd) o pe_tool para PE32+
```

**Fuentes del compilador (solo Basalt en `compiler/`):**

| Archivo | Rol |
|---------|-----|
| `basaltc.bd` | Frontend, IR, optimizadores, driver |
| `basm.bd` | Ensamblador + enlace (BASM → objeto → ejecutable) |
| `blink.bd` | Front del linker (delega en basm) |

Objeto runtime: `bin/basalt_rt.o` (y `/tmp/basalt_rt.o` en builds).
