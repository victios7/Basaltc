# Basalt — Guía oficial completa

**Versión documentada:** 0.4.x  
**Ámbito:** lenguaje, pipeline, optimizadores, runtime, Rocktorch, dialectos `@lang`, empaquetado y ejemplos.  
**Objetivo:** ser la referencia única legible de punta a punta.

---

## 0. Qué es Basalt

Basalt (antes Bedrock en algunas ramas) es un lenguaje de sistemas **self-hosted** orientado a:

- Generar código **x86_64** (ELF64 Linux y PE32+ Windows).
- Un pipeline propio: fuente → preprocess → BIR (SSA) → BASM → BPO/BAL/BAM/POW → `basm` → binario.
- Optimización agresiva (incluyendo reducción a **forma cerrada** / álgebra cuando el patrón es detectable).
- Extensión por **dialectos** (`@lang`, `rust.bd`, `zig.bd`, …) y metaprogramación comptime.
- Librería de IA **Rocktorch** (`rt.*`) sobre CPU/GPU/TPU/NPU/LPU.

No es un frontend de LLVM por defecto: el backend nativo es BASM + linker propio (`basm` / `blink_link`).

---

## 1. Instalación y layout

### 1.1 Árbol típico

```
basalt/
  bin/basaltc          # compilador
  bin/basm             # ensamblador + link
  bin/basalt_rt.o      # runtime (stdio, arenas, accel)
  bin/pe_tool          # ayuda PE (opcional)
  compiler/basaltc.bd  # fuente del compilador
  runtime/             # C: runtime, arena, accel, gpu
  std/                 # std + rocktorch.bd
  languages/           # dialectos @lang
  docs/                # documentación
  examples/
  packaging/linux|windows/
```

### 1.2 Variables de entorno (sin rutas `/tmp` hardcodeadas)

| Variable | Uso |
|----------|-----|
| `BASALT_HOME` | Prefijo con `bin/` y `lib/` |
| `BASALT_ROOT` | Alias de home |
| `BASALT_RT` | Ruta exacta a `basalt_rt.o` |
| `BASALT_BARE` | `1` = sin aceleradores / bare-metal |
| `BASALT_GPU` / `BASALT_TPU` / `BASALT_NPU` / `BASALT_LPU` | Forzar detección |

```bash
export BASALT_HOME=/opt/basalt
export PATH="$BASALT_HOME/bin:$PATH"
basaltc hello.bd -O2 -o hello
./hello
```

### 1.3 Hello world

```basalt
@cpu main(): int32 [
    print_int(42)
    print_nl()
    return(0)
]
```

```bash
basaltc hello.bd -O2 -o hello
./hello   # → 42
```

Solo BASM (sin link):

```bash
basaltc hello.bd -O2 -S -o hello
# produce hello.basm
```

---

## 2. Sintaxis del lenguaje

### 2.1 Comentarios

```basalt
// línea
; también línea
/* bloque */
```

### 2.2 Tipos

Tipos base de almacenamiento:

- `int64` (alias frecuentes: `int`, `int32` → mismo storage en muchas rutas)
- `float64` (alias `float`)
- `string`, `ptr`
- **Zig-style:** `intN` / `uN` / `floatN` cuando el front los reconoce (`int8`, `u32`, …) con máscaras de bits en semántica de bajo nivel

Ejemplo:

```basalt
@cpu add(a int64, b int64): int64 [
    return(a + b)
]
```

### 2.3 Funciones y decoradores de dispositivo

```basalt
@cpu  foo(): int64 [ return(1) ]   // CPU
@gpu  bar(): int64 [ return(2) ]   // marca kernel GPU
@tpu  baz(): int64 [ return(3) ]
@npu  qux(): int64 [ return(4) ]
@lpu  zap(): int64 [ return(5) ]
```

En **bare-metal** (`-bare` o `BASALT_BARE=1`) el uso de aceleradores debe rechazarse.

