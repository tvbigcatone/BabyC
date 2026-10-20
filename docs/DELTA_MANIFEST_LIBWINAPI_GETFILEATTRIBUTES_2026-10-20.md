# Delta libWINAPI — GetFileAttributes / GetFileAttributesEx (2026-10-20)

## Modifiche

- `src/winapi/include/wfiles.h`: dichiarate `GetFileAttributes`, `GetFileAttributesEx`, `WIN32_FILE_ATTRIBUTE_DATA`, `GET_FILEEX_INFO_LEVELS` e `INVALID_FILE_ATTRIBUTES`.
- `src/winapi/src/wfiles.c`: introdotta una classificazione comune degli attributi basata su `stat`; implementate le due API con conversione percorso Windows, controlli degli argomenti, timestamp FILETIME, dimensione a 64 bit e `GetLastError` deterministico. `FindFirstFile` riusa la stessa classificazione.
- `src/winapi/tests/smoke.c`: test di regressione per file non vuoto, attributi, timestamp, livello non supportato, output nullo e percorso assente.
- `src/winapi/STATUS.md`: stato, verifiche e limiti registrati.

## Limiti espliciti

La compatibilità è quella del backend POSIX, non una replica completa di NTFS. I file nascosti sono riconosciuti tramite basename con punto iniziale; `FILE_ATTRIBUTE_SYSTEM` e `FILE_ATTRIBUTE_TEMPORARY` non sono sintetizzati. `ftCreationTime` è un proxy basato su `ctime` dove non è disponibile un birth time portabile. `GetFileAttributesEx` supporta solo `GetFileExInfoStandard`.

## Verifica eseguita

- Build standard con `-Werror`: CTest 1/1 superato.
- Build `WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`, ASan+UBSan, leak detection attiva: CTest 1/1 superato.
- Build `WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`, `-fanalyzer -Werror`: CTest 1/1 superato.
- MSVC/Windows non è stato eseguito in questo ambiente.
