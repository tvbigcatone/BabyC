# Delta — pthread GIL trampoline + Function→void* assign

**Data:** 2026-10-15  
**Baseline:** `attachments/babyc.zip`  
**Zip:** `artifacts/babyc_pthread_trampoline.zip`

## Ideologia

- Core educational: diagnostica resta attiva sul codice guest eseguito nel trampolino.
- Thin layer BabyPOSIX: su Linux wrapper nativo; su Windows CS/CV + thread OS.
- Specularità con wthreads: create+join ora disponibili (sync già allo stesso livello).
- `win.h` assente; solo `windows.h`.

## Cosa sblocca

1. **Trampolino GIL** in `posix_pthread.c`: thread OS → lock interprete →
   `BabyPosixCallGuest` (IR body o token body) → unlock.
2. **`ExpressionAssignToPointer`**: designatore funzione → `void*` passa il
   `Value*` persistente (prima il branch `void*` copiava un Pointer vuoto).
3. **`ir_call`**: packing TypeFunction prima del formal `void*`.
4. **`ir_lower`**: call con designatore funzione resta su tree residual
   (pack lineare non porta Value*).

## Test

| Test | Esito |
|---|---|
| `pthread_create_join` (default) | ok |
| `pthread_create_join` `BABYC_NO_IR=1` | ok |
| `pthread_create` null start | ok (EINVAL/ENOSYS) |
| mutex / cond | ok |

Esempio student-friendly:

```c
void *start = worker;
pthread_create(&th, 0, start, &cell);
pthread_join(th, 0);
```

## File nel zip

posix_pthread.*, include.c, Makefile, expression.c, ir_call.c, ir_lower.c,
win.c (banner), test pthread_*, docs architecture + delta notes.
