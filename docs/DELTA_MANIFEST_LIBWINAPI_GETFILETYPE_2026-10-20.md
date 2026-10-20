# Delta libWINAPI — GetFileType — 2026-10-20

Baseline: `babyc_baseline_2026-10-19_phase2t1.zip`.

## Modifiche del turno

- `src/winapi/include/wfiles.h`: aggiunti `FILE_TYPE_*` e dichiarazione `GetFileType`.
- `src/winapi/src/wfile_handles.c`: implementata la verifica tramite lease del registro handle; gli handle di file regolari restituiscono `FILE_TYPE_DISK`, gli handle non validi restituiscono `FILE_TYPE_UNKNOWN` con errore Win32 coerente.
- `src/winapi/tests/smoke.c`: test per handle file valido, last-error in caso di successo e handle già chiuso.
- `src/winapi/STATUS.md`: registrazione di implementazione, verifica e limiti.

## Verifiche

Build CMake standard con `-Werror`, build completa con `WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`, `-fanalyzer -Werror`, e build completa ASan+UBSan con leak detection. In tutte le configurazioni CTest passa (1/1).

## Limiti

Il backend file registra solo file regolari: `FILE_TYPE_CHAR`, `FILE_TYPE_PIPE` e `FILE_TYPE_REMOTE` sono definiti per compatibilità di header ma non dichiarati implementati. MSVC/Windows non è stato eseguito in questo ambiente.
