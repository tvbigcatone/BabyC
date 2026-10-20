# Delta — libWINAPI thread parity (GetExitCodeThread + CS)

**Data:** 2026-10-15  
**Zip:** `artifacts/babyc_winapi_thread_parity.zip`  
**Baseline:** `attachments/babyc.zip`

## Ideologia

- Layer specular: sync + create allo stesso livello di supporto.
- Host nativo = wrapper; host opposto = implementazione layer.
- GUI/helper non ufficiali restano stub su Linux (`WinGuiUnavailable`).

## Aggiunte bridge Linux (`win.c`)

- `GetExitCodeThread`
- `InitializeCriticalSection` / `Enter` / `Leave` / `Delete`
- `WinDefs`: `CRITICAL_SECTION` opaque 64 byte + `STILL_ACTIVE`

## Test

- `tests/windows/winapi_exitcode_cs.c` — create + CS increment + exit code 7
- regression: createthread, pthread create/join/mutex/cond

## Simmetria

| | BabyPOSIX | libWINAPI |
|---|---|---|
| create | pthread_create | CreateThread |
| wait | pthread_join | WaitForSingleObject |
| exit status | join retval | GetExitCodeThread |
| mutex | pthread_mutex | CreateMutex **e** CRITICAL_SECTION |
| event/cond | pthread_cond | CreateEvent/Set/Reset |
