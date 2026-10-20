# BabyC — thin layers per la compatibilità bidirezionale

**Aggiornamento:** 2026-10-15

## Ruolo del core

BabyC è un progetto **educational**. Il nucleo è un interprete C che restituisce
diagnostica amichevole allo studente (OOB, use-after-free, variabile non
inizializzata, …). I thin layer non sostituiscono quel compito: espongono un
sottoinsieme documentato di API di sistema eseguibile **sotto l’interprete**.

## Due thin layer (desktop only, esclusi da `BABYC_MICRO`)

| Layer | Include ufficiale | Host nativo | Host non-nativo |
|---|---|---|---|
| **BabyPOSIX** | `unistd.h`, `fcntl.h`, `dirent.h`, `pthread.h`, `sys/time.h` | **Linux**: wrapper sulle API POSIX del sistema | **Windows**: implementazione portable (fd table, CS/CV, …) |
| **libWINAPI** | **`windows.h` only** | **Windows**: wrapper sulle API Win32 native | **Linux**: implementazione in `src/winapi/` |

### Principio di simmetria

I due layer devono restare **il più possibile speculari** per livello di supporto:

- se libWINAPI espone sincronizzazione a livello mutex/event/wait
  (`CreateMutex`, `CreateEvent`, `WaitForSingleObject`, …),
  BabyPOSIX espone il livello corrispondente in pthreads
  (`pthread_mutex_*`, `pthread_cond_*`, …);
- se la creazione di thread con **callback interpretata** non è ancora sicura
  (`CreateThread` non registrato finché manca il trampolino),
  anche `pthread_create` resta allo stesso livello (`ENOSYS` / non esposto)
  finché non esiste lo stesso meccanismo.

In ambiente nativo il thin layer **non** sostituisce il sistema: è un
**wrapper** che registra i prototipi guest e inoltra alle funzioni native,
così lo studente continua a disporre delle API già implementate.

### `win.h` non esiste

`win.h` **non** è un header ufficiale Windows e **non** deve essere registrato
né come alias né come superficie parallela. L’unico include Win32 del guest è:

```c
#include <windows.h>
```

Il file `src/cstdlib_ext/win.c` non è “l’API win.h”: è il **bridge interprete**
che implementa `WinSetupFunc` per `windows.h` (branch native Win32 + branch
Linux che chiama libWINAPI). Le implementazioni host-side non native vivono in
`src/winapi/`. Eventuali API ancora solo in `win.c` vanno integrate in
libWINAPI (header + `src/winapi/src/…`) e poi solo avvolte dal bridge.

### Come si caricano

```c
#include <windows.h>   /* → WinSetupFunc → native Win32 | libWINAPI */
#include <unistd.h>    /* → BabyPOSIX */
#include <pthread.h>   /* → BabyPOSIX pthreads */
```

## Livello di supporto attuale (simmetria sync)

| Famiglia | libWINAPI / `windows.h` | BabyPOSIX / `pthread.h` |
|---|---|---|
| Mutex | `CreateMutex`, `ReleaseMutex`, … | `pthread_mutex_*` |
| Event / cond | `CreateEvent`, `SetEvent`, `WaitForSingleObject`, … | `pthread_cond_*` |
| Thread create + guest callback | non registrato (`CreateThread` in attesa trampolino) | `pthread_create` → `ENOSYS` (stesso vincolo) |
| Join / exit | via handle wait dove applicabile | struttura presente; utile dopo trampolino |

## Principi

1. **ABI guest stabile** su ogni host.
2. **Sottoinsieme piccolo e testato**, non un clone completo di POSIX/Win32.
3. **Un solo include ufficiale per famiglia** (`windows.h`, `pthread.h`, …).
4. **Wrapper in ambiente nativo**, implementazione portable in ambiente non-nativo.
5. **Stessa policy sui callback** verso thread OS finché non c’è un trampolino
   educational-safe (diagnostica e single-interpreter-lock da progettare insieme).

## Riferimenti

- BabyPOSIX: `docs/BabyPOSIX-STEP16.md`, `STEP17`, `BabyPOSIX-PTHREAD-STEP1.md`
- libWINAPI: `docs/BABYC_LIBWINAPI_INTEGRATION_B1.md` … `B4.md`
- Micro: `src/Makefile.minimal` (`-DBABYC_MICRO`)

## Thread surface (2026-10-15)

Shared trampoline: `platform/guest_thread.c` (GIL + BabycGuestCall).

| Capability | BabyPOSIX | libWINAPI |
|---|---|---|
| create + join/wait | pthread_create/join | CreateThread + WaitForSingleObject |
| exit code | pthread_join retval | GetExitCodeThread |
| mutex | pthread_mutex_* | CreateMutex / CRITICAL_SECTION |
| cond/event | pthread_cond_* | CreateEvent / SetEvent / ResetEvent |

GUI helpers still registered on Linux as stubs (`WinGuiUnavailable`) — educational surface only; not part of the thin layer core.
