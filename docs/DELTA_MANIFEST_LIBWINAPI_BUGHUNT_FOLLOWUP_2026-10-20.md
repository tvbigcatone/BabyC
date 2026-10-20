# Delta manifest — libWINAPI bug-hunting follow-up (2026-10-20)

## Correzione

`MoveFileEx` con `MOVEFILE_WRITE_THROUGH` sincronizzava solo la directory di destinazione. Il contratto di persistenza richiede anche la sincronizzazione della directory sorgente quando il rename modifica due directory distinte. La funzione ora esegue `fsync` sul parent della destinazione e su quello della sorgente. Se una sincronizzazione fallisce dopo il rename, restituisce l'errore reale senza tentare di annullare un'operazione già committata.

## Test

Nuovo caso in `src/winapi/tests/smoke.c`: crea due directory distinte, crea un file nella sorgente, lo sposta con `MOVEFILE_WRITE_THROUGH` e verifica che la sorgente non esista più e la destinazione esista.

## Verifiche

- build standard `-Werror`: CTest 1/1
- ASan + UBSan + leak detection: CTest 1/1
- GCC `-fanalyzer -Werror`: CTest 1/1
- ThreadSanitizer: CTest 1/1
- `WINAPI_DISPLAY=ON`: CTest 1/1
- `WINAPI_X11=ON`: CTest 1/1

MSVC/Windows non eseguito. Il write-through può restituire errore dopo che il rename è già avvenuto, perché il commit non è reversibile in sicurezza.

## Correzione aggiuntiva: SetFileAttributes

L'attributo `FILE_ATTRIBUTE_ARCHIVE` è una proiezione sintetizzata per i file regolari, non un bit mutabile persistito dal backend POSIX. `SetFileAttributes` ora rifiuta con `ERROR_NOT_SUPPORTED` richieste che cambierebbero tale bit invece di riportare un successo apparente. Il test controlla il rifiuto di `FILE_ATTRIBUTE_NORMAL` su un file ordinario e la persistenza dell'attributo proiettato.
