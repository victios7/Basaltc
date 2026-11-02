# Flags / Flags

## English

| Flag | Description |
|------|-------------|
| `-O0` … `-O6` | Optimization level (`-O5` recommended) |
| `-Ofast` | High optimization + algebra |
| `-Falgebra` / `-FBAL` | BAL + BAM + POW stack |
| `-Fbayes` / `-Fbandit` | Bandit pass ordering |
| `-S` | Emit `.basm` only |
| `-E` | Preprocess only |
| `-o path` | Output path |
| `-W` | Windows PE32+ (via pe_tool when available) |
| `-bare` | Freestanding / kernel-oriented |
| `-q` / `-v` | Quiet / verbose |

**basm:** `-e` encode+link, `-o out`, `-r runtime.o`, `-W`, `-bare`  
**blink:** `-o out`, `-r runtime.o`, `-W`, `-bare`, `-v`

## Español

| Flag | Descripción |
|------|-------------|
| `-O0` … `-O6` | Nivel de optimización (`-O5` recomendado) |
| `-Ofast` | Alta optimización + álgebra |
| `-Falgebra` / `-FBAL` | Pila BAL + BAM + POW |
| `-Fbayes` / `-Fbandit` | Orden de pases tipo bandit |
| `-S` | Solo emitir `.basm` |
| `-E` | Solo preproceso |
| `-o path` | Ruta de salida |
| `-W` | Windows PE32+ (con pe_tool si existe) |
| `-bare` | Freestanding / kernel |
| `-q` / `-v` | Silencioso / verboso |

**basm:** `-e` codificar+enlazar, `-o out`, `-r runtime.o`, `-W`, `-bare`  
**blink:** `-o out`, `-r runtime.o`, `-W`, `-bare`, `-v`
