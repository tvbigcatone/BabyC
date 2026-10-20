# Delta — complete BabyC ↔ libWINAPI / BabyPOSIX bridge

**Data:** 2026-10-15  
**Zip:** `artifacts/babyc_winapi_complete_bridge.zip`  
**Baseline:** `attachments/babyc.zip`

## Regola

> Tutto ciò che è implementato in **libWINAPI** deve essere richiamabile da BabyC  
> tramite `#include <windows.h>`.  
> Tutto ciò che è implementato in **BabyPOSIX** deve essere richiamabile da BabyC  
> tramite gli include POSIX (`unistd.h`, `pthread.h`, …).

Nessuna API del layer “nascosta” dal bridge.

## libWINAPI → WinFunctions (completamento)

Registrate le API che esistevano in `winapi/src` ma non nel bridge:

| API | Note |
|---|---|
| PulseEvent | presente in wthreads |
| TerminateThread | idem |
| SuspendThread / ResumeThread | possono fallire (non supportati a fondo) ma sono esposte |
| SetThreadPriority | esposta |
| WaitForMultipleObjects | esposta |
| SetFilePointerEx | esposta |
| GetDriveType | esposta |
| RtlInitUnicodeString | esposta (formal void*) |

**Verifica automatica:** `impl − registered = ∅` (54/54 API WINAPI in src).

## BabyPOSIX

Tabelle LibraryFunction già allineate agli handler (unistd, fd, dirent, time, pthread). Nessun gap rilevato in questo passo.

## Test

- `tests/windows/winapi_full_surface.c` — create/join/exit + CS + chiamate alle API nuove
- createthread, exitcode_cs, pthread_* regression

## Nota GUI

Le API solo-bridge (`MsgBox`, mouse, `FindWindow`, …) restano registrate: su Linux
sono stub intentionali (`WinGuiUnavailable`) perché **non** fanno parte di libWINAPI
(non esistono in `winapi/src`). Non violano la regola “tutto libWINAPI → BabyC”.
