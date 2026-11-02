# Benchmarks / Benchmarks

## English

Fair rules:

- Do not use pure triangular `sum 1..N` when claiming loop speed (unless testing closed form).
- Prefer dependent kernels (LCG, poly hash, nested dependence).
- Report median of several runs; match stdout vs GCC.

Indicative (x86_64 Linux): CF sum 50M — Basalt `-O5` ≈ GCC/Clang `-O3` after slot CF; anti-CF LCG/hash ≈ GCC, Clang often faster; vs CPython often 50–90× on tight integer loops.

## Español

Reglas justas:

- No uses solo `sum 1..N` triangular para reclamar velocidad de bucle (salvo probar forma cerrada).
- Prefiere kernels dependientes (LCG, hash polinómico, nidos).
- Mediana de varias ejecuciones; misma salida que GCC.

Orientativo (x86_64 Linux): sum CF 50M — Basalt `-O5` ≈ GCC/Clang `-O3` con CF de slots; LCG/hash anti-CF ≈ GCC, Clang suele ganar; vs CPython a menudo 50–90× en bucles enteros densos.
