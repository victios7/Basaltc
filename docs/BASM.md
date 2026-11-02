# BASM + linker driver

## English

**Sources:** `compiler/basm.bd`, `compiler/blink.bd` (Basalt).

### Linker preference (no gcc)

1. `mold` / `ld.mold`
2. `ld.lld` / `lld`
3. `ld.gold` / `gold`
4. `ld`

`basm --show-linker` prints the one that will be used.

### Flow

1. BASM text → GNU `as --64` → `.o` (binutils assembler only)
2. Best linker links `.o` + `basalt_rt.o` → ELF64 (`-nostdlib -static -e _start`)

`blink` is a thin CLI over `basm -e`.

### Rebuild

```bash
basaltc compiler/basm.bd -O2 -o bin/basm
basaltc compiler/blink.bd -O2 -o bin/blink
```

## Español

**Fuentes:** `compiler/basm.bd`, `compiler/blink.bd` (Basalt).

### Preferencia de linker (sin gcc)

1. `mold` / `ld.mold`
2. `ld.lld` / `lld`
3. `ld.gold` / `gold`
4. `ld`

`basm --show-linker` muestra el elegido.

### Flujo

1. Texto BASM → GNU `as --64` → `.o`
2. Mejor linker enlaza `.o` + `basalt_rt.o` → ELF64

`blink` es un CLI fino sobre `basm -e`.

### Rebuild

```bash
basaltc compiler/basm.bd -O2 -o bin/basm
basaltc compiler/blink.bd -O2 -o bin/blink
```