### 2.4 Control de flujo

```basalt
@cpu main(): int32 [
    box x = 0 int64
    while x < 10 [
        if x - (x / 2) * 2 == 0 [
            print_int(x)
            print_nl()
        ]
        x = x + 1
    ]
    return(0)
]
```

No se exige `break`/`continue` estilo Python; el estilo de sistemas usa `while`/`if` anidados.

### 2.5 `box` y memoria

```basalt
box x = 1 int64          // binding local
box p = allocator(64) ptr
free(p)
```

**Pools / arenas (std):**

```basalt
@import "std"

@cpu main(): int32 [
    // pool "hola" 4096 bytes en CPU-regs tier (ejemplo de API de diseño)
    std.pool("hola", 4096)   // o pool_create("hola", 4096, tier)
    box y = 6 int64
    // -into(pool) asigna al heap de la pool cuando el desugar está activo
    free(y)
    return(0)
]
```

Tiers de memoria (diseño Rocktorch / std):

| Tier | Nombre | Uso |
|------|--------|-----|
| 0 | ram | heap normal |
| 1 | ssd | backing lento |
| 2 | vram | GPU |
| 3 | cpu | registros / hot |
| 4 | tpu | Coral Edge TPU |
| 5 | npu | AMD XDNA / DirectML |
| 6 | lpu | LPU |

### 2.6 Punteros

```basalt
box p = allocator(8) ptr
store_i64(p, 0, 42)
box v = load_i64(p, 0) int64
free(p)
```

### 2.7 Strings y builtins de runtime

- `print_int`, `print_str`, `print_nl`, `flush`
- `strlen`, `strcmp`, `substr`, `str_concat`, `itoa`
- `read_file`, `write_file`, `write_file_bin`, …
- `system`, `getenv`, `argc`, `argv`
- `scan_int`, `scan_float`, `scan_string`
- `str_phase_begin`, `str_phase_end`, `str_persist` (arenas de fase del compilador)

### 2.8 Importación

```basalt
@import "std"
@import "Rocktorch"
@import "ejemplo"
@import "ejemplo.bd"
@import "etc/ejemplo"
@import ("a" "b")   // multi-archivo
```

### 2.9 Librerías y prefijos

```basalt
@library "rt"
// las funciones se exportan / reescriben como rt.nombre → rt_nombre
```

Rocktorch se importa como `Rocktorch` y se usa `rt.tensor`, `rt.matmul_auto`, etc.

### 2.10 Macros y comptime (núcleo del lenguaje)

- Macros viven en el **lenguaje**, no como “basura” en `lib/`.
- Cabeceras `//!lang:zig` o `#lang rust` activan dialecto.
- `@comptime_error` / `@compile_error` fallan en compile-time.
- VM de preprocess (estilo Zig): evaluación comptime limitada + ownership interpretado en dialecto Rust embebido.

### 2.11 Bloques de bajo nivel (peligrosos)

| Directiva | Efecto |
|-----------|--------|
| `@basm` | Inyecta BASM; **aviso** de peligro |
| `@bir` | Inyecta BIR; **aviso**, pero **sí se optimiza** |
| `@asl` | Capa assembly-level auxiliar |
| `@_no_op` | Marca región no optimizable; **aviso** |

---

## 3. Pipeline de compilación

```
.bd source
   │
   ├─ read_file
   ├─ str_phase_begin()          // arena de fase (no GC)
   ├─ dialect / @lang expand
   ├─ @import resolve
   ├─ @library rewrite (prefijos)
   ├─ desugar (for, pools, …)
   ├─ typecheck
   ├─ codegen → BASM (y opcional BIR)
   ├─ optimizers según -O / flags
   │     BIR (SSA) → BPO → BAL → BAM → POW → bandit
   ├─ write .basm
   ├─ str_phase_end()            // libera arena entera
   └─ basm -e -o bin -r basalt_rt.o
```

