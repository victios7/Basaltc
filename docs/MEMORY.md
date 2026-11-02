# Modelo de memoria Basalt

```basalt
@import "std"
box x = 1 int64
free(x)

std.pool("hola", 64)-cpu     // -ram | -ssd | -vram | -cpu
std.pool("hola", 1024)-vram
box y = 6 int8 -into(hola)
free(hola)

box z = 5 float64 -into(x)   // z toma storage de x
box p = std.allocator(32) ptr
```

Tiers: `0` ram · `1` ssd · `2` vram · `3` cpu.

**IA / tensores:** Rocktorch usa tier **vram** y `vram_alloc` — ver `docs/ROCKTORCH.md`.
