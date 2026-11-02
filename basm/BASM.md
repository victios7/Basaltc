# BASM — Basalt Assembly (v0.1)

Ensamblador **personalizado para Basalt**. No es GAS genérico: el modelo
refleja `box`/slots, frames fijos, tiers de memoria y el ABI que usa el lenguaje.

## Por qué existe

| Problema con GAS/x86 crudo | Beneficio BASM |
|----------------------------|----------------|
| Offsets `%rbp` opacos | **Slots** con nombre o índice = `box` |
| Sintaxis AT&T ruidosa | Mnemonics cortos, 1 insn / línea |
| Sin noción de frame | `.frame N` = `sub $N,%rsp` |
| Sin tiers | `.tier ram\|ssd\|vram` en datos |
| Labels crípticos | `.L` + bloques alineados a `if`/`while` |
| Link externo (gcc/ld) | **blink**: ELF64 (y PE vía pe_write) propio |

El compilador Basalt puede emitir BASM; `basm` lo baja a máquina + ejecutable.

## Modelo de ejecución

```
  ┌─────────────────────────────────────┐
  │  FRAME (bytes, alineado 16)         │
  │  slot0 @ -8(%rbp)                   │
  │  slot1 @ -16(%rbp)                  │
  │  ...                                │
  └─────────────────────────────────────┘
  Registros virtuales: r0..r7  (map a rax,rcx,rdx,rsi,rdi,r8,r9,r10)
  Args de llamada:     a0..a5  (rdi,rsi,rdx,rcx,r8,r9)  — SysV
  Retorno:             r0      (rax)
```

Slots = variables `box` del lenguaje. No hay `**`; un slot guarda un valor de 8 bytes
(o un ptr de 64 bits).

## Léxico

- Una instrucción por línea (como Basalt).
- Comentarios: `//` y `;` hasta fin de línea.
- Identificadores: `[a-zA-Z_][a-zA-Z0-9_]*`
- Inmediatos: decimal, `0x`, `0b`
- Strings: `"..."` con `\n \t \\ \"`

## Directivas

```
.func name              // empieza función (globl)
.frame N                // reserva N bytes de stack (default 256)
.end                    // fin de función

.slot name              // declara slot simbólico (auto offset)
// o numerados implícitos: s0, s1, ...

.str  lab "text"        // string en rodata → lab
.tier ram|ssd|vram      // marca tier del siguiente bloque de datos (v0.1: metadata)
.align N
```

## Instrucciones

### Movimiento y slots
```
li    rd, imm           // rd = imm
mov   rd, rs            // rd = rs
load  rd, slot           // rd = *slot   (slot local)
store slot, rs          // *slot = rs
loadg rd, global        // rd = global (ptr / symbol)
loadm rd, rs            // rd = *(u64*)rs  (systems ptr load)
storem rd, rs           // *(u64*)rd = rs  (systems ptr store)
```

### Aritmética (64-bit signed, wrap)
```
add   rd, rs            // rd += rs
sub   rd, rs
mul   rd, rs
div   rd, rs            // idiv; rdx:rax
neg   rd
```

### Comparación y saltos
```
cmp   ra, rb            // flags ← ra ? rb
cmpi  ra, imm
je    lab
jne   lab
jl    lab
jle   lab
jg    lab
jge   lab
jmp   lab
```

### Control
```
call  name              // call; args ya en a0..a5
ret                     // leave; ret  (usa frame)
reti  imm               // r0=imm; leave; ret
enter                   // push rbp; mov rsp,rbp; sub frame
// (enter se emite solo si no hubo .frame auto-prolog)
```

### Args / retorno (azúcar ABI)
```
arg   ai, rs            // ai = rs   (a0=rdi…)
argi  ai, imm
retv  rs                // r0 = rs; leave; ret
```

### Runtime Basalt (calls fijos)
```
print_int  rs           // ≡ arg a0,rs; call basalt_print_int
print_str  rs
panic      rs
```

### Labels
```
lab:
```

## Ejemplo (equiv. hello.bd)

```
.func main
.frame 64
.slot x
.slot y
.slot sum
    enter
    li    r0, 42
    store x, r0
    li    r0, 8
    store y, r0
    load  r0, x
    load  r1, y
    add   r0, r1
    store sum, r0
    load  r0, sum
    print_int r0
    reti  0
.end
```

## Mapeo desde Basalt

| Basalt              | BASM                          |
|----------------------|-------------------------------|
| `box x = 42 int32`   | `li r0,42` / `store x,r0`   |
| `x = y + z`          | `load r0,y` `load r1,z` `add r0,r1` `store x,r0` |
| `print_int(x)`       | `load r0,x` `print_int r0`  |
| `if a < b [..]`      | `load/cmp/jge .Lelse` …     |
| `while ..`           | label + cmp + jmp            |
| `@cpu f(a,b)`        | `.func f` + `load` de a0,a1 |
| `string "hi"`        | `.str S0 "hi"` + `loadg`    |
| `-into` / tier       | `.tier` + metadata slot      |

## Salida

```
basm input.basm -o out.elf     # ELF64 ejecutable
basm input.basm -o out.exe -W  # PE64 (via pe_write)
basm input.basm -S -o out.s    # dump GAS (debug)
```

## Extensiones futuras

- Registros virtuales ilimitados + linear scan
- `.tier vram` + ops GPU stub
- Relocs multi-objeto
- Peephole BASM→BASM (slot coalescing = free del optimizador Basalt)

## Integración con el compilador (backend activo)

Desde Stage 0, el codegen **emite BASM** (no GAS):

```
basaltc file.bd -o prog
  → /tmp/file.basm      (texto BASM)
  → basm → file.o       (ELF relocatable)
  → gcc -no-pie prog file.o basalt_rt.o
```

Flags útiles:
- `-S` / emit-asm: deja el `.basm`
- `-c` / emit-obj: deja el `.o`

El backend antiguo GAS queda en `src/codegen.c.gas.bak`.
