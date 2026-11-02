# Basalt

Self-hosted systems language (x86_64). Production toolchain: `basaltc`, `basm`, `basalt_rt.o`.

## Install from source tree

```bash
export BASALT_HOME=$PWD
export PATH="$PWD/bin:$PATH"
basaltc examples/hello.bd -O2 -o hello && ./hello
```

## Rebuild compiler (self-host)

```bash
make bootstrap   # stage0 → C → link with runtime → bin/basaltc
```

## Memory model

```basalt
box *x = 7 int64          // typed cell, OWN+LIVE
box p = allocator(n) ptr  // heap block
*x = 8                    // store
print_int(*x)             // load
free(p)                   // once; double-free is a compile error
```

Typecheck runs before pointer desugar: double-free, UAF, free non-ptr, undefined calls.

## Output

```basalt
println!("hola", 1, 2)
print!(42)
```

## Libraries

- `hl` — ergonomics only
- `Rocktorch` — tensors / training (`rt.`)

## Flags

`-O0`…`-O6`, `-Fbandit`, `-Falgebra`, `-JIT`, `-bare`, `-W`, `-S`, `-c`

## CI

`.github/workflows/ci.yml` + `release.yml` → Linux `.deb`/zip and Windows build.
