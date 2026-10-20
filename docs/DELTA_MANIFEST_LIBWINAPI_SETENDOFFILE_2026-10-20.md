# Delta libWINAPI — SetEndOfFile (2026-10-20)

Baseline cumulativa: `babyc_baseline_2026-10-19_phase2t1.zip`.

## Modifiche del turno

- `src/winapi/include/wfiles.h`: dichiarazione pubblica `SetEndOfFile(HANDLE)`.
- `src/winapi/src/wfile_handles.c`: implementazione sincronizzata con il lease del registro handle; verifica accesso in scrittura; usa la posizione corrente e `ftruncate()`; mappa `EFBIG` a `ERROR_DISK_FULL`.
- `src/winapi/tests/smoke.c`: test per troncamento, estensione, zero-fill, accesso negato in sola lettura e handle invalido.
- `src/winapi/STATUS.md`: registro del round e limiti noti.

## Verifica

Tutte le build e i test sono stati eseguiti nel backend Linux/GCC:

1. Build ASan+UBSan, `WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`, leak detection: CTest 1/1 superato.
2. Build standard con `-Werror`: CTest 1/1 superato.
3. Build con `-fanalyzer -Werror`, thread e shared memory abilitati: CTest 1/1 superato.

La compilazione nativa MSVC/Windows rimane da verificare. Il delta ZIP contiene tutti i file modificati rispetto alla baseline indicata, inclusi i round precedenti.
