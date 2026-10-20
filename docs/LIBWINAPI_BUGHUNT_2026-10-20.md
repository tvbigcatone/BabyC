# libWINAPI — general bug-hunting session (2026-10-20)

## Scope

Audit statica e test mirati su `src/winapi` e sulle funzioni CRT adattate da `wfiles_private.c`. Non sono stati modificati BabyPOSIX, IR, progetti MSVC o test numerati esterni. Il lavoro resta una compatibilità POSIX→Win32 parziale; la build nativa Windows deve essere eseguita separatamente.

## Difetti corretti

### File e directory

1. **`FindFirstFile`/`FindNextFile` restituivano nomi vuoti per directory.** `glob(GLOB_MARK)` aggiunge `/` al percorso delle directory e l'estrazione del basename prendeva la stringa dopo l'ultimo slash. Ora il basename viene estratto dopo aver ignorato i separatori finali, senza alterare il percorso usato da `stat()`.
2. **Le ricerche wildcard omettevano i nomi nascosti.** Il `glob()` POSIX predefinito ignora i nomi che iniziano con `.`; ora viene usato `GLOB_PERIOD` quando disponibile, in modo da includerli nell'enumerazione Win32-like.
3. **Le directory nascoste perdevano `FILE_ATTRIBUTE_HIDDEN` durante l'enumerazione.** La classificazione ora considera il basename dopo la rimozione logica dei separatori finali e distingue `.` e `..`.
4. **Un elemento sparito tra `glob()` e `stat()` veniva riportato come record riuscito con metadati a zero.** La copia dei dati fallisce ora e `FindNextFile` traduce l'errore invece di pubblicare un record incompleto.
5. **`GetLogicalDriveStrings` restituiva designatori (`C:`) invece di root path (`C:\`).** Ora ogni voce include il backslash finale e il doppio NUL; dimensionamento e controlli overflow sono aggiornati.
6. **`SetFileAttributes` poteva modificare il target di un symlink.** `stat()` seguito da `chmod()` seguiva il link e cambiava i permessi dell'oggetto puntato. Ora il percorso è controllato con `lstat()` e i symlink vengono rifiutati con `ERROR_NOT_SUPPORTED`.
7. **`resolve_root_path` copiava il token `root=` con `strcpy` senza conoscere la capacità di destinazione.** La funzione riceve ora la capacità e rifiuta token troppo lunghi prima di copiare.

### Wrapper CRT

8. **`win_remove` usava `unlink()` e non poteva rimuovere directory vuote.** Ora delega a `remove()`, come richiesto dal contratto C.
9. **`win_mkdir` trattava `EEXIST` come successo e `win_rmdir` trattava `ENOENT` come successo.** Sono stati rimossi questi falsi successi. Gli argomenti nulli e i percorsi vuoti hanno ora `errno` deterministico; `win_fopen` controlla gli argomenti prima di `is_win_path()`.
10. **`win_fclose` ignorava gli errori di `fflush` e `fdatasync`.** Ora restituisce `EOF` se flush, sync di un file regolare o close falliscono, preservando il primo errore. Evita `fdatasync` su device speciali, dove `EINVAL` non deve trasformare un close valido in errore.

### Stringhe, memoria e UI

11. **`RtlInitUnicodeString` impostava `MaximumLength == Length` e troncava senza controllo a `USHORT`.** Ora `Length` esclude il terminatore, `MaximumLength` riserva lo spazio per esso e le stringhe lunghe vengono saturate al massimo rappresentabile senza wrap.
12. **`GlobalMemoryStatusEx` non segnalava il fallimento di `sysinfo`, poteva fare overflow nei prodotti/somme e poteva riportare valori disponibili superiori ai totali.** Aggiunti controlli aritmetici, clamp delle stime e `ullAvailVirtual` stimato sottraendo il `VmSize` del processo da un limite conservativo.
13. **Il backend X11 di `MessageBox` aveva una gestione errata di `WM_DELETE_WINDOW`** (confronto del primo carattere del nome dell'atomo, uso di `only_if_exists=True`, possibile dereferenziazione nulla), restituiva `0` anche dopo una chiusura normale e dichiarava riuscite le modalità con pulsanti non supportate. Ora il protocollo è verificato attraverso `WM_PROTOCOLS`/`data.l[0]`, l'esito OK è `IDOK` (1), le modalità di pulsanti non implementate falliscono esplicitamente e vengono controllati alcuni fallimenti di allocazione X11.
14. **`EnumDisplayDevices` poteva concatenare nomi socket arbitrariamente lunghi in un buffer da 64 byte, copiare nomi troppo lunghi in `DeviceName` e perdere il descrittore della directory su ritorno anticipato.** Ora i nomi sono validati e copiati in modo limitato, gli argomenti sono verificati e la directory viene sempre chiusa. `EnumDisplaySettings` e `ChangeDisplaySettings`, precedentemente no-op che dichiaravano successo, restituiscono ora un errore esplicito di funzione non implementata.

## Copertura di regressione aggiunta

- Wildcard su directory normali e nascoste, nomi non vuoti, attributi directory/hidden e fine enumerazione.
- Wrapper `mkdir`/`rmdir`/`remove` su esistenza, assenza e directory vuote.
- `fclose` su `/dev/full` per verificare la propagazione dell'errore e su `/dev/null` per evitare falsi errori sui device speciali.
- `RtlInitUnicodeString` con stringa corta e buffer lungo, incluso controllo del limite `USHORT`.
- `GetLogicalDriveStrings`: dimensione richiesta, backslash finale per ogni root e doppio NUL.
- `GlobalMemoryStatusEx`: percentuale entro 100 e valori disponibili non superiori ai totali.
- `SetFileAttributes` su symlink: errore esplicito e verifica che i permessi del target non cambino.
- `MessageBox` e API display: modalità non supportate e argomenti invalidi.

## Verifiche eseguite

- Build standard con `-Werror`: CTest 1/1 superato.
- ASan + UBSan, leak detection, `WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`: CTest 1/1 superato in 20 esecuzioni consecutive.
- GCC `-fanalyzer -Werror`, thread e shared-memory abilitati: CTest 1/1 superato.
- ThreadSanitizer, thread e shared-memory abilitati: CTest 1/1 superato.
- Build `WINAPI_DISPLAY=ON` con `-Werror`: CTest 1/1 superato.
- Build `WINAPI_X11=ON` con `-Werror`: CTest 1/1 superato.

## Limiti / follow-up

- **MSVC/Windows non è stato eseguito in questo ambiente**; non dichiarare certificata la build nativa.
- La UI X11 è stata compilata e testata sui percorsi senza display (ad esempio modalità di pulsanti non supportate); non è stata eseguita una prova interattiva contro un X server reale.
- `GetLogicalDriveStrings` usa ancora la scoperta dei mount del backend POSIX, quindi l'inventario drive è una proiezione e non un equivalente completo dei volumi Windows.
- `SetFileAttributes` rifiuta i symlink invece di modificare attributi del link, perché il backend non dispone di un'operazione portabile equivalente.
- `GetFileAttributes`/`GetFileAttributesEx` continuano a usare `stat()` e quindi seguono i symlink per la lettura dei metadati; la rappresentazione Win32 dei reparse point resta incompleta.
- `WCHAR` è definito in termini di `wchar_t` dell'host; il layout/encoding wide non è ancora un ABI Win32 a 16 bit uniforme su piattaforme POSIX.
- L'assenza di `GLOB_PERIOD` su libc non GNU richiede un fallback dedicato per garantire l'enumerazione dei dot-file.

## Follow-up — durability di MOVEFILE_WRITE_THROUGH (2026-10-20)

L'audit successivo ha trovato che `MoveFileEx(..., MOVEFILE_WRITE_THROUGH)` sincronizzava soltanto il parent della destinazione. Un rename tra directory diverse modifica due directory: la destinazione aggiunge un nome, la sorgente ne rimuove uno. Ora vengono sincronizzate entrambe le directory dopo il commit. Se la seconda sincronizzazione fallisce, l'API restituisce errore ma non tenta un rollback pericoloso: lo spostamento potrebbe essere già avvenuto. Aggiunto test di regressione con sorgente e destinazione in directory diverse. Build standard, ASan+UBSan, analyzer, TSan, DISPLAY e X11 tutte superate (CTest 1/1).

## Follow-up — attributo ARCHIVE non mutabile

`SetFileAttributes` accettava il bit `FILE_ATTRIBUTE_ARCHIVE` nella maschera supportata ma non aveva un backing store per modificarlo: sui file regolari l'attributo è sintetizzato dalla proiezione POSIX. Ora il backend confronta lo stato richiesto con quello corrente e restituisce `ERROR_NOT_SUPPORTED` se il chiamante chiede di impostare o cancellare un bit non persistibile (compreso `FILE_ATTRIBUTE_NORMAL` su file ordinari). Il test verifica che la richiesta fallisca e che l'attributo proiettato resti invariato. Tutte le sei configurazioni di build e CTest sono state ripetute e superate.


## API expansion — SetFileInformationByHandle / FileIdInfo (2026-10-20)

- `SetFileInformationByHandle(FileEndOfFileInfo)` validates size and signed offset before `ftruncate`, requires write access and preserves the current shared file offset.
- `SetFileInformationByHandle(FileBasicInfo)` changes only last-access/last-write times; zero means unchanged. Unsupported creation/change timestamps and Windows attribute mutations are rejected before any mutation.
- `GetFileInformationByHandleEx(FileIdInfo)` exposes POSIX device/inode-derived identity with explicit caveat that it is not a native NTFS 128-bit identifier.
- Regression coverage includes ABI sizes, short-buffer output preservation, negative EOF, invalid handles/classes, readonly handles, timestamp round-trip and pointer preservation.
- Remaining unsupported classes fail explicitly rather than returning guessed metadata. Native MSVC/Windows compilation remains unverified in this environment.

- Follow-up API: `GetFileInformationByHandleEx(FileAlignmentInfo)` restituisce `AlignmentRequirement == 0` per file regolari, coerente con I/O POSIX a granularità di byte. Il layout a 4 byte e il rifiuto del buffer corto sono coperti da regressione.


## Follow-up C-prio19 — query nome del file tramite handle

`GetFileInformationByHandleEx` espone ora `FileNameInfo` e `FileNormalizedNameInfo` su Linux tramite `/proc/self/fd`. La conversione del nome in UTF-16 è validata e non sostituisce caratteri in modo silenzioso: path con UTF-8 valido, buffer insufficienti, file unlinkati e nomi POSIX contenenti byte non UTF-8 sono coperti da test. Il percorso viene letto sotto il lease dell'handle; il supporto richiede Linux `/proc` e resta limitato a file regolari.
