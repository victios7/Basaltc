# Librerías Basalt (`@library` / `@import`)

## `@library "pref"`

En un archivo de librería declara el prefijo de API:

```basalt
@library "rt"
@cpu rt_tensor(...): ptr [ ... ]
```

En el usuario:

```basalt
@import "Rocktorch"
rt.tensor(10)    // rewrite → rt_tensor(10)
```

Sin `@library`, los nombres se usan tal cual.

## Imports

```basalt
@import "std"              // nativo
@import "Rocktorch"        // IA oficial
@import "file" / "file.bd" / "dir/file"
@import ("a" "b")          // varios
```

## Librería oficial de IA

**Rocktorch** — ver `docs/ROCKTORCH.md`.