### 3.1 Fases de memoria del compilador

No hay GC de roots. Hay **arenas por fase**:

1. `str_phase_begin` — slabs ~1MB, bump allocation para `str_concat`/`substr`/`itoa`.
2. Todo el trabajo intermedio vive en la arena.
3. Tras escribir `.basm`, `str_phase_end` **libera la arena completa**.
4. `str_persist` copia a `malloc` solo lo que deba sobrevivir (poco frecuente en el path normal).

### 3.2 Intermedios

| Flag | Salida |
|------|--------|
| `-E` | preprocess a stdout |
| `-S` / `-emit-basm` | solo `.basm` |
| `-c` | objeto `.o` |
| `-emit-bir` | dump BIR |
| `-save-temps` | conserva intermedios |

---

## 4. Flags del compilador

### 4.1 Salida y destino

```
-o FILE
-S / -emit-basm
-c
-E
-emit-bir
-save-temps
-l                 ELF64 (default)
-W                 PE32+ hint
-target elf64|pe64
-bare              freestanding (sin accel)
-march=x86-64
-JIT               optimiza, escribe en RAM/tmp, ejecuta y limpia
```

### 4.2 Optimización

```
-O0 … -O6
-bir-O0..4
-blink-O0..4
-fvectorize / -fno-vectorize
-funroll-loops / -fno-unroll-loops
-Falgebra          BAL whole-program
-Fbandit / -Fbayes bandit UCB (flag principal, nivel arsenal ~O5)
-fbranch-guard | -fbranch-soft | -fno-branch-guard | -Fallow-branch-opt
-fno-inline
-GO6               bestia + GPU link
```

### 4.3 Niveles (definición práctica)

| Nivel | Contenido típico |
|-------|------------------|
| **-O0** | Sin optimización / mínima |
| **-O1** | Peep + limpiezas ligeras |
| **-O2** | BPO fuerte, algo de unroll/vector |
| **-O3** | Casi todo BPO + más passes |
| **-O4** | CF / promote / arsenal amplio |
| **-O5** | Arsenal completo BPO+BAL+BAM+POW |
| **-O6** | Como -O5 + bump + BAL/BAM “beast” + más análisis matricial |
| **-Fbandit** | Orden de pases por bandit UCB; no depende de poner `-On` delante |

### 4.4 Diagnóstico

```
-v / --verbose
-q / --quiet
-Wall
-Werror
-ftime-report
-g
-h / --help
```

### 4.5 Dialectos de archivo completo

```
-Fzig -Frust -Fforge
-lang <name>
//!lang:zig   o   #lang rust   en cabecera
```

---

## 5. Optimizadores (BIR, BPO, BAL, BAM, POW)

### 5.1 Capas

| Motor | Nivel | Rol |
|-------|-------|-----|
| **BIR** | SSA / IR | Fold, VN, dedupe, graph, arsenal SSA |
| **BPO** | BASM peep | Unroll, dead code, promote loops, runtime-aware, SIMD hints, CF slots |
| **BAL** | Álgebra / e-graph | Fusión de bucles, DAG, pulsos, wave, SMT-lite, invariantes |
| **BAM** | Matrices / recurrencias | Affine 3×3, sumas, geom, tables, análisis profundo |
| **POW** | Pulsos / e-graph brutal | Saturación, cost model, SMT loop independence |
| **Bandit** | Meta | Ordena pases con UCB |

Sentinels anti-reentrada (no borran pases; evitan OOM por reentrada):

- `OPT:BAM-DONE`, `OPT:BPO-ARS`, `OPT:POW-STACK`, …

### 5.2 Catálogo de pases (muestra del arsenal ~119)

**BAL:** `bal_arsenal`, `bal_egraph`, `bal_fuse_loops`, `bal_dep_graph_close`, `bal_dag_solve`, `bal_nested_recurrence`, `bal_pulse`, `bal_wave`, `bal_smt`, `bal_const_prop_global`, `bal_invariant_hoist`, `bal_equation_fixpoint`, …

