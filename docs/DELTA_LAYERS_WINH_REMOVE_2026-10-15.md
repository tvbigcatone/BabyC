# Delta — rimozione `win.h` + ideologia thin layer

**Data:** 2026-10-15  
**Baseline:** `attachments/babyc.zip`  
**Ideologia:** interprete educational + due thin layer speculari (BabyPOSIX ↔ libWINAPI)

## Decisioni

1. **`win.h` non si registra.** Non è un header ufficiale Windows. L’unico include
   guest Win32 è `#include <windows.h>`.
2. **`cstdlib_ext/win.c`** è solo il *bridge* `WinSetupFunc` per `windows.h`
   (wrapper nativi su Win32, libWINAPI su Linux). Non è una superficie `win.h`.
3. **Simmetria di supporto:** sync mutex/event ↔ mutex/cond allo stesso livello;
   `CreateThread` e `pthread_create` restano bloccati allo stesso modo finché
   non c’è un trampolino guest→thread OS condiviso.
4. **Ambiente nativo = wrapper** sulle funzioni di sistema già disponibili;
   ambiente non-nativo = implementazione portable del layer.

## Verifica

| Check | Esito |
|---|---|
| `#include <windows.h>` + `GetTickCount` | ok |
| `#include "win.h"` | errore `can't read file win.h` |
| `pthread_mutex_basic` | ok |
| `pthread_create_enosys` | ok (`ENOSYS`) |

## File nel delta zip (vs baseline)

- `src/include.c` — no `win.h`; guard `BABYC_MICRO`; registrazione `pthread.h`
- `src/Makefile` — `posix_pthread.c`
- `src/posix/posix_pthread.c` / `.h`
- `src/tests/posix/pthread_*.c`
- `src/cstdlib_ext/win.c` — banner “windows.h bridge, not win.h”
- `docs/ARCHITECTURE_THIN_LAYERS.md`
- `docs/BabyPOSIX-PTHREAD-STEP1.md`
- questo file

## Prossimo passo

Audit API ancora solo in `win.c` (soprattutto GUI/helper non ufficiali) e
decisione: integrare in libWINAPI, lasciare come Intrinsic solo-bridge, o
deprecare. In parallelo: trampolino comune per `CreateThread` / `pthread_create`.
