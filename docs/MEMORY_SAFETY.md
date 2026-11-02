# Memory & type analysis (Basaltc)

Runs **before** pointer desugar on high-level `*` / `&` source.

## Pointer model

```basalt
box *x = 7 int64          // OWN + LIVE, cell size 8
box y = allocator(64) ptr // OWN + LIVE, heap block
*x                        // requires LIVE, type ptr
*x = 8
x = 0                     // NULLISH
x = z                     // BORROW alias (warn)
free(x)                   // OWN only; once
```

## States

| State | Meaning |
|-------|---------|
| LIVE | valid heap/cell |
| FREED | after free — UAF is **error** |
| NULLISH | null-ish — deref **error** |
| ESCAPED | returned ownership |
| BORROW | alias — free is **error** |

## Errors (hard)

- double free, free non-ptr / undeclared / borrow / null
- use-after-free deref
- null deref, deref non-ptr / undeclared
- `**`
- undefined function
- type clash ptr vs non-ptr assignment
- dangling return

## Warnings

- reassign LIVE without free (leak)
- pointer alias / borrow
- free unknown

## Continuous / production

- Prefer `box *` and `allocator` + matching `free`
- One owner; do not free borrows
- `-Werror` elevates diagnostics when enabled in driver
