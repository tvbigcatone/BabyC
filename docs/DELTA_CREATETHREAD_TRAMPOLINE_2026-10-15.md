# Delta — CreateThread + shared GIL trampoline

**Data:** 2026-10-15  
**Zip:** `artifacts/babyc_createthread_trampoline.zip`  
**Baseline:** `attachments/babyc.zip`

## Ideologia

- Diagnostica educational resta sotto un solo interprete (GIL).
- Layer **speculari**: `pthread_create` e `CreateThread` usano lo stesso trampolino.
- Host nativo = wrapper; non-nativo = implementazione layer (libWINAPI / BabyPOSIX).
- Nessun `win.h`; solo `#include <windows.h>`.

## Cosa c’è

| Pezzo | Ruolo |
|---|---|
| `platform/guest_thread.c` | GIL + `BabycGuestCall` / `ResolveFuncValue` |
| `posix_pthread.c` | usa il modulo condiviso |
| `cstdlib_ext/win.c` | registra `CreateThread` → OS thread + trampolino guest |
| test `winapi_createthread.c` | create + WaitForSingleObject + flag/cell |

## Test

- `tests/windows/winapi_createthread.c` → ok  
- `tests/posix/pthread_create_join.c` → ok  
- mutex / cond → ok  

## Esempio

```c
#include <windows.h>
DWORD WINAPI worker(LPVOID arg) { ...; return 0; }
void *start = worker;
HANDLE h = CreateThread(0, 0, start, &cell, 0, &tid);
WaitForSingleObject(h, 5000);
```
