# Delta cumulativo libWINAPI — 2026-10-20

## Baseline

- Baseline iniziale: `babyc_baseline_2026-10-19_phase2t1.zip`.
- Il pacchetto include tutti i file nuovi/modificati rispetto a quella baseline, non soltanto il delta dell’ultimo intervento.
- Perimetro: `src/winapi` e documentazione relativa; BabyPOSIX (`src/posix`) non viene modificato.

## Interventi aggiunti in questo turno

- Aggiunta `SetFileInformationByHandle` per `FileEndOfFileInfo` (ftruncate, validazione signed/overflow, handle scrivibile, posizione corrente preservata).
- Aggiunta `SetFileInformationByHandle` per il sottoinsieme rappresentabile di `FileBasicInfo` (LastAccess/LastWrite; campi non supportati rifiutati prima delle mutazioni).
- Aggiunta `FileIdInfo` a `GetFileInformationByHandleEx` con layout `FILE_ID_128` / `FILE_ID_INFO` e proiezione esplicita device/inode POSIX.
- Aggiunta `FileAlignmentInfo` con layout ABI dedicato e requisito zero per file regolari, coerente con I/O POSIX byte-granulare.
- Regressioni per layout ABI, buffer corti senza output parziale, handle/classi invalidi, EOF negativo, accesso read-only, timestamp, posizione file, ID e allineamento.
- Aggiornato `src/winapi/STATUS.md` e `docs/LIBWINAPI_BUGHUNT_2026-10-20.md`.

## Verifiche

- GCC standard `-Werror`: build e CTest superati.
- ASan + UBSan + leak detection: build/CTest superati, poi 20 esecuzioni consecutive superate.
- GCC `-fanalyzer -Werror`: build e CTest superati.
- ThreadSanitizer: build/CTest superati, poi 10 esecuzioni consecutive superate.
- `WINAPI_DISPLAY=ON`, `WINAPI_X11=ON`: build e CTest superati.
- Clang 17 `-Werror`: build e CTest superati.
- MSVC/Windows nativo non eseguito in questo ambiente.

## Contenuto

- File modificati/aggiunti (prima di questo manifest): 34.
- File rimossi dalla baseline: 0.
- Nessun file generato di build è incluso.

### Elenco file

- `docs/DELTA_MANIFEST_LIBWINAPI_FILENAMEINFO_2026-10-20.md`
- `docs/DELTA_MANIFEST_LIBWINAPI_BUGHUNT_2026-10-20.md`
- `docs/DELTA_MANIFEST_LIBWINAPI_BUGHUNT_FOLLOWUP_2026-10-20.md`
- `docs/DELTA_MANIFEST_LIBWINAPI_COPYFILE_TEMP_2026-10-20.md`
- `docs/DELTA_MANIFEST_LIBWINAPI_FILETIME_2026-10-19.md`
- `docs/DELTA_MANIFEST_LIBWINAPI_FSOPS_2026-10-20.md`
- `docs/DELTA_MANIFEST_LIBWINAPI_GETFILEATTRIBUTES_2026-10-20.md`
- `docs/DELTA_MANIFEST_LIBWINAPI_GETFILEINFORMATIONEX_2026-10-20.md`
- `docs/DELTA_MANIFEST_LIBWINAPI_GETFILEINFORMATION_2026-10-20.md`
- `docs/DELTA_MANIFEST_LIBWINAPI_GETFILETYPE_2026-10-20.md`
- `docs/DELTA_MANIFEST_LIBWINAPI_HANDLE_LIFECYCLE_2026-10-20.md`
- `docs/DELTA_MANIFEST_LIBWINAPI_HEAVY_BUGHUNT_2026-10-19.md`
- `docs/DELTA_MANIFEST_LIBWINAPI_MOVEFILEEX_2026-10-20.md`
- `docs/DELTA_MANIFEST_LIBWINAPI_READFILE_EOF_2026-10-19.md`
- `docs/DELTA_MANIFEST_LIBWINAPI_SETENDOFFILE_2026-10-20.md`
- `docs/DELTA_MANIFEST_LIBWINAPI_SETTIME_LOCKFILE_2026-10-20.md`
- `docs/LIBWINAPI_BUGHUNT_2026-10-20.md`
- `src/cstdlib_ext/win.c`
- `src/tests/windows/winapi_filetime.c`
- `src/winapi/STATUS.md`
- `src/winapi/include/wfiles.h`
- `src/winapi/include/wtime.h`
- `src/winapi/src/wdisplay.c`
- `src/winapi/src/wfile_handles.c`
- `src/winapi/src/wfiles.c`
- `src/winapi/src/wfiles_private.c`
- `src/winapi/src/wmsgbox.c`
- `src/winapi/src/wperf.c`
- `src/winapi/src/wprivate.h`
- `src/winapi/src/wstats.c`
- `src/winapi/src/wstring.c`
- `src/winapi/src/wthreads.c`
- `src/winapi/src/wtime.c`
- `src/winapi/tests/smoke.c`


## Round C-prio19 — FileNameInfo / FileNormalizedNameInfo

- `GetFileInformationByHandleEx` ora gestisce `FileNameInfo` e `FileNormalizedNameInfo` per file regolari aperti da `CreateFile`. Recupera il nome del descriptor via `/proc/self/fd` sotto lease attivo e converte UTF-8 in UTF-16 a 16 bit con validazione delle sequenze e delle coppie surrogate.
- Aggiunta struttura `FILE_NAME_INFO` con unità `WORD` per ABI Win32; `FileNameLength` è espresso in byte e non include terminatore. I buffer insufficienti non vengono modificati. File già unlinkati sono rifiutati.
- Test: nomi ASCII/UTF-8, nome normalizzato, buffer troppo corto, layout e handle ancora aperto dopo unlink.
- Limite dichiarato: richiede Linux `/proc/self/fd`; il supporto non è disponibile sugli altri backend POSIX.
