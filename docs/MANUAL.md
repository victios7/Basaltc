# Compiler manual / Manual del compilador

## English

### Build tools

```bash
export PATH="$PWD/bin:$PATH"
cp bin/basalt_rt.o /tmp/basalt_rt.o
basaltc examples/hello.bd -O5 -o hello && ./hello
```

### Rebuild compiler / tools

```bash
make bootstrap   # stage0 C seed → basaltc
make basm        # compiler/basm.bd → bin/basm
make blink       # compiler/blink.bd → bin/blink
make selftest
```

### Inputs / outputs

- Input: `.bd`  
- Intermediate: `.basm`  
- Output: ELF64 executable (PE with `-W` + pe_tool)

### Diagnostics

Use `-v` for verbose pipeline logs. Type errors print counts and stop codegen.

## Español

### Construir y usar

```bash
export PATH="$PWD/bin:$PATH"
cp bin/basalt_rt.o /tmp/basalt_rt.o
basaltc examples/hello.bd -O5 -o hello && ./hello
```

### Reconstruir compilador / herramientas

```bash
make bootstrap   # seed C stage0 → basaltc
make basm        # compiler/basm.bd → bin/basm
make blink       # compiler/blink.bd → bin/blink
make selftest
```

### Entradas / salidas

- Entrada: `.bd`  
- Intermedio: `.basm`  
- Salida: ejecutable ELF64 (PE con `-W` + pe_tool)

### Diagnósticos

`-v` activa logs del pipeline. Errores de tipos detienen el codegen.
