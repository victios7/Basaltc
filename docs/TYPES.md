# Types / Tipos

## English

| Syntax | Meaning |
|--------|---------|
| `i8`…`i64` / `intN` | signed; narrow stores mask |
| `u8`…`u64` | unsigned + mask |
| `int<N>` | arbitrary bit width (1…65536) |
| `f16` `f32` `f64` / `floatN` | floats |
| `isize` `usize` | pointer-sized |
| `bool` | i1 |
| `ptr` `*T` | 64-bit pointer |
| `string` `void` | string / void |

Registers are 64-bit; narrowing uses masks in codegen.

## Español

| Sintaxis | Significado |
|----------|-------------|
| `i8`…`i64` / `intN` | con signo; almacenes estrechos con máscara |
| `u8`…`u64` | sin signo + máscara |
| `int<N>` | ancho arbitrario (1…65536) |
| `f16` `f32` `f64` / `floatN` | flotantes |
| `isize` `usize` | tamaño puntero |
| `bool` | i1 |
| `ptr` `*T` | puntero 64-bit |
| `string` `void` | string / void |

Registros de 64 bits; el estrechamiento usa máscaras en codegen.
