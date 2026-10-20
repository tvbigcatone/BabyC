# libWINAPI — delta cumulativo ciclo di vita handle e apertura file

Baseline: `babyc_baseline_2026-10-19_phase2t1.zip`.

## Modifiche di questo turno

- `src/winapi/src/wfile_handles.c`: `CREATE_ALWAYS` e `OPEN_ALWAYS` determinano in modo atomico se il file è stato creato o già esisteva; evitata la race `stat()`/`open()` e aggiunto retry se il file sparisce tra il tentativo esclusivo e la riapertura.
- `src/winapi/tests/smoke.c`: regressioni per `OPEN_ALWAYS` nuovo/esistente, preservazione dei dati, stato `ERROR_ALREADY_EXISTS` e `CREATE_ALWAYS` su percorso nuovo.
- `src/winapi/STATUS.md`: registrato il round C-prio4 e i limiti di compatibilità.

Il delta contiene anche gli interventi precedenti già presenti nella baseline cumulativa di lavoro: FILETIME, EOF di ReadFile, heavy bug hunt e transazioni di chiusura handle. Le API non supportate non vengono presentate come complete.

## Verifiche

- CMake full: `WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`, `-Werror`, ASan+UBSan.
- `ASAN_OPTIONS=detect_leaks=1 ctest --output-on-failure`: 1/1 test superato.
- MSVC/Windows non disponibile nell'ambiente di esecuzione: verifica nativa ancora richiesta.


## Round successivo — FlushFileBuffers (2026-10-20)

Aggiunta l'API `FlushFileBuffers` per gli handle di file registrati. La funzione usa un lease (`wfile_begin_io`/`wfile_end_io`) e quindi è coordinata con `CloseHandle`; richiede accesso in scrittura e propaga gli errori del backend tramite il mapping Win32. Sono inclusi test per successo, accesso negato su handle read-only e handle non valido dopo la chiusura.

Verifica eseguita dopo la modifica: build standard `-Werror` + CTest, build completa `WINAPI_THREADS=ON`/`WINAPI_SHMEM=ON` con `-fanalyzer -Werror` + CTest e build completa ASan/UBSan con leak detection + CTest. I risultati puntuali sono riportati nel riepilogo del turno; il backend nativo MSVC/Windows non è disponibile in questa sessione.
