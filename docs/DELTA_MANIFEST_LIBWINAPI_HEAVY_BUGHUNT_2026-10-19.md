# Delta cumulativo libWINAPI — heavy bug hunting (2026-10-19)

Baseline iniziale: `babyc_baseline_2026-10-19_phase2t1.zip`  
SHA-256 baseline: `d4c4ee298112801b1e221463dafa6d0c526cf8cc5dce9614f14a66d8ce7b5e9f`

Questo archivio è un overlay cumulativo rispetto alla baseline iniziale, non soltanto rispetto al delta precedente. I percorsi iniziano con `babyc/`; sono inclusi file nuovi o modificati rispetto alla baseline. Non contiene build artifact, `src/posix`, parser/interprete, test numerati o progetti MSVC.

## Bug corretti in questo round

1. `CreateFile` con `CREATE_ALWAYS` e solo `GENERIC_READ` poteva raggiungere `open(O_RDONLY | O_TRUNC)`. POSIX non definisce quel caso e Linux può troncare comunque il file. Ora la richiesta viene respinta con `ERROR_ACCESS_DENIED` prima di modificare il file.
2. `CreateFile` lasciava un `GetLastError()` precedente anche dopo un'apertura/creazione riuscita normale. Ora restituisce `ERROR_SUCCESS` in caso ordinario e `ERROR_ALREADY_EXISTS` nel caso previsto.
3. `GetCurrentDirectory`, `SetCurrentDirectory` e `GetDiskFreeSpaceEx` non impostavano in modo deterministico `GetLastError()` su tutti i percorsi di errore/successo. È stato aggiunto il mapping errno→Win32 e coperto con regressioni.
4. `GetDiskFreeSpaceEx` moltiplicava dimensione blocco e conteggi `statvfs` senza controllare l'overflow e poteva aggiornare solo parte degli output. Ora controlla l'overflow prima di pubblicare qualsiasi risultato.
5. GCC `-fanalyzer` riportava un possibile leak in `_findfirst` dovuto al confronto fra puntatore originale e stringa convertita. La gestione è stata rifattorizzata con un puntatore separato e rilascio esplicito, eliminando l'avviso senza disabilitare il controllo.

## Test

- CMake standard, `-Werror`: build + CTest superati (1/1).
- CMake completa con `WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`, `-fanalyzer -Werror`: build + CTest superati (1/1).
- ASan + UBSan, `ASAN_OPTIONS=detect_leaks=1`: CTest superato 20/20 ripetizioni.
- MSVC/Windows: non eseguito in questo ambiente; da verificare esternamente.

## Limiti espliciti non dichiarati come completati

I/O overlapped, enforcement dei file-share mode, eventi nominati/OpenEvent, `TerminateThread`, `SuspendThread`/`ResumeThread`, inheritance/security descriptors e alcune opzioni di mapping restano non supportati. Non sono stati implementati tramite scorciatoie che simulino in modo ingannevole garanzie Win32 non disponibili.

## Inventario dei file inclusi

| Percorso nell’archivio | SHA-256 |
|---|---|
| `babyc/docs/DELTA_MANIFEST_LIBWINAPI_FILETIME_2026-10-19.md` | `c4c39b1313dfa0482be059960f012185a532c547e05f51ff510a25194be6f833` |
| `babyc/docs/DELTA_MANIFEST_LIBWINAPI_READFILE_EOF_2026-10-19.md` | `d7ac0870b58d75063ffa89df2bff024426d1bb51d55824e1419d779e11ea2af8` |
| `babyc/src/cstdlib_ext/win.c` | `2e476423b3f13ca8f07358c95a51855243351986d051e0c25241cc91cbd99463` |
| `babyc/src/tests/windows/winapi_filetime.c` | `ca6622928f53825f7d69537be386e3462b5088010b8b68b2d6ec625422f66ab6` |
| `babyc/src/winapi/STATUS.md` | `f1f2752d6b2dddc8f7da09849758465a41cb274301acc853dffbc97ecb76bd64` |
| `babyc/src/winapi/include/wfiles.h` | `6eebcac9beddd730114ac135325b624851c226ae1668a6b0caf543f04e8e231a` |
| `babyc/src/winapi/include/wtime.h` | `8795f02036cc3a71a02dbed9b617160c6640d5db54e1ffec0ac5bbba69cd9a9f` |
| `babyc/src/winapi/src/wfile_handles.c` | `b2d7fa3ebfc4724e7b6188a5465e543ddedbd0f27a10781db176b79f46965c91` |
| `babyc/src/winapi/src/wfiles.c` | `f41009e41ed2b88b526b82d4989229797ef1c5798ed5e2780982310b8621ad99` |
| `babyc/src/winapi/src/wprivate.h` | `63ed195b413b77f95d6d75bc580f1f46b2aa23d5040228587f67ec6c9968d9a4` |
| `babyc/src/winapi/src/wthreads.c` | `4f57461884a1c6fea2fedb1575aaf86fcd98d1791f014f3ef31f08bd86ccaddb` |
| `babyc/src/winapi/src/wtime.c` | `5d015d1ee8931762ec851ff266c9ff58023f2792fd52df346ba381096b3b2006` |
| `babyc/src/winapi/tests/smoke.c` | `9c28cdddc903dd592fb8ad0bf653be507e06498ecf67398d2aa2477ce7f69f7f` |

## Round aggiuntivo — rollback del ciclo di vita degli handle (2026-10-20)

- In `src/winapi/src/wthreads.c`, se il rilascio di una mappatura di evento nominato fallisce, il record dell'handle viene ripristinato nel registro invece di essere perso dopo la rimozione. Il backing descriptor rimane aperto e l'errore è esplicito.
- `CloseHandle` ora controlla `pthread_join`; se il join fallisce, ripristina il record del thread e non libera le strutture native senza conferma della chiusura.
- Test: build CMake completa `WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON` e CTest 1/1 superati. La verifica nativa MSVC/Windows resta esterna.
