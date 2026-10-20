# BabyPOSIX — Step pthread-1: supporto `pthread.h` cross-platform

**Data:** 2026-10-15  
**Obiettivo:** simmetria con il layer WINAPI (`windows.h` disponibile anche su Linux via libWINAPI): rendere `#include <pthread.h>` utilizzabile anche sotto Windows, con un sottoinsieme documentato implementato in BabyPOSIX.

## Cosa è stato aggiunto

| File | Ruolo |
|---|---|
| `src/posix/posix_pthread.h` | Dichiarazioni modulo |
| `src/posix/posix_pthread.c` | Implementazione portable |
| `src/tests/posix/pthread_mutex_basic.c` | Mutex init/lock/trylock/unlock/destroy + `PTHREAD_MUTEX_INITIALIZER` |
| `src/tests/posix/pthread_cond_basic.c` | Cond init/signal/broadcast/destroy |
| `src/tests/posix/pthread_create_enosys.c` | `pthread_create` → `ENOSYS` (policy trampolino) |

Registrazione in `include.c`:

```c
IncludeRegister(pc, "pthread.h", &BabyPosixPthreadSetupFunc,
    &BabyPosixPthreadFunctions[0], BabyPosixPthreadDefs);
```

`Makefile`: aggiunto `posix/posix_pthread.c` a `SRCS` (il link `-pthread` era già presente per libWINAPI).

## ABI guest (stabile su ogni host)

- `pthread_t`, `pthread_mutex_t`, `pthread_cond_t` → `unsigned long` (handle opachi, slot di tabelle interne)
- `pthread_once_t`, `pthread_attr_t` → `int`
- `PTHREAD_MUTEX_INITIALIZER` / `PTHREAD_COND_INITIALIZER` / `PTHREAD_ONCE_INIT` = `0`
- Primo `pthread_mutex_lock` / `trylock` su handle `0` alloca lazy (pattern educational comune)

## API supportate nello step 1

**Completi (main thread e, su host con thread OS, tra thread nativi una volta che il create sarà sbloccato):**

- `pthread_mutex_init/destroy/lock/unlock/trylock`
- `pthread_cond_init/destroy/wait/signal/broadcast`
- `pthread_once` (init interpretata ancora soft)
- `pthread_attr_init/destroy/setdetachstate/getdetachstate`
- `pthread_self` / `pthread_equal` (self riporta `0` finché non c’è mappa TLS slot↔OS id)
- `pthread_detach` / `pthread_join` / `pthread_exit` (struttura presente; join utile dopo create)

**Policy create (allineata a `CreateThread` nel bridge WINAPI):**

- `pthread_create` restituisce **`ENOSYS`**
- Motivo: il runtime interpretato non ha ancora un trampolino sicuro da `start_routine` guest a entry point del thread OS (stesso vincolo documentato per `CreateThread` in `BABYC_LIBWINAPI_INTEGRATION_B*.md`)

## Mapping host

| Host | Mutex | Cond | Thread table |
|---|---|---|---|
| UNIX | `pthread_mutex_t` | `pthread_cond_t` | `pthread_create` (quando sbloccato) |
| WIN32 | `CRITICAL_SECTION` | `CONDITION_VARIABLE` | `_beginthreadex` (quando sbloccato) |

Nessuna dipendenza pthread aggiunta al modulo FD (quel modulo resta sullo spinlock esplicito).

## Verifica Linux/GCC (questa sessione)

```
BabyPOSIX pthread mutex: ok
BabyPOSIX pthread cond: ok
BabyPOSIX pthread create enosys: ok
```

Regression spot-check: `unistd_subset`, `pipe_basic`, `fd_reuse` → ok.

## Prossimi passi

1. **Trampolino** condiviso (o gemello) con `CreateThread`: entry OS → lock interprete → chiamata funzione guest → unlock.
2. Mappa TLS `pthread_self` → slot tabella.
3. `pthread_cond_timedwait`, attributi mutex, `pthread_key_*` se servono ai test reali.
4. Runner MSVC / `run-posix-tests-msvc.bat` sull’ambiente Windows dell’utente.
