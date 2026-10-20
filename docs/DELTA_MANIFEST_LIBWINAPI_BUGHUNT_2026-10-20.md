# Delta manifest — libWINAPI general bug hunt — 2026-10-20

Baseline cumulativa: `babyc_baseline_2026-10-19_phase2t1.zip`.

Il delta include tutte le modifiche libWINAPI già presenti nella baseline di lavoro cumulativa più le correzioni di questa sessione; non è limitato alle sole differenze dell'ultimo commit mentale. Per la lista dei difetti, test e limiti consultare `docs/LIBWINAPI_BUGHUNT_2026-10-20.md`.

## File modificati/aggiunti in questo turno

- `babyc/src/winapi/src/wfiles.c`
- `babyc/src/winapi/src/wfiles_private.c`
- `babyc/src/winapi/src/wprivate.h`
- `babyc/src/winapi/src/wdisplay.c`
- `babyc/src/winapi/src/wmsgbox.c`
- `babyc/src/winapi/src/wstring.c`
- `babyc/src/winapi/src/wstats.c`
- `babyc/src/winapi/tests/smoke.c`
- `babyc/src/winapi/STATUS.md`
- `babyc/docs/LIBWINAPI_BUGHUNT_2026-10-20.md`
- `babyc/docs/DELTA_MANIFEST_LIBWINAPI_BUGHUNT_2026-10-20.md`

## Validazione

- Standard `-Werror`: pass, CTest 1/1.
- ASan + UBSan, leak detection, thread/shared-memory: pass, CTest 1/1 per 20 ripetizioni consecutive.
- GCC `-fanalyzer -Werror`, thread/shared-memory: pass, CTest 1/1.
- ThreadSanitizer, thread/shared-memory: pass, CTest 1/1.
- `WINAPI_DISPLAY=ON`, `-Werror`: pass, CTest 1/1.
- `WINAPI_X11=ON`, `-Werror`: pass, CTest 1/1.

Non eseguita la build nativa MSVC/Windows; non eseguita la UI interattiva contro un server X11 reale.
