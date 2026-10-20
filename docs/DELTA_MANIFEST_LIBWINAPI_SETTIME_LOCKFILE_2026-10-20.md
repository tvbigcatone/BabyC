# Delta libWINAPI — SetFileTime e byte-range locks (2026-10-20)

## File modificati
- `src/winapi/include/wfiles.h` — dichiarazioni pubbliche `SetFileTime`, `LockFile`, `UnlockFile`.
- `src/winapi/src/wfile_handles.c` — conversione FILETIME→timespec controllata, aggiornamento timestamp tramite `futimens`, byte-range locking tramite `fcntl`, mapping errori, bookkeeping e cleanup delle regioni per handle.
- `src/winapi/tests/smoke.c` — regressioni per timestamp, errori, lock/unlock, range sovrapposti, conflitto fra handle e rilascio a chiusura.
- `src/winapi/STATUS.md` — stato e limiti dell'implementazione.

## Semantica e limiti
`SetFileTime` supporta l'aggiornamento di access time e write time. Se `lpCreationTime` non è nullo restituisce `ERROR_NOT_SUPPORTED` senza modificare gli altri timestamp; è una limitazione esplicita del backend POSIX, non una simulazione. I valori non rappresentabili dal `time_t` host vengono rifiutati.

`LockFile`/`UnlockFile` gestiscono range non vuoti, con offset e lunghezza combinati in 64 bit e verifiche contro overflow/limiti `off_t`. `UnlockFile` accetta solo un range esattamente acquisito sullo stesso handle. Range sovrapposti ma diversi sul medesimo handle sono rifiutati per non affidarsi alla fusione delle regioni POSIX. Il file sorgente abilita `_GNU_SOURCE` prima degli header POSIX così la build GNU/Linux rileva davvero `F_OFD_SETLK`; il test di conflitto fra due aperture indipendenti è quindi attivo in questa build. Su Linux viene usato `F_OFD_SETLK` quando disponibile; il fallback `F_SETLK` ha semantica di lock associata al processo e quindi è meno fedele fra più aperture nello stesso processo.

## Hardening aggiuntivo nello stesso turno
`GetTempFileName` valida i primi tre caratteri del prefisso prima di risolvere il percorso. I test provano `../` e `a\\b`, che vengono rifiutati con `ERROR_INVALID_PARAMETER`.

## Test eseguiti
- Build standard con `-Werror`: CTest 1/1.
- Build ASan+UBSan con leak detection, thread/shared memory: CTest 1/1.
- Build `-fanalyzer -Werror`, thread/shared memory: CTest 1/1.
- MSVC/Windows non eseguito in questo ambiente.
