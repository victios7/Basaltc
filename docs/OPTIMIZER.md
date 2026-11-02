# Optimizer / Optimizador

## English

Layers applied to BASM (and BIR when enabled):

1. **BPO** — peephole, loop promote, strength reduction, closed-form sums (`bpo_cf_arith_sum_slots`)
2. **BAM** — affine / matrix power \(O(\log n)\), geometric sums, nests
3. **BAL** — pulse, wave, e-graph saturation, SMT-lite, 2D/3D tables
4. **POW** — orchestrates pulse → wave → e-graph → SMT → arsenal

At `-O5+`, pure `sum 1..N` shaped loops can become \(N(N+1)/2\) with no `jl`.

## Español

Capas sobre BASM (y BIR si aplica):

1. **BPO** — peephole, promote de bucles, strength reduction, sumas cerradas
2. **BAM** — afín / potencia de matrices \(O(\log n)\), geométricas, nidos
3. **BAL** — pulso, onda, saturación e-graph, SMT-lite, tablas 2D/3D
4. **POW** — orquesta pulso → onda → e-graph → SMT → arsenal

En `-O5+`, bucles tipo `sum 1..N` pueden pasar a \(N(N+1)/2` sin `jl`.