**BAM:** `bam_optimize`, `bam_optimize_aggressive`, `bam_affine3_apply`, `bam_try_general_affine`, `bam_sum_arith`, `bam_sum_sq`, `bam_geom`, `bam_table2_*`, `bam_table3_*`, `bam_analyze_deep`, …

**BPO:** `bpo_arsenal`, `bpo_peep_cpu`, `bpo_unroll_x4`, `bpo_unroll_reg_loop`, `bpo_closed_form_sum`, `bpo_promote_*_loop`, `bpo_runtime_aware`, `bpo_simd_sum_hint`, `bpo_gpu_opt`, …

**BIR:** `bir_optimize`, `bir_arsenal`, `bir_const_fold_add`, `bir_graph_vn`, `bir_to_basm`, …

**POW:** `pow_optimize`, `pow_optimize_brutal`, `pow_pulse`, `pow_wave`, `pow_egraph_saturate_basm`, `pow_smt_*`, …

La lista numerada completa está en `docs/PASSES_119.txt`.

### 5.3 Forma cerrada vs cómputo real

- **CF (closed form):** el optimizador reconoce bucles de suma/recurrencia y emite `li` con el resultado algebraico.
- **Cómputo real:** el bucle se ejecuta; igual puede haber unroll, vectorize, promote de registros, fusión, etc.
- **BAM/BAL/POW** no son “solo CF”: pueden resolver sistemas, e-graphs y grafos de dependencias; cuando no hay patrón seguro, **no inventan** resultado (branch guards).

### 5.4 Ejemplo mental CF

```basalt
// patrón suma 0..N-1 → N*(N-1)/2 si el pass lo ve
box s = 0 int64
box i = 0 int64
while i < N [
    s = s + i
    i = i + 1
]
```

Con `-O5`/`-O6`/`-Falgebra` el BASM puede colapsar a cargar la fórmula.

---

## 6. Runtime

### 6.1 Contenido de `basalt_rt.o`

- I/O, strings, files, system, getenv
- Arenas de fase del compilador
- `pool_create` / `pool_alloc` / `pool_free`
- Aceleradores (`accel.c`): GPU/VRAM, TPU Coral, NPU AMD, LPU
- GPU stubs / OpenCL probe (`gpu.c` según link)

### 6.2 Aceleradores

| API | Descripción |
|-----|-------------|
| `gpu_available` / `vram_alloc` / `vram_copy_*` / `gpu_sync` | GPU |
| `tpu_available` / `tpu_load_model` / `tpu_invoke` | un modelo Edge TPU |
| `npu_available` / `npu_launch` / `npu_sync` | AMD XDNA / DirectML |
| `lpu_available` / `lpu_launch` / `lpu_sync` | LPU |
| `accel_best` / `accel_alloc` / `accel_free` / `accel_sync` | API unificada |

Bare-metal: todo retorna “no disponible”.

---

## 7. Rocktorch

Librería oficial de tensores / entrenamiento (`std/rocktorch.bd`, `@library "rt"`).

### 7.1 Uso

```basalt
@import "Rocktorch"

@cpu main(): int32 [
    box a = rt.tensor(16) ptr
    box b = rt.tensor(16) ptr
    box c = rt.tensor(16) ptr
    rt.fill_auto(a, 1)
    rt.fill_auto(b, 2)
    rt.add_auto(c, a, b)
    print_int(rt.sum(c))
    print_nl()
    return(0)
]
```

### 7.2 Capacidades

- Tensores RTLE1 (header 80B), 1D/2D/3D, devices 0–4
- Ops elemento a elemento, matmul tiled, linear, conv1d
- Activaciones, SGD/momentum/Adam (entero)
- Kernels `@gpu/@tpu/@npu/@lpu` + `*_auto`
- Checkpoints por secciones, weights/grads
- MSE + grad

