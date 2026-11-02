# Embedded languages / Lenguajes embebidos

## English

Metaprogramming only (no second compiler binary):

```basalt
@lang name
@rule name "pattern" => "template"
@prelude name "box __x = 0 int64"
```

Captures: `$name`, `$val`, `$cond`, `$rest`. Blocks: `@name[ ... ]`.

Shipped dialects under `languages/`: zig, rust, forge, mini, c, go, js, python, ember, obsidian, pebble, quartz.

## Español

Solo metaprogramación (no hay segundo binario de frontend):

```basalt
@lang name
@rule name "pattern" => "template"
@prelude name "box __x = 0 int64"
```

Capturas: `$name`, `$val`, `$cond`, `$rest`. Bloques: `@name[ ... ]`.

Dialectos en `languages/`: zig, rust, forge, mini, c, go, js, python, ember, obsidian, pebble, quartz.
