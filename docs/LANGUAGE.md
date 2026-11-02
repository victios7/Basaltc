# Language / Lenguaje

## English

Basalt is a systems language compiling to x86_64.

**Functions:** `@cpu name(args): ret [ body ]`  
**Locals:** `box x = expr type`  
**Control:** `if cond [ ... ]`, `while cond [ ... ]`  
**I/O:** `print_int`, `print_str`, `print_nl`, `print!(...)`  
**Low-level blocks:** `@asm[]`, `@basm[]`, `@bir[]`, `@_no_op[]`  
**Metaprogramming:** `@macro`, `@lang` / `@rule` / `@prelude`  

Types: see TYPES.md. Pipeline: see PIPELINE.md.

Minimal example:

```basalt
@cpu main(): int32 [
    box n = 10 int64
    box s = 0 int64
    box i = 1 int64
    while i <= n [
        s = s + i
        i = i + 1
    ]
    print_int(s)
    print_nl()
    return(0)
]
```

## Español

Basalt es un lenguaje de sistemas que compila a x86_64.

**Funciones:** `@cpu name(args): ret [ body ]`  
**Locales:** `box x = expr type`  
**Control:** `if cond [ ... ]`, `while cond [ ... ]`  
**I/O:** `print_int`, `print_str`, `print_nl`, `print!(...)`  
**Bajo nivel:** `@asm[]`, `@basm[]`, `@bir[]`, `@_no_op[]`  
**Metaprogramación:** `@macro`, `@lang` / `@rule` / `@prelude`  

Tipos: TYPES.md. Pipeline: PIPELINE.md.

Ejemplo mínimo: igual que arriba (`sum 1..10`).