### 7.3 Dispatch

**LPU → NPU → TPU → GPU → CPU**, con `*_sync` cuando aplica.

---

## 8. Dialectos `@lang`

Archivos en `languages/`:

`zig.bd`, `rust.bd`, `python.bd`, `go.bd`, `c.bd`, `js.bd`, `ember.bd`, `quartz.bd`, `pebble.bd`, `forge.bd`, `obsidian.bd`, `mini.bd`, …

### 8.1 Activación

```basalt
@lang zig
@rule ...
```

O archivo completo:

```bash
basaltc prog.bd -Fzig -o prog
```

Cabecera:

```
//!lang:rust
```

### 8.2 Rust embebido

Ownership interpretado en comptime (modelo del dialecto): reglas de préstamo / move en la VM de preprocess, no en un borrow-checker LLVM.

### 8.3 Zig embebido

Estilo de tipos y control cercano a Zig; el preprocess actúa como VM de macros.

### 8.4 Obsidian (propio)

Dialecto postfix/stack orientado a experimentación (lenguaje “diferente” del resto).

---

## 9. BASM (visión de programador)

Fragmento típico generado:

```
.func main
.frame 128
.slot s8
...
enter
    li      r0, 42
    call    print_int
    call    print_nl
    li      r0, 0
    leave
    ret
```

`basm` ensambla y puede linkear (`-e -o out -r basalt_rt.o`).

---

## 10. Empaquetado

### 10.1 Linux

- `.deb` (`packaging/linux/build_deb.sh`): binarios, runtime, **docs**, examples, languages, std.
- `.zip` / tarball con `env.sh` que exporta `BASALT_HOME`.

### 10.2 Windows

- `build_native.ps1`: stage0 → basaltc.exe, timeout anti-cuelgue, payload MSI.
- Mismo modelo de `BASALT_HOME` / PATH.

GitHub Actions debe construir ambos; el job no debe hardcodear `/tmp` como única ruta de runtime.

---

## 11. Ejemplos por categoría

### 11.1 Aritmética y bucle

```basalt
@cpu sum_n(n int64): int64 [
    box s = 0 int64
    box i = 0 int64
    while i < n [
        s = s + i
        i = i + 1
    ]
    return(s)
]
@cpu main(): int32 [
    print_int(sum_n(100))
    print_nl()
    return(0)
]
```

### 11.2 Archivos

```basalt
@cpu main(): int32 [
    box t = read_file("a.txt") string
    write_file("b.txt", t)
    return(0)
]
```

### 11.3 Rocktorch matmul

```basalt
@import "Rocktorch"
@cpu main(): int32 [
    box A = rt.tensor(9) ptr
    box B = rt.tensor(9) ptr
    box C = rt.tensor(9) ptr
    rt.fill(A, 1)
    rt.fill(B, 1)
    rt.matmul_auto(C, A, B, 3, 3, 3)
    print_int(rt.sum(C))
    print_nl()
    return(0)
]
```

### 11.4 Flags de guerra

```bash
basaltc kern.bd -O6 -Falgebra -fvectorize -funroll-loops -o kern
basaltc kern.bd -Fbandit -o kern
basaltc kern.bd -GO6 -o kern_gpu
```

---

## 12. Errores del compilador (estilo)

Los diagnósticos deben ser profesionales:

- `error:` con archivo/contexto cuando esté disponible
- notas `basaltc: note:` (bare-metal, GPU skip, …)
- avisos en `@basm` / `@bir` / `@asl` / `@_no_op`
- `-Werror` eleva diagnósticos

---


## 13. Mapa de documentación satélite

