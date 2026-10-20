# Integrazione libWINAPI — baseline 2026-10-20 06:45:16

## Baseline e sorgente

- Baseline fissa: `babyc(20261020-064516).zip` (SHA-256 `121b4c87492850d49ab492f766e095fde5541a3b35e8272ff92b0afdb3409f8c`).
- Delta integrato: `babyc_delta_libwinapi_filenameinfo_2026-10-20.zip` (SHA-256 `e973b9d4688e7cdb4373c073fc5bb795dfd3cc3e2ea14591d9645d4c80c38429`).
- Perimetro: integrazione cumulativa delle API libWINAPI e delle relative regressioni/documentazione, inclusi `FileNameInfo` e `FileNormalizedNameInfo`.

## Conflitti trovati e risolti

1. **`src/cstdlib_ext/win.c`** — il file nel delta non conteneva alcune modifiche della baseline necessarie al runtime BabyC: include `platform/guest_thread.h` e rilascio/riacquisizione del GIL attorno a `WaitForSingleObject` e `WaitForMultipleObjects`. Ho mantenuto il file della baseline come base e integrato soltanto i wrapper/registrazioni FileTime e `GetFileTime` del delta. In questo modo non si perdono le modifiche di integrazione dei thread di BabyC.
2. **Priorità thread: dichiarazione incompleta** — `wthreads.c` del delta implementava `GetThreadPriority`, ma `wthreads.h` non lo dichiarava e mancava `THREAD_PRIORITY_ERROR_RETURN`. Aggiunti la dichiarazione e il valore ABI Win32 `0x7fffffff`, necessari perché la build con `WINAPI_THREADS=ON` e `-Werror` falliva.
3. **Commento incoerente in `SetThreadPriority`** — il codice tenta una mappatura best-effort della priorità sui nice Linux, mentre il commento dichiarava che non veniva applicata allo scheduler. Aggiornato il commento per descrivere il comportamento effettivo: valore logico sempre conservato, aggiornamento scheduler best-effort e potenzialmente negato dai privilegi.

Gli altri file sovrapposti sono stati integrati dalla versione del delta allegato, che contiene l'espansione delle API file, gestione handle/lock, conversioni FILETIME, display/UI, thread, smoke test e stato/documentazione associata. Non sono stati aggiunti workaround specifici per benchmark.

## Verifiche eseguite in questa integrazione

- BabyC `make -j2 babyc`: **successo**, nessun `warning:` o `error:` nel log.
- libWINAPI CMake con `-Werror`, configurazione standard: build e CTest **1/1** superati.
- `WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`, `-Werror`: build e CTest **1/1** superati.
- `WINAPI_DISPLAY=ON`, `-Werror`: build e CTest **1/1** superati.
- `WINAPI_X11=ON`, `-Werror`: build e CTest **1/1** superati.

ASan/UBSan, TSan, GCC analyzer, Clang e build nativa MSVC/Windows non sono stati rieseguiti in questa integrazione; le verifiche riportate nei manifest allegati appartengono al delta sorgente e non sono una nuova certificazione indipendente di questa combinazione. La build MSVC/Windows resta non verificata.

## Packaging

Il delta risultante è cumulativo rispetto alla baseline fissa sopra indicata. Contiene solo file sorgente, test, documentazione e manifest; nessun binario o artefatto di build. `DELETE_FILES.txt` è vuoto: non è stato necessario eliminare file della baseline.
