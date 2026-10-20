# Delta manifest — CopyFile e file temporanei (2026-10-20)

## Cambiamenti

- `src/winapi/include/wfiles.h`
  - Dichiarate `CopyFile`, `GetTempPath`, `GetTempFileName`.
- `src/winapi/src/wfiles.c`
  - Implementata copia transazionale tramite file temporaneo nella directory di destinazione. Cicli robusti per `read`/`write` parziali e `EINTR`, controlli di `fsync`/`close`, preservazione dei permessi e tentativo di preservare i timestamp.
  - `bFailIfExists` usa `link()` come commit atomico senza sostituzione; overwrite usa `rename()` per pubblicare solo contenuto completo.
  - Aggiunte API temporanee con fallback di directory, validazione dei buffer, generazione esclusiva e codici errore Win32; separati gli errori `stat` dai percorsi esistenti che non sono directory per evitare `errno` stale.
  - Corretto `_splitpath` per evitare ambiguità nel rilascio del buffer convertito e mantenere il parsing quando la conversione di percorsi assoluti fallisce prima dell'inizializzazione della drive map.
- `src/winapi/tests/smoke.c`
  - Copia piccola e payload da 160.123 byte; no-overwrite, overwrite, sorgente uguale alla destinazione, sorgente directory/mancante; API temporanee; buffer corto; percorso relativo `_splitpath`.
- `src/winapi/STATUS.md`
  - Aggiunto Round C-prio11 con limiti e verifiche.

## Verifiche effettuate

- CMake standard con `-Werror`: build completata e CTest 1/1 passato.
- CMake ASan+UBSan, leak detection, thread e shared memory: build completata e CTest 1/1 passato.
- CMake con `-fanalyzer -Werror`, thread e shared memory: build completata e CTest 1/1 passato.

## Limiti

- Nessuna promessa di equivalenza completa con NTFS: niente ACL, alternate data streams o security descriptors; timestamp best-effort.
- `GetTempFileName` segue la firma Win32 senza lunghezza esplicita dell'output e quindi richiede un buffer adeguato (`MAX_PATH`).
- Compilazione nativa MSVC/Windows non eseguita in questo ambiente Linux.