| Archivo | Tema |
|---------|------|
| `LANGUAGE.md` | Sintaxis |
| `PIPELINE.md` | Pipeline |
| `OPTIMIZER.md` | Motores |
| `PASSES_119.txt` | Lista de pases |
| `ROCKTORCH.md` | IA |
| `MEMORY.md` | box/pool |
| `TYPES.md` | intN/floatN |
| `ACCEL.md` | GPU/TPU/NPU/LPU |
| `LL_BLOCKS.md` | @basm/@bir |
| `LANGUAGES.md` | Dialectos |
| **OFFICIAL_GUIDE.md** | Esta guía |

---

## 14. Glosario rápido

| Término | Significado |
|---------|-------------|
| BASM | Assembly intermedio Basalt |
| BIR | IR SSA |
| BPO | Peephole / opt sobre BASM |
| BAL | Algebra / e-graph / loops |
| BAM | Matrix / recurrence engine |
| POW | Pulse / wave / brutal e-graph |
| RTLE1 | Formato tensor/checkpoint little-endian |
| Bandit | Selector de orden de pases UCB |

---

*Fin de la guía oficial Basalt 0.4.x — mantener alineada con `compiler/basaltc.bd` y `std/rocktorch.bd`.*


---

## Anexo A — Lista completa de pases (PASSES_119)

```
  1. bal_advanced_cse_arith
  2. bal_arith_geo
  3. bal_arsenal
  4. bal_const_prop_global
  5. bal_dag_solve
  6. bal_dep_graph_close
  7. bal_dot_arith
  8. bal_egraph
  9. bal_egraph_add_rules
 10. bal_equation_fixpoint
 11. bal_fib_matrix
 12. bal_fuse_loops
 13. bal_geom_sum
 14. bal_has
 15. bal_invariant_hoist
 16. bal_keep
 17. bal_lcg_step
 18. bal_loop_markers
 19. bal_mat2_pow_apply
 20. bal_nested_recurrence
 21. bal_optimize
 22. bal_optimize_massive
 23. bal_poly_hash_cf
 24. bal_pow_stack
 25. bal_pulse
 26. bal_pulse_geom_train
 27. bal_pulse_lcg_close
 28. bal_pulse_pow
 29. bal_pulse_scan_A
 30. bal_pure_calls
 31. bal_rewrite_arith_identities
 32. bal_scan_li_after
 33. bal_smt
 34. bal_smt_gf2_xor_close
 35. bal_strip_ws_line
 36. bal_sum_cubes
 37. bal_sum_k_pow
 38. bal_table_close_asm
 39. bal_tensor
 40. bal_tensor_hint
 41. bal_try_lcg_close
 42. bal_try_poly_close
 43. bal_try_power_sums
 44. bal_wave
 45. bal_wave_front
 46. bal_wave_nest_close
 47. bam_affine3_apply
 48. bam_analyze_deep
 49. bam_count
 50. bam_geom
 51. bam_itoa_emit
 52. bam_mat3_mul_cell
 53. bam_mat3_pow_s0
 54. bam_nest
 55. bam_optimize
 56. bam_optimize_aggressive
 57. bam_pow_u
 58. bam_scan_has
 59. bam_sum_arith
 60. bam_sum_sq
 61. bam_table2_dep
 62. bam_table2_sum
 63. bam_table2_sum_ij
 64. bam_table3_dep
 65. bam_table3_sum
 66. bam_table3_wave
 67. bam_try_general_affine
 68. bandit_get_l
 69. bandit_get_w
 70. bandit_optimize
 71. bandit_run_arm
 72. bandit_ucb
 73. bir_algebra_graph
 74. bir_arsenal
 75. bir_const_fold_add
 76. bir_dedupe
 77. bir_graph_lookup
 78. bir_graph_put
 79. bir_graph_vn
 80. bir_optimize
 81. bir_to_basm
 82. bir_trim
 83. bpo_arsenal
 84. bpo_cf_arith_sum_slots
 85. bpo_cf_count_slots
 86. bpo_clean_arith
 87. bpo_closed_form_sum
 88. bpo_cmpi_loop
 89. bpo_dce_mov
 90. bpo_dead_li
 91. bpo_fold_inc1
 92. bpo_fold_memops
 93. bpo_gpu_opt
 94. bpo_graph_dce_labels
 95. bpo_math_matrix_mix
 96. bpo_peep_cpu
 97. bpo_promote_abc_loop
 98. bpo_promote_dep_loop
 99. bpo_promote_loop_regs
100. bpo_promote_mix_loop
101. bpo_rt_fold_strlen_const
102. bpo_runtime_aware
103. bpo_simd_sum_hint
104. bpo_strength_count
105. bpo_strength_mul_add
106. bpo_unroll_reg_loop
107. bpo_unroll_x4
108. pow_bir_pulse
109. pow_bir_wave
110. pow_cost
111. pow_eclass_get
112. pow_eclass_put
113. pow_egraph_saturate_basm
114. pow_optimize
115. pow_optimize_brutal
116. pow_pulse
117. pow_smt_apply
118. pow_smt_loop_independent
119. pow_wave

Total: 120

```

---

## Anexo B — Tabla de builtins runtime más usados

| Builtin | Aridad / notas |
|---------|----------------|
| print_int / print_str / print_nl | salida |
| strlen / strcmp / substr / str_concat / itoa | strings (arena de fase en compilador) |
| read_file / write_file / write_file_bin | IO |
| allocator / free / memset_n / memcpy_n | heap |
| store_i64 / load_i64 / store_u8 / load_u8 | memoria |
| pool_create / pool_alloc / pool_free | arenas nombradas |
| gpu_available / vram_alloc / vram_free / vram_copy_to / vram_copy_from / gpu_sync | GPU |
| tpu_* / npu_* / lpu_* | aceleradores |
| str_phase_begin / str_phase_end / str_persist | memoria del compilador |
| getenv / system / argc / argv | entorno |

---

## Anexo C — Ejemplo de programa de menú (stdio)

```basalt
@cpu triangulo(): int32 [
    print_str("Base: ")
    box base = scan_float() float64
    print_str("Altura: ")
    box altura = scan_float() float64
    // área en dominio float del runtime
    print_str("Area calculada\n")
    return(0)
]

@cpu main(): int32 [
    print_str("1. Triangulo 2. Trapecio 3. Divisas: ")
    box op = scan_int() int64
    if op == 1 [ triangulo() ]
    if op == 2 [ print_str("trapecio\n") ]
    if op == 3 [ print_str("conversion\n") ]
    return(0)
]
```

---

## Anexo D — Flujo JIT

```bash
basaltc app.bd -JIT
# equivale a: opts altas, salida temporal, ejecutar, borrar artefacto
```

Internamente: `-O5` implícito razonable, `out_path` tipo `basalt_jit_bin`, `basm -e`, `chmod +x`, `system(run)`, cleanup.

---

## Anexo E — Windows vs Linux

| Tema | Linux | Windows |
|------|-------|---------|
| Objeto final | ELF64 | PE32+ (`-W` / pe_tool) |
| Runtime | `basalt_rt.o` | mismo + `#ifdef _WIN32` en accel |
| Probe GPU | `/dev/dri`, libOpenCL | OpenCL.dll |
| Probe NPU | amdxdna sysfs | DirectML.dll |
| Probe TPU | `/dev/apex_*`, libedgetpu | edgetpu.dll |
| Bare | `-bare` / `BASALT_BARE` | igual |

---

## Anexo F — Cómo leer el costo de un pass

1. Compilar con `-ftime-report -v`.
2. Mirar tamaño `.basm` pre/post (`-save-temps`).
3. Para CF: buscar desaparición de `jl`/`loop` y aparición de `li` constantes.
4. Bandit: `-Fbandit` reordena; no garantiza el mismo orden dos veces si el estado UCB cambia.

---

*Anexos alineados con el árbol 0.4.x.*
