# winapi – stato della sanificazione

## Fase 1 — pulizia strutturale (completata)
- Albero ripulito: sorgenti in `src/`, header pubblici in `include/`, UTF-8 + LF, niente file IDE/binari.
- Compilazione C11/CMake e opzioni per moduli opzionali.
- Correzioni iniziali a tipi base, last-error thread-local, tempo, memoria e percorsi.

## Fase 2 — primo round di correzioni (parzialmente completata)
- Correzioni circoscritte a `wthreads.h`, enumerazione directory e alcune operazioni su thread/mapping.
- CTest per il test di fumo.
- La compilazione dei moduli thread e memoria condivisa non implica che la loro semantica sia corretta: sono ancora da riprogettare/verificare.

## Fase A — inventario dei contratti (completata come documentazione)
- Aggiunto `API_INVENTORY.md` con catalogo delle API, moduli, opzioni, rischi e piano di test.
- Registrati i limiti generali: target Linux/POSIX, assenza di compatibilità ABI con eseguibili Windows, supporto Unicode incompleto e macro libc da rivedere.
- Eseguite build default e build con `WINAPI_THREADS=ON` + `WINAPI_SHMEM=ON`; `winapi_smoke` passa in entrambe (1/1).

## Prossimo lavoro
1. Fase B: audit e test di file/percorsi/handle di ricerca.
2. Fase C: riprogettazione del modello handle e sincronizzazione; non aggiungere funzionalità prima di avere test deterministici.
3. Fase D: audit della memoria condivisa.
4. Fase E: test di regressione e sanitizer.
5. Fase F: integrazione BabyC con registrazione esplicita dei simboli, non tramite macro libc globali.

## Regola di prudenza
Le API non coperte da test specifici sono sperimentali. Le API thread/memoria condivisa non devono essere considerate affidabili per uso concorrente fino al completamento delle fasi C–E.

## Fase B — audit file, percorsi e handle di ricerca

Interventi effettuati in questo round:
- `update_drive_list`: controlla il fallimento di `setmntent`, usa formattazione limitata per i percorsi e gestisce i fallimenti di allocazione delle voci disco.
- `FindFirstFile`/`FindNextFile`/`FindClose`: validano gli argomenti, gestiscono gli errori di allocazione, convertono i percorsi Windows riconosciuti, restituiscono il solo nome del file e inizializzano i dati di attributi/dimensione disponibili. Il cursore ora parte dal secondo risultato dopo `FindFirstFile`.
- `GetLogicalDriveStrings`: non scrive più oltre il buffer e calcola la dimensione necessaria prima di copiare la lista multistringa.
- `GetDiskFreeSpaceEx`: supporta il percorso nullo come directory corrente e rispetta i puntatori di output opzionali.
- `GetCurrentDirectory`: evita la scrittura fuori limite quando il buffer è troppo piccolo e restituisce la dimensione richiesta secondo il contratto Win32.
- Test di regressione per enumerazione file, output opzionali e buffer insufficiente.

Limiti registrati al termine della Fase B (stato storico, aggiornato nelle fasi successive): il modello dei mount point/drive letter è euristico; conversioni percorso e `_makepath`/`_splitpath` richiedono ancora un audit specifico; le date `FILETIME` di `WIN32_FIND_DATA` non sono ancora convertite dai timestamp POSIX. La sincronizzazione del registro Win32 di `FindFirstFile` è stata aggiunta nella C47; restano necessari test concorrenti dedicati. Non considerare ancora queste API pienamente compatibili con Windows.

Verifiche aggiuntive della Fase B: test CTest superati anche con AddressSanitizer e UndefinedBehaviorSanitizer abilitati (`-fsanitize=address,undefined`, leak detection attiva). I test sono ancora test mirati di regressione, non una suite completa di compatibilità Win32.

## Round successivo — audit conversioni percorsi

Sono state irrigidite le conversioni `win2posix_path` e `posix2win_path`, aggiunti controlli sugli input nulli e sulle allocazioni e corretto `_splitpath` per input nullo e gestione del prefisso drive. I test coprono separatori misti e scomposizione di un percorso Windows. La mappatura drive/mountpoint resta deliberatamente euristica; le API legacy senza dimensione del buffer richiedono ancora cautela.

## Fase C — primo round di audit handle

- Aggiunte verifiche di appartenenza al registro prima di dereferenziare handle in alcune API di evento, thread e mutex.
- Corrette verifiche di argomenti/output nulli e alcuni casi di ownership del mutex.
- Aggiunti test per handle invalidi, evento manual-reset, doppia chiusura e rilascio del mutex da parte del proprietario.
- Creato `HANDLE_AUDIT.md` con il piano per un registro unificato e i rischi non risolti.

**Avvertenza:** il registro corrente resta costituito da liste globali non sincronizzate; la validazione non è atomica rispetto a `CloseHandle`. Questa fase non rende sicuro l'uso concorrente. `WaitForMultipleObjects`, eventi nominati e ciclo di vita dei thread richiedono ancora una riprogettazione strutturale.

## Round C1 — correttezza locale degli handle

- `CreateMutex`: gestione dell'errore di allocazione della voce di registro e rollback delle risorse.
- `CloseHandle`: non riporta successo quando la distruzione del mutex fallisce; un mutex occupato rimane registrato.
- `WaitForSingleObject` con timeout zero su mutex occupato restituisce `WAIT_TIMEOUT`, non `WAIT_ABANDONED`.
- Aggiunto test di regressione per la chiusura di un mutex ancora posseduto.
- **Non risolto:** registry globale non sincronizzato e lifetime pinning degli oggetti. Vedere `HANDLE_AUDIT.md`.

### Round C2 — attese e test anti-regressione

`WaitForMultipleObjects` ora valida gli argomenti e non crea più un thread
helper per ciascun handle. Le attese sugli eventi gestiscono lo stato sotto
mutex e normalizzano le deadline. Sono stati aggiunti test per wait-any,
wait-all su eventi manual-reset, timeout, auto-reset e input non validi.
Restano esplicitamente aperti l'atomicità Win32 del wait-all e la sicurezza
contro `CloseHandle` concorrente con operazioni in corso.

### Round C3 — signaling e attese

`WaitForMultipleObjects` è stato riscritto senza polling, con condition variable
comune, timeout monotoni e pinning degli oggetti in attesa. Aggiunti test per
segnalazione asincrona, wait-all bloccante, durata degli handle durante wait e
codice di uscita dei thread. Le primitive non implementabili in sicurezza
(PulseEvent, sospensione/ripresa/terminazione forzata dei thread, eventi nominati)
restituiscono ora esplicitamente `ERROR_CALL_NOT_IMPLEMENTED` invece di simulare
comportamenti errati.

### Round C3 — signaling e attese

`WaitForMultipleObjects` è stato riscritto senza polling, con condition variable
comune, timeout monotoni e pinning degli oggetti in attesa. Aggiunti test per
segnalazione asincrona, wait-all bloccante, durata degli handle durante wait e
codice di uscita dei thread. Le primitive non implementabili in sicurezza
(PulseEvent, sospensione/ripresa/terminazione forzata dei thread, eventi nominati)
restituiscono ora esplicitamente `ERROR_CALL_NOT_IMPLEMENTED` invece di simulare
comportamenti errati.

## Round C4 — chiusura dei difetti principali di handle/wait

- Handle evento/thread/mutex convertiti in token opachi non riutilizzati, evitando che un puntatore obsoleto venga accidentalmente riconosciuto dopo il riuso dell'allocazione.
- Condvar globale con clock deadline coerente, signaling senza polling, test broadcast multi-waiter.
- Corretto il valore di ritorno di wait-all in presenza di mutex abbandonato a indice diverso da zero.
- `GetCurrentThreadId` restituisce ID DWORD assegnati dalla libreria; flag `CreateThread` non supportati rifiutati esplicitamente.
- Vedi `HANDLE_AUDIT.md` per limiti residui e contratto attuale.


## Round C5 — contratti thread e test anti-regressione

- `thread.exitCode` ora usa `DWORD`, conservando tutti i 32 bit del valore restituito dalla routine.
- `CreateThread` valorizza `LastError` per errori noti di inizializzazione/configurazione/creazione.
- `SetThreadPriority` restituisce esplicitamente `ERROR_CALL_NOT_IMPLEMENTED` per priorità valide ma non emulabili, anziché riportare successo senza modificare la schedulazione. Handle e priorità non validi sono distinti.
- Gli errori della condition variable producono `WAIT_FAILED` e `ERROR_GEN_FAILURE`.
- Test anti-regressione aggiunti per codici DWORD ad alto bit, `STILL_ACTIVE`, chiusura di thread ancora attivo, stack size invalida, priorità e handle duplicati nelle wait.

La fase thread non è ancora completa: manca una mappatura documentata delle priorità e restano non implementate terminazione/sospensione/ripresa forzata. Le build e i test non dimostrano da soli compatibilità Win32 completa.

## Round C6 — stress concorrente e deadline delle attese

- Aggiunto un test concorrente con 8 thread, ognuno dei quali crea/segnala/attende/chiude eventi ripetutamente e contende un mutex condiviso; il contatore finale verifica che nessun aggiornamento protetto dal mutex venga perso.
- Corretto il bordo di timeout in `WaitForMultipleObjects`: dopo `ETIMEDOUT`, la funzione rivaluta prima le condizioni sotto il lock comune, così una condizione già soddisfatta non viene scartata soltanto per l'esito della wait POSIX.
- La scadenza viene controllata usando lo stesso clock della condition variable; gli errori di `clock_gettime` diventano `WAIT_FAILED` con `ERROR_GEN_FAILURE`.
- Build standard e completa con `-Werror`, ASan/UBSan con leak detection e ThreadSanitizer superati; il test completo è stato ripetuto 50 volte.

Il test di stress migliora la copertura ma non sostituisce test formali di linearizzabilità né dimostra assenza assoluta di race. La gestione dei token e delle attese resta un'implementazione compatibile per un sottoinsieme delle API Win32.

## Round C7 — primo hardening della memoria condivisa

- Sostituita la gestione permissiva di `CreateFileMapping`/`OpenFileMapping`/`MapViewOfFile`/`UnmapViewOfFile` con controlli su nomi, dimensioni, overflow, offset allineati e intervalli della vista.
- Corretto il difetto per cui `OpenFileMapping` controllava un descrittore ma non lo assegnava a `mapfile->fd`; ora usa `fstat` e memorizza la dimensione reale.
- Le liste di mapping e viste sono protette da un mutex dedicato; `UnmapViewOfFile` confronta gli indirizzi come puntatori, non tramite `strcmp` su memoria arbitraria.
- Gli errori di allocazione, `ftruncate`, `shm_open`, `mmap` e `munmap` vengono gestiti senza dereferenziare allocazioni nulle.
- Il backend attuale accetta mapping di memoria condivisa anonimi/nominati creati con `INVALID_HANDLE_VALUE`. I mapping basati su file ordinari sono rifiutati esplicitamente: non erano implementati in modo corretto.
- Aggiunto un test di smoke per scrittura/lettura, limiti della vista, offset fuori intervallo e doppio unmap.

**Limiti noti ancora aperti:** `CloseHandle` ora riconosce gli handle di mapping quando è abilitato il backend thread e rifiuta la chiusura se esistono viste attive; dopo la chiusura rimuove la registrazione e chiude il descrittore. Non è ancora implementato un conteggio di riferimenti per handle distinti sullo stesso oggetto e la semantica di vita/unlink degli oggetti nominati non è ancora equivalente a Windows. Il backend usa `shm_open` POSIX e richiede nomi senza `/`. La condivisione tra processi è possibile per gli oggetti nominati, ma deve ancora essere verificata con test multiprocesso e crash del creatore. Non considerare questa fase una compatibilità Win32 completa.


## Round C8 — chiusura dei mapping

- `CloseHandle` delega il riconoscimento degli handle mapping a `wshmem_close_handle`, mantenendo separati i lock del sottosistema thread e della memoria condivisa.
- La chiusura viene rifiutata con `ERROR_BUSY` se una vista ancora mappata fa riferimento all'oggetto. Dopo `UnmapViewOfFile`, la chiusura rimuove il mapping dal registro e chiude il file descriptor.
- Aggiunte regressioni per chiusura durante vista attiva, chiusura valida e doppia chiusura.
- Build standard e build completa thread+shmem con `-Werror` e CTest superate.

**Limiti C8:** non è ancora gestito il refcount per più handle allo stesso oggetto nominato; non si esegue ancora unlink automatico degli oggetti POSIX, per non invalidare prematuramente aperture da processi distinti. La verifica multiprocesso resta da fare. `CloseHandle` è definita nel modulo thread e quindi richiede `WINAPI_THREADS=ON` in questa configurazione.

## Round C9 — verifica interprocesso dei mapping nominati

- Aggiunto un test multiprocesso: il processo figlio rimuove la registrazione locale ereditata dal `fork`, apre nuovamente il mapping nominato con `OpenFileMapping`, scrive in una vista e chiude le risorse; il processo padre verifica il byte modificato tramite una nuova vista.
- Il test controlla gli esiti di `fork`/`waitpid` e segnala separatamente gli errori di apertura, mapping, unmap e chiusura nel figlio.
- Build standard e build completa con `WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON` e `-Werror` superate; CTest superato in entrambe.

**Limiti residui:** il test usa `fork` e non un processo avviato tramite `exec`, quindi non dimostra ancora il comportamento con uno spazio di indirizzamento inizializzato da zero né la terminazione inattesa del creatore. Il refcount di handle distinti per lo stesso oggetto e la politica automatica di `shm_unlink` restano non implementati. Non dichiarare equivalenza completa con Win32.

## Round C10 — handle distinti per aperture ripetute

Rimosso l'aliasing del puntatore interno quando `CreateFileMapping` o `OpenFileMapping` riaprono un nome già presente nel processo. Ogni chiamata riuscita crea ora un wrapper locale e un descrittore propri, così la chiusura di un handle non invalida un altro handle ottenuto con una chiamata separata. I nomi POSIX restano intenzionalmente senza unlink automatico: il conteggio dei riferimenti tra processi non è ricostruibile in modo affidabile con il solo registro locale.

**Limiti C10:** non è ancora implementata la semantica completa di sicurezza/accesso Win32; resta da verificare l'apertura da un processo indipendente via `exec` e la politica di pulizia dei nomi POSIX persistenti.


## Round C11 — test con `exec` e terminazione inattesa

- Esteso `winapi_smoke` con una modalità figlio dedicata, avviata tramite `fork` + `exec`, che apre il mapping nominato da uno spazio di indirizzamento nuovo e verifica la condivisione senza ereditare il registro in memoria del padre.
- Il figlio scrive un byte condiviso e termina intenzionalmente senza chiamare `UnmapViewOfFile` o `CloseHandle`; il padre attende il processo e verifica che la modifica sia visibile, coprendo il cleanup delle risorse locali affidato al kernel alla terminazione del processo.
- Il test lascia al padre la responsabilità di chiudere l'handle e rimuovere il nome POSIX temporaneo. Questo è cleanup esplicito nel test, non una politica automatica della libreria.
- Build standard e build completa (`WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`) con `-Werror` e CTest superate.

**Limiti residui:** non è stato implementato `shm_unlink` automatico nel backend: non esiste ancora un protocollo multiprocesso affidabile per decidere quale processo sia l'ultimo proprietario logico. Il test dimostra l'apertura via `exec` e la visibilità dei dati, ma non dimostra da solo assenza di leak di nomi in crash arbitrari né compatibilità completa Win32.


## Fase C12 — audit cleanup POSIX e mapping anonimi

- I mapping senza nome usavano un nome POSIX temporaneo che rimaneva nel namespace dopo la chiusura: ora il nome viene rimosso con `shm_unlink` subito dopo `ftruncate`, lasciando vivo l'oggetto attraverso il descrittore aperto.
- Aggiunto un test di regressione per creazione, mappatura, accesso, unmap e chiusura di un mapping anonimo.
- I mapping nominati restano deliberatamente persistenti nel namespace POSIX: la rimozione automatica alla chiusura di un handle locale non è sicura in presenza di altri processi. I test li rimuovono esplicitamente.
- Limite: la semantica di sicurezza/accesso e il comportamento di `CreateFileMapping` su un nome già esistente richiedono ulteriore confronto con Win32.


## Round C13 — semantica dei nomi esistenti e dimensioni

- Corretto `CreateFileMapping`: se il nome esiste già, la chiamata restituisce un nuovo handle locale all'oggetto esistente e conserva la dimensione originale, anche quando la dimensione richiesta dalla nuova chiamata è maggiore o minore. In caso di successo imposta `ERROR_ALREADY_EXISTS`.
- La dimensione registrata del mapping usa `uint64_t`, evitando la troncatura implicita su piattaforme dove `unsigned long` è a 32 bit. `MapViewOfFile` controlla anche che la vista completa sia rappresentabile in `size_t`.
- Resa più deterministica la gestione degli errori di `OpenFileMapping`, evitando di interpretare `errno` residuo quando fallisce `fstat`.
- Aggiunti test per riapertura tramite `CreateFileMapping` con dimensioni discordanti, `ERROR_ALREADY_EXISTS`, limiti della dimensione originale, dimensione zero e nomi POSIX non validi.

**Limiti residui:** access mask, `flProtect`, ereditarietà e sicurezza non sono ancora emulati integralmente. La libreria usa oggetti POSIX nominati e richiede una politica esterna di cleanup per i mapping nominati.


## Fase C14 — access mask e protezioni dei mapping

- `CreateFileMapping` accetta soltanto `PAGE_READONLY` e `PAGE_READWRITE`; le protezioni execute, guard, no-access e copy-on-write restano esplicitamente non supportate. Gli attributi di sicurezza/ereditarietà non vengono ignorati silenziosamente.
- `OpenFileMapping` e `MapViewOfFile` validano le access mask; `FILE_MAP_COPY` e diritti non implementati sono rifiutati. I diritti concessi al singolo handle vengono verificati prima di creare una vista.
- La protezione dei mapping nominati è riflessa nei permessi POSIX dell'oggetto condiviso; il modo viene controllato anche dopo l'apertura, per evitare che processi con privilegi elevati aggirino il controllo logico della libreria.
- Test di regressione per mapping read-only, rifiuto delle viste scrivibili, access mask non supportate e protezioni non implementate.
- Limite residuo: i descrittori di sicurezza Windows e le ACL non sono emulati; il modello POSIX dei permessi è solo un'approssimazione e il supporto a mapping basati su file resta escluso.

## Fase C15 — robustez dei percorsi di errore della memoria condivisa

- Separati i controlli `fstat` dagli invarianti sulla dimensione, evitando di leggere `errno` dopo un controllo logico che non ha necessariamente impostato un errore di sistema.
- `MapViewOfFile` verifica anche che l'offset sia rappresentabile da `off_t`, oltre a rispettare l'allineamento richiesto dal sistema.
- `wshmem_close_handle` ritira la registrazione del mapping quando non esistono viste attive anche se `close(2)` segnala un errore: su Linux un `close` interrotto può aver già liberato il descrittore, e mantenere l'handle registrato rischierebbe di riutilizzare un fd non più valido.
- Aggiunti test negativi per handle nullo, access mask non supportata, offset disallineato, vista fuori dai limiti, unmap nullo e tentativo di chiusura con vista attiva.
- Le libc duplicate non sono state modificate.


## Fase C16 — audit dei mapping basati su file ordinari

- Verificato il contratto del backend: `CreateFileMapping` supporta solo il sentinel `INVALID_HANDLE_VALUE` per i mapping backed da memoria condivisa POSIX; `NULL` e handle arbitrari vengono rifiutati con `ERROR_INVALID_HANDLE`.
- Aggiunti test negativi espliciti per `hFile == NULL` e per un handle opaco arbitrario, per impedire regressioni che possano trattare un handle Win32 come un file descriptor POSIX.
- Non è stato introdotto supporto file-backed: questa libreria non espone attualmente una `CreateFile` con un contratto di handle POSIX registrato e verificabile, quindi non esiste una conversione sicura e portabile da `HANDLE` a `fd`. Aggiungere il mapping file-backed senza prima progettare tale contratto rischierebbe di dereferenziare o chiudere handle non appartenenti al backend.
- Prossimo prerequisito per file-backed: definire un registro di handle file posseduti dalla libreria, access mode, ownership del descrittore, lifetime concorrente e comportamento di `CloseHandle`; poi integrare `CreateFileMapping`/`MapViewOfFile` con `fstat` e `mmap`.
- Build standard e completa con `-Werror` e CTest superate.

Il supporto file-backed resta esplicitamente non implementato; non dichiarare compatibilità Win32 completa.

## Fase C17 — contratto degli handle file

È stato documentato il contratto necessario per introdurre handle file WinAPI opachi. Un test di regressione verifica che un fd POSIX grezzo passato come `HANDLE` venga rifiutato senza essere chiuso o consumato. I mapping basati su file ordinari restano intenzionalmente non supportati finché non esiste un registro file-handle con ownership esplicita.

## C18 — file-handle registry foundation

Added a minimal `CreateFile` for regular files, with opaque process-wide handle
tokens, a mutex-protected registry, ownership-aware `CloseHandle`, basic access
rights and creation dispositions. Unsupported sharing, security attributes,
template handles, special flags, and wide-character paths are rejected
explicitly. This phase intentionally does not add ReadFile/WriteFile or file-
backed mappings; the descriptor bridge needs pinning/refcount semantics before
mapping code may safely use it concurrently with `CloseHandle`.

## C19 — synchronous file I/O

- Added synchronous `ReadFile` and `WriteFile` for regular-file handles created
  by `CreateFile`; `OVERLAPPED` requests are explicitly unsupported.
- Added operation leases and a condition variable so `CloseHandle` prevents new
  operations and waits for active operations before closing the POSIX descriptor.
- Added regression coverage for write/read round trips, EOF, access denial,
  null byte-count output, unsupported overlapped I/O, and invalid handles.

## Fase C20 — posizionamento e dimensioni dei file

Aggiunte le API `SetFilePointerEx`, `SetFilePointer`, `GetFileSizeEx` e
`GetFileSize` per gli handle creati da `CreateFile`. Le operazioni riutilizzano
il lease degli handle e sono serializzate per singolo handle tramite un mutex
I/O, così letture, scritture e seek non si sovrappongono modificando
inaspettatamente il puntatore condiviso. Le distanze sono validate rispetto a
`off_t`; i percorsi di errore impostano `LastError` e le API a 64 bit sono la
via preferita per file di grandi dimensioni.

Test aggiunti per dimensione a 32/64 bit, seek assoluto/relativo/fine file,
seek a offset superiori a 4 GiB, seek negativo, metodo non valido e handle
chiuso. Le modalità asincrone e le semantiche complete Win32 rimangono fuori
ambito.

## C21 — primi mapping basati su file ordinari

- `CreateFileMapping` accetta ora un handle opaco valido creato da `CreateFile`, oltre al sentinel `INVALID_HANDLE_VALUE` già usato per i mapping anonimi.
- Aggiunto `wfile_duplicate_fd`: duplica il descrittore sotto un operation lease e lo restituisce con una durata indipendente dall'handle file originale. La chiusura concorrente non può invalidare la duplicazione.
- I mapping file-backed supportano `PAGE_READONLY` e `PAGE_READWRITE`, dimensione zero (dimensione corrente del file) e dimensione esplicita. Se la dimensione richiesta supera il file, l'estensione è consentita solo quando l'handle sottostante è scrivibile.
- `MapViewOfFile` riutilizza i controlli di accesso, allineamento e limiti già esistenti; le viste sono `MAP_SHARED`, quindi le modifiche sono condivise con il file.
- I nomi per mapping file-backed sono rifiutati con `ERROR_CALL_NOT_IMPLEMENTED`: il protocollo di metadati multiprocesso per associare un nome a un file reale non è ancora implementato e non va confuso con gli oggetti POSIX shm.
- Test aggiunti per lettura/scrittura condivisa, chiusura dell'handle file originale mentre il mapping resta attivo, estensione del file, mapping read-only e rifiuto dei nomi non supportati.
- Build standard e build completa (`WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`) con `-Werror` e CTest superate.

**Limiti residui:** non sono supportati i mapping file-backed nominati, `PAGE_WRITECOPY`/`FILE_MAP_COPY`, protezioni execute, attributi di sicurezza e l'intera semantica Win32. L'estensione di un file modifica il file sottostante ed è irreversibile alla chiusura del mapping.

## C22 — hardening dei mapping file-backed

- L'estensione del file sottostante è ora consentita soltanto per `PAGE_READWRITE` e con un handle file effettivamente scrivibile. Un handle aperto in lettura/scrittura non causa più l'estensione implicita se il mapping richiesto è `PAGE_READONLY`.
- `MapViewOfFile` interroga il file con `fstat` prima di creare una vista. Se il file è stato troncato esternamente rispetto alla dimensione registrata nel mapping, la nuova vista viene rifiutata con `ERROR_FILE_INVALID`, evitando il caso noto di una vista nuova che potrebbe generare `SIGBUS` oltre EOF.
- Gli errori `mmap` più comuni sono tradotti in errori WinAPI specifici (`ERROR_NOT_ENOUGH_MEMORY`, `ERROR_ACCESS_DENIED`, `ERROR_INVALID_PARAMETER`, `ERROR_FILE_INVALID`) invece di collassare tutti in `ERROR_GEN_FAILURE`.
- Test aggiunti per file vuoto, mapping read-only con dimensione maggiore del file (nessuna estensione collaterale), mapping a dimensione zero su file vuoto e troncamento esterno prima di una nuova vista.
- Limite: il controllo `fstat` non può eliminare una race con un processo esterno che tronchi il file dopo il controllo o mentre una vista è attiva. I chiamanti devono evitare modifiche incompatibili alla dimensione del file mentre esistono viste.
- Build standard e completa con `-Werror` e CTest superate.

## C23 — concorrenza del ciclo di vita delle viste

- Audit del percorso `MapViewOfFile` / `UnmapViewOfFile` / `CloseHandle`: registro mapping e viste protetti dallo stesso mutex privato; creazione e rimozione delle viste e ritiro del mapping sono serializzati.
- Aggiunto stress test con quattro thread che creano, scrivono e distruggono viste sullo stesso mapping; dopo il join, la chiusura resta correttamente rifiutata con `ERROR_BUSY` finché la vista di controllo è attiva.
- La protezione del registro non può sincronizzare accessi diretti del chiamante alla memoria mappata con un `UnmapViewOfFile` eseguito da un altro thread: l'applicazione deve coordinare l'uso dei puntatori alle viste.
- Resta aperta la race con troncamenti esterni del file e non è stato introdotto il protocollo per mapping file-backed nominati.

## C24 — chiusura concorrente degli handle file e ThreadSanitizer

- Aggiunto un test di regressione dedicato alla chiusura concorrente di un handle file mentre un worker ripete `SetFilePointerEx` e `ReadFile` sullo stesso handle.
- Il test verifica che `CloseHandle` attenda la fine delle operazioni già ammesse, impedisca nuove operazioni e consenta al worker di terminare con `ERROR_INVALID_HANDLE` senza usare un descrittore riciclato o un mutex distrutto.
- Build e CTest superati in configurazione standard e con `WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`.
- Build e CTest superati anche con ThreadSanitizer (`-fsanitize=thread`) nel runtime corrente.
- Il test resta focalizzato sul contratto interno dell'handle; non prova a sincronizzare puntatori a viste mappate usati direttamente dall'applicazione, né elimina i limiti dovuti al troncamento esterno dei file.

## C25 — concorrenza multi-worker e audit dell'ordine dei lock

- Esteso il test di chiusura concorrente del file da un singolo worker a quattro worker che condividono lo stesso handle. Il test esercita più operazioni ammesse o in attesa sul mutex per-handle mentre `CloseHandle` ritira l'handle dal registro.
- Verificato l'ordine dei lock del backend file: `g_file_handles_lock` viene rilasciato prima di attendere `entry->io_lock`; `wfile_end_io` rilascia `io_lock` prima di riacquisire il lock del registro; la chiusura attende `active_ops` con `pthread_cond_wait`, che rilascia temporaneamente il lock del registro.
- Nessun cambiamento al contratto ABI o alle libc duplicate.
- Il test copre la chiusura concorrente con worker multipli, ma non dimostra l'assenza di tutte le race in tutta la libreria; l'uso concorrente di una vista dopo `UnmapViewOfFile` resta responsabilità del chiamante.

## C26 — concorrenza tra creazione/rimozione viste e chiusura mapping

- Esteso lo stress test delle viste: mentre quattro worker creano e distruggono viste, un quinto thread prova ripetutamente `CloseHandle` sullo stesso mapping.
- Una vista di controllo resta attiva per tutta la prova; ogni chiusura concorrente deve quindi fallire con `ERROR_BUSY` senza ritirare il mapping. Il test verifica tutte le 1200 chiamate e controlla che il mapping resti utilizzabile fino alla rimozione della vista di controllo.
- Il test è progettato per esercitare contemporaneamente l'accesso ai registri delle viste e del mapping, senza introdurre una race nei puntatori di memoria: i worker usano solo le viste di cui possiedono il ciclo di vita.
- Nessuna modifica al comportamento pubblico o alle libc duplicate.
- Limiti residui: il mutex interno non sincronizza il codice chiamante che usa un puntatore dopo `UnmapViewOfFile`; le modifiche esterne alla dimensione di un file mappato possono ancora invalidare viste già attive.


## C27 — atomicità dei percorsi di errore nei mapping file-backed

- In `CreateFileMapping` per file ordinari, le strutture locali di bookkeeping (`mapfile` e voce del registro) vengono ora allocate prima di `ftruncate`. Se un'allocazione fallisce, la chiamata restituisce `ERROR_NOT_ENOUGH_MEMORY` senza aver già esteso il file come effetto collaterale.
- I percorsi in cui l'estensione non è consentita o `ftruncate` fallisce liberano entrambe le strutture e chiudono il descrittore duplicato prima di restituire l'errore.
- La modifica non cambia l'ABI né il contratto delle API; non modifica le libc duplicate.
- Verificate build e CTest con `-Werror` sia nella configurazione standard sia con `WINAPI_THREADS=ON` e `WINAPI_SHMEM=ON` (1/1 test superato per configurazione).
- Limite del test: non è presente un fault-injection allocator, quindi il ramo specifico di fallimento di `calloc` non è forzato deterministicamente dalla suite; la correzione è una modifica di ordine delle operazioni e pulizia esplicita dei percorsi d'errore.


## C28 — cleanup robusto dei nomi POSIX shared-memory

- Centralizzata la rimozione dei nomi POSIX in `unlink_posix_name()`: `EINTR` viene ritentato e `ENOENT` è trattato come successo, perché il nome è già assente e la pulizia è quindi completa.
- Usata la funzione sia per rimuovere immediatamente i nomi dei mapping anonimi sia nei percorsi di rollback dopo errori di creazione/allocazione. Questo evita di considerare fallita una pulizia già completata e gestisce esplicitamente l'interruzione della syscall.
- Non cambia l'ABI né il comportamento delle libc duplicate. I fallimenti di `shm_unlink` diversi da `EINTR`/`ENOENT` restano errori: il sistema operativo può impedire la rimozione del nome e la libreria non può garantire la pulizia in quel caso.
- Build con `-Werror` e CTest superati in configurazione standard e con `WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON` (1/1 per configurazione).
- Limite: non è stato introdotto un wrapper syscall iniettabile, quindi `EINTR` e gli errori di `shm_unlink` non sono simulati deterministicamente dalla suite.

## C29 — verifica dei diritti per handle di mapping

Aggiunti test di regressione che aprono un oggetto `PAGE_READWRITE` tramite `OpenFileMapping(FILE_MAP_READ)` e verificano che lo specifico handle consenta viste in lettura ma rifiuti `FILE_MAP_WRITE` e `FILE_MAP_ALL_ACCESS` con `ERROR_ACCESS_DENIED`. Aggiunto anche un controllo che una maschera contenente `FILE_MAP_COPY`, non supportata, venga rifiutata con `ERROR_INVALID_PARAMETER` anziché accettata o ampliata implicitamente.

La verifica distingue i diritti concessi al singolo handle dalla protezione dell'oggetto POSIX sottostante. Il test non pretende di coprire ACL Windows, security descriptor o copy-on-write, che restano non implementati.

## C30 — creazione concorrente e lifetime indipendente dei mapping nominati

- Aggiunto un test concorrente con sei thread che invocano `CreateFileMapping` sullo stesso nome. Una barriera basata su mutex/condition variable sincronizza l'avvio tramite signaling, senza polling.
- Il test verifica che una sola chiamata crei l'oggetto (`ERROR_SUCCESS`), le altre riportino `ERROR_ALREADY_EXISTS`, ogni chiamata restituisca un handle locale distinto e tutti gli handle possano creare una vista in lettura e venire chiusi indipendentemente.
- Il test rimuove esplicitamente il nome POSIX al termine e verifica la dimensione attesa tramite la possibilità di mappare tutti i 4096 byte.
- Nessuna modifica al runtime o all'ABI pubblica; le libc duplicate restano invariate.
- Limite importante: questa regressione copre la concorrenza tra thread nello stesso processo, serializzata dal mutex interno. Non dimostra l'atomicità dell'inizializzazione tra processi distinti: `shm_open(O_CREAT|O_EXCL)` rende atomica la creazione del nome, ma un altro processo può potenzialmente aprire l'oggetto prima che il creatore abbia completato `ftruncate`/`fchmod`. Risolvere quel caso richiede un protocollo esplicito di inizializzazione/metadata e viene lasciato come punto aperto, non dichiarato risolto.
- Build e CTest con `-Werror` superati in configurazione standard e con `WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON` (1/1 per configurazione).


## C31 — serializzazione interprocesso dell'inizializzazione dei mapping nominati

- Aggiunto un lock advisory POSIX per nome, acquisito prima di `shm_open()` nelle API `CreateFileMapping` (mapping di shared memory nominati) e `OpenFileMapping`. Il lock copre la creazione e l'inizializzazione (`ftruncate`/`fchmod`) oppure l'apertura e la verifica dell'oggetto, evitando che un processo cooperante osservi il nome mentre il creatore lo sta ancora inizializzando. Il mutex locale continua a proteggere i registri in-process.
- I lock sono file dedicati in `/tmp`, aperti con `O_NOFOLLOW`, `O_CLOEXEC`, permessi richiesti `0600` e verifica di file regolare e proprietario corrente. Si usa `flock`, che blocca e risveglia i concorrenti senza polling.
- I file di lock vengono intenzionalmente mantenuti: rimuoverli mentre altri processi potrebbero attendere sul medesimo inode permetterebbe la creazione di un secondo inode e quindi la divisione del dominio di lock. Questo introduce file di coordinamento persistenti in `/tmp`, limite documentato da rivalutare in una fase dedicata.
- Aggiunta una regressione con quattro processi exec distinti rilasciati da una pipe, che competono per creare lo stesso mapping; ogni processo verifica creazione/apertura, dimensione mappabile, vista e chiusura. La pipe funge da barriera, senza polling.
- Build standard e build con `WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`, entrambe con `-Werror`, superate; CTest 1/1 in ciascuna configurazione.
- Limiti: il protocollo sincronizza solo processi che usano questa implementazione; programmi esterni che chiamano direttamente `shm_open` possono ignorare il lock. Non sono implementate ACL/security descriptor Windows e il file di lock persistente richiede ulteriore valutazione operativa.

## C32 — recupero da inizializzazione interrotta dei mapping nominati

- Esteso il file di coordinamento per nome con un record privato contenente il nome esatto e uno stato `INITIALIZING`/`READY`. Lo stato viene scritto prima di tentare la pubblicazione di un nuovo oggetto POSIX e portato a `READY` solo dopo `ftruncate()` e `fchmod()`.
- Dopo aver acquisito il medesimo `flock`, il primo chiamante cooperante che trova un record `INITIALIZING` rimuove l'eventuale oggetto POSIX residuo e azzera lo stato prima di procedere. La morte del processo rilascia automaticamente il lock del kernel, evitando lock rimasti posseduti indefinitamente.
- Il descrittore del lock già acquisito viene riutilizzato per aggiornare lo stato, evitando una seconda apertura del percorso durante la sezione critica.
- Aggiunta una regressione deterministica che avvia un processo helper, scrive il record di inizializzazione, pubblica un oggetto a dimensione zero e termina senza pulizia. Il successivo `OpenFileMapping` deve recuperare e rifiutare il residuo; una nuova `CreateFileMapping` deve poi riuscire e produrre una vista valida.
- Limiti: il recupero copre la terminazione del processo, non garantisce la persistenza del record in caso di perdita improvvisa dell'alimentazione o crash del sistema; richiede che gli utilizzatori cooperino con il lock della libreria. Il layout del record resta un dettaglio privato di implementazione.

## C33 — I/O completo e validazione del record di coordinamento

- Introdotti helper interni `pread_full()` e `pwrite_full()` che gestiscono trasferimenti parziali e ritentano `EINTR`; una lettura prematuramente terminata o una scrittura a zero byte viene trattata come errore.
- Il record del lock viene ora validato rispetto a dimensione esatta, magic, terminazione della stringa, nome richiesto e valori di stato ammessi (`INITIALIZING`/`READY`). Un file non vuoto troncato, malformato o non corrispondente al nome provoca un errore chiuso (`ERROR_GEN_FAILURE` tramite API), invece di essere interpretato implicitamente come record assente.
- La prima scrittura del record inizializza direttamente lo stato richiesto, evitando la finestra in cui un record nuovo sarebbe scritto temporaneamente come `READY` prima di essere portato a `INITIALIZING`.
- Aggiunto un test di regressione che prepara deliberatamente un file di coordinamento troncato, verifica il rifiuto di `OpenFileMapping` e rimuove il file di test. Le libc duplicate e l'ABI pubblica non sono state modificate.
- Limite: non è introdotto fault injection per forzare in modo deterministico `EINTR` o scritture parziali del filesystem; i helper gestiscono questi risultati per costruzione, mentre la suite verifica il caso di record persistito corrotto.


## C34 — collisione dei nomi e sicurezza dei file di coordinamento

- Il percorso del lock usa due hash indipendenti a 64 bit del nome del mapping, riducendo ulteriormente la probabilità di collisione accidentale. Il record continua a contenere il nome completo e viene confrontato esattamente: una collisione residua non autorizza la condivisione del lock/record fra nomi diversi, ma viene rifiutata in modo conservativo.
- Rafforzata la validazione del file di lock: oltre a file regolare e proprietario corretto, vengono richiesti `st_nlink == 1` e assenza di permessi per gruppo/altri. Restano `O_NOFOLLOW` e `O_CLOEXEC`.
- Aggiornati i test che costruiscono i file di coordinamento per usare il nuovo percorso. Aggiunta una regressione che crea un lock con permessi di gruppo e verifica il rifiuto.
- I file di lock non vengono rimossi automaticamente: eliminare un inode mentre altri processi possono averlo aperto potrebbe dividere il dominio di `flock`.
- Limite: gli hash non sono un'identità crittografica e non eliminano matematicamente ogni collisione; la validazione del nome nel record è il controllo definitivo. Il protocollo rimane cooperativo e non protegge da processi ostili dello stesso UID che possono modificare i file di coordinamento.


## C35 — namespace privato per i file di coordinamento

- I file di lock dei mapping nominati sono stati spostati da `/tmp` direttamente in una directory per-UID (`/tmp/winapi-shm-locks-UID`), creata con permessi richiesti `0700`. La directory esistente viene validata come directory reale, di proprietà dell'UID corrente e senza permessi per gruppo/altri.
- L'apertura dei file avviene relativamente al descrittore della directory con `openat()` e `O_NOFOLLOW`, `O_CLOEXEC`; questo evita di risolvere nuovamente il percorso completo durante l'apertura del file. Restano i controlli del file regolare, proprietario, link count e permessi.
- Aggiornati i test che preparano record incompleti e stati di crash per il nuovo percorso privato. Build standard e build completa con `-Werror` superate; ASan/UBSan e TSan sono state eseguite separatamente.
- Decisione di ciclo di vita: **nessuna rimozione automatica dei lock file**. Un processo può avere già aperto l'inode o essere in attesa su `flock`; cancellare il percorso senza un protocollo globale di registrazione e retry può dividere il dominio di sincronizzazione. La directory dedicata limita l'ingombro e consente una futura procedura amministrativa esplicita, ma non è una garbage collection automatica.
- Limite: questa protezione è pensata contro sostituzioni accidentali e utenti con UID diversi; un processo ostile con lo stesso UID può modificare i file del medesimo utente.

## C36 — matrice di regressione del protocollo dei mapping nominati

- Aggiunto un caso di crash distinto da quello già coperto: il processo termina dopo aver scritto lo stato `INITIALIZING` ma prima di creare l'oggetto POSIX. Il successivo `OpenFileMapping` deve tollerare `shm_unlink(...)=ENOENT`, completare il recupero del record e restituire `ERROR_FILE_NOT_FOUND`; un successivo `CreateFileMapping` deve riuscire.
- La regressione complementa il caso preesistente `INITIALIZING` + oggetto POSIX a dimensione zero, la validazione dei record troncati/corrotti e dei permessi del lock, e la gara multiprocesso di creazione con processi avviati via `exec`.
- La matrice attualmente copre gli stati di crash prima/dopo `shm_open`, la corruzione rilevabile del record e la concorrenza di creazione; non simula ogni possibile interruzione tra tutte le singole syscall né processi ostili dello stesso UID.
- La libreria continua a fallire chiusa su record non valido. Non sono stati modificati ABI pubblica o elementi libc duplicati.

## C37 — hardening dei percorsi di errore del record di coordinamento

- Estesa la matrice negativa dei file di lock: un record della dimensione corretta ma associato a un nome diverso deve essere rifiutato, anche se il file si trova nel percorso calcolato per il nome richiesto.
- Aggiunta una regressione per valori di stato sconosciuti: non devono essere interpretati come `READY` né consentire l'apertura del mapping.
- I test verificano `ERROR_GEN_FAILURE` e rimuovono esplicitamente i file artificiali dopo la prova; non modificano i lock di mapping validi.
- Riesaminati i percorsi di rilascio: gli errori dopo l'acquisizione del lock rilasciano il `flock` e chiudono il descrittore; la chiusura del descrittore resta la salvaguardia finale che rilascia il lock kernel anche se `LOCK_UN` fallisce. Gli errori di inizializzazione non vengono trasformati in successi silenziosi.
- Limiti residui: non è stato introdotto fault injection per simulare errori arbitrari di `ftruncate`/`pread`/`pwrite`/`fchmod`; la recovery da crash è testata tramite processi helper deterministici. I file di lock non vengono cancellati automaticamente, per evitare di dividere il dominio di `flock`.
- Nessuna modifica all'ABI pubblica o alle implementazioni duplicate della libc.


## Round C38 — rollback della pubblicazione del record di lock

- `write_lock_state` non pre-dimensiona più un file di lock vuoto prima di scrivere il record: `pwrite` estende il file, evitando una finestra artificiale con dimensione valida ma contenuto non ancora scritto.
- Se la scrittura del record iniziale fallisce dopo un trasferimento parziale, tenta di riportare il file a dimensione zero e conserva l'errore originale per il chiamante. Un successivo tentativo può quindi inizializzare nuovamente il record se il rollback è riuscito.
- Se il processo viene terminato durante la scrittura, oppure anche il rollback fallisce, il record può restare corrotto. In tal caso il comportamento rimane intenzionalmente fail-closed: non si presume che un record sconosciuto sia sicuro e non si rimuovono automaticamente file di lock.
- Verificata la build completa con `WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`, `-Werror` e CTest. Questa suite non inietta direttamente errori artificiali in `pwrite`/`ftruncate`; il ramo di rollback è quindi coperto da revisione del codice, non da fault injection dedicata.


## C39 — fault injection controllata per scrittura parziale del record

- Introdotto un hook di fault injection esclusivamente per build di test con `WINAPI_SHMEM=ON` e `BUILD_TESTING=ON`. Non è dichiarato negli header pubblici e non viene compilato nelle build di produzione (`BUILD_TESTING=OFF` oppure memoria condivisa disabilitata).
- La regressione forza una scrittura parziale del primo record di coordinamento seguita da `EIO`. Verifica che `CreateFileMapping` fallisca con `ERROR_GEN_FAILURE`, che l'oggetto POSIX non venga pubblicato, e che un successivo tentativo possa inizializzare il record e creare il mapping.
- Build standard e build completa con `-Werror` superate. ASan/UBSan con leak detection superati; ThreadSanitizer superato; 20 ripetizioni della suite completa superate.
- Copertura ancora parziale: l'iniezione attuale verifica il rollback della prima scrittura parziale, ma non forza il fallimento di `ftruncate` durante il rollback né errori arbitrari di `pread`, `fchmod`, `shm_open` o `flock`. La garanzia del rollback resta condizionata al successo di `ftruncate`.
- Nessuna modifica all'ABI pubblica e nessuna modifica alle implementazioni duplicate della libc. I file di lock validi continuano a non essere rimossi automaticamente.

## C40 — fault injection del fallimento del rollback

- Esteso l'harness di test per simulare il fallimento di `ftruncate(fd, 0)` usato per annullare una scrittura parziale del record di coordinamento.
- La regressione forza prima una scrittura parziale di `pwrite`, poi un errore `EIO` anche nel rollback. Verifica che la creazione fallisca, che l'oggetto POSIX non venga pubblicato e che un tentativo successivo rifiuti il record danneggiato in modalità fail-closed.
- Il test verifica la dimensione residua del record e rimuove il file di lock di test solo dopo il ritorno delle chiamate API; non viene introdotta garbage collection automatica dei lock.
- Gli hook restano privati al sorgente e sono compilati solo con `BUILD_TESTING=ON` e `WINAPI_SHMEM=ON`; non fanno parte dell'ABI pubblica.
- Build standard con `-Werror`, build completa con thread e memoria condivisa, ASan/UBSan con leak detection e ThreadSanitizer superati. La suite completa è stata ripetuta 20 volte senza fallimenti.
- Nessuna modifica all'ABI pubblica o alle implementazioni duplicate della libc.
- Limiti residui: restano da aggiungere fault injection specifiche per `pread`, `fchmod`, `shm_open` e `flock`; questo round non dimostra la gestione di ogni possibile errore di syscall.

## C41 — fault injection per gli errori di lettura e inizializzazione dei mapping

- Estesi gli hook privati di test per simulare una volta sola errori `EIO` di `pread`, `fchmod`, `shm_open(O_CREAT|O_EXCL)` e `flock(LOCK_EX)`. Gli hook sono compilati solo nelle build di test con memoria condivisa abilitata; non sono dichiarati negli header pubblici.
- Aggiunta una regressione per il fallimento di `flock`: la chiamata deve fallire prima della pubblicazione dell'oggetto POSIX.
- Aggiunta una regressione per il fallimento della `shm_open` di creazione: nessun oggetto deve essere pubblicato e il successivo tentativo deve recuperare il record `INITIALIZING` e completare la creazione.
- Aggiunta una regressione per il fallimento di `fchmod`: l'oggetto parzialmente inizializzato deve essere chiuso e rimosso; il tentativo successivo deve recuperare e riuscire.
- Aggiunta una regressione per il fallimento di `pread` del record di coordinamento: `OpenFileMapping` rifiuta la richiesta, mentre una successiva apertura senza fault deve riuscire.
- Build standard e build completa con `-Werror` superate; ASan/UBSan con leak detection superati; ThreadSanitizer superato; 20 ripetizioni della suite completa superate. Una build di produzione con `BUILD_TESTING=OFF` è stata verificata con `nm`: nessun simbolo `winapi_test_shmem_*` o `g_test_fail*` presente.
- Limiti residui: gli hook simulano errori specifici ma non coprono ogni combinazione di errore e interruzione; in particolare restano da valutare il fallimento di `flock(LOCK_UN)` e i fallimenti durante le operazioni di cleanup. Le chiusure dei descrittori restano la salvaguardia finale per il rilascio del lock del kernel.
- Nessuna modifica all'ABI pubblica o alle implementazioni duplicate della libc. I file di lock non vengono rimossi automaticamente.

## C42 — regressioni per il fallimento di unlock e cleanup

- Estesi gli hook di fault injection esclusivamente per i test per simulare un errore `EIO` su `flock(LOCK_UN)` e su `shm_unlink`.
- Aggiunta una regressione che fa fallire l'unlock esplicito durante il ritorno da una chiamata API e verifica che la chiusura del descrittore rilasci comunque il lock kernel: una successiva apertura sullo stesso nome deve riuscire.
- Aggiunta una regressione combinata: `fchmod` fallisce e anche la prima rimozione dell'oggetto POSIX fallisce. Il test verifica che l'oggetto residuo non venga trattato come pronto e che il record `INITIALIZING` consenta al chiamante successivo di ritentare la pulizia prima di creare il mapping.
- Gli hook non sono dichiarati negli header pubblici e sono inclusi soltanto nelle build di test con `WINAPI_SHMEM=ON` e `BUILD_TESTING=ON`.
- Limite: se il cleanup fallisce per cause persistenti, l'oggetto POSIX può restare temporaneamente nominato; il protocollo lo mantiene in stato `INITIALIZING` e il prossimo chiamante cooperante ritenta la rimozione. Non si aggiunge garbage collection automatica dei file di lock.
- Nessuna modifica all'ABI pubblica o alle implementazioni duplicate della libc.

## C43 — fault injection per fallimento dell'allocazione del wrapper

- Introdotto `shmem_calloc`, un wrapper interno che nelle build di test può fallire una sola allocazione con `ENOMEM`; nelle build normali delega direttamente a `calloc` e non aggiunge simboli pubblici.
- Aggiunta una regressione su `OpenFileMapping`: dopo l'apertura del descrittore POSIX, il fallimento dell'allocazione del wrapper deve restituire `ERROR_NOT_ENOUGH_MEMORY`, chiudere il descrittore e non inserire una voce parziale nel registro. Un'apertura successiva deve riuscire.
- Il wrapper è usato per le allocazioni dei wrapper mapping e delle viste, così i test futuri possono mirare anche a quei punti senza cambiare l'API Win32.
- Nessuna modifica all'ABI pubblica o alle implementazioni duplicate della libc; nessuna cancellazione automatica dei file di lock.
- La regressione copre un punto di allocazione in `OpenFileMapping`; non dimostra ancora il fallimento separato della seconda allocazione (entry), né gli errori di `close`/`fstat` e le combinazioni arbitrarie di cleanup.

## C44 — regressioni per la seconda allocazione dei wrapper

- Esteso l'hook privato di fault injection per fallire una allocazione dopo N allocazioni riuscite, senza esporre nuove API pubbliche. `winapi_test_shmem_fail_next_calloc()` conserva il comportamento precedente.
- Aggiunto un test che fallisce la seconda allocazione in `OpenFileMapping`: il wrapper appena allocato deve essere liberato, il descrittore chiuso e il registro lasciato coerente; l'apertura successiva deve riuscire.
- Aggiunto un test che fallisce la seconda allocazione in `MapViewOfFile`, dopo il successo di `mmap`: la vista deve essere smappata e non registrata, mentre un tentativo successivo deve riuscire.
- Nessuna modifica all'ABI pubblica o alle implementazioni duplicate della libc. Gli hook restano limitati alle build di test.
- Limiti residui: gli errori di `fstat` e `close` non sono ancora simulati in modo controllato; la suite verifica qui due percorsi di fallimento della seconda allocazione, non ogni possibile combinazione di cleanup.

## C45 — fault injection mirata per `fstat` e preservazione di `errno`

- Introdotto il wrapper interno `shmem_fstat`, che nelle build di test può simulare un errore `EIO` dopo un numero configurabile di chiamate riuscite. Nelle build di produzione delega direttamente a `fstat` e non aggiunge hook pubblici.
- Aggiunta una regressione sul percorso di apertura di un mapping nominato già esistente: dopo le tre verifiche `fstat` del namespace/lock, la lettura dei metadati dell'oggetto POSIX fallisce. La chiamata deve restituire `ERROR_GEN_FAILURE`, chiudere il descrittore e lasciare possibile un tentativo successivo riuscito.
- Corretto `lock_named_mapping` per preservare l'`errno` originale quando `fstat` fallisce sulla directory privata, sul file di lock prima dell'acquisizione o sul file dopo l'acquisizione. Le verifiche di proprietà/tipo/permesse non validi continuano invece a restituire `EACCES`.
- Build standard e build completa con `-Werror` superate; ASan/UBSan con leak detection superati; ThreadSanitizer superato; suite completa ripetuta 20 volte (20/20). Build di produzione con `BUILD_TESTING=OFF` superata e controllata con `nm`: nessun simbolo `winapi_test_shmem_*` presente.
- Limiti residui: il test copre un punto mirato di `fstat`, non tutte le chiamate né ogni combinazione di guasti. Non è stata aggiunta fault injection per `close`, perché un `close` fallito su Linux non implica in generale che il descrittore sia ancora aperto; simulare il solo valore di ritorno senza riprodurre la semantica del kernel potrebbe testare un modello scorretto.
- Nessuna modifica all'ABI pubblica, alle implementazioni duplicate della libc o alla politica di conservazione dei file di lock.

### C46 — close lifecycle regression

Added a regression for the stale-handle/descriptor-reuse boundary: after a
WinAPI file handle is closed, a raw POSIX descriptor is opened and kept live;
a repeated `CloseHandle` with the stale opaque token must fail with
`ERROR_INVALID_HANDLE` and must not invalidate the POSIX descriptor. This
checks the externally observable lifetime contract without injecting an
unrealistic return value into `close(2)`. Public ABI and duplicate libc
implementations are unchanged.

## C47 — namespace HANDLE condiviso per le ricerche file

- `FindFirstFile` non restituisce più il puntatore heap di `filesearch` come `HANDLE`: assegna un token opaco tramite `winapi_allocate_handle_token`, condividendo il namespace monotono usato dagli handle file, thread, eventi, mutex e mapping.
- Il registro Win32 `FindFirstFile`/`FindNextFile`/`FindClose` è protetto da un mutex, serializzando lookup, avanzamento, inserimento e rimozione delle voci.
- L'allocatore globale dei token usa ora un ciclo compare/exchange saturante: in caso teorico di esaurimento restituisce `NULL` senza far traboccare il contatore e riutilizzare token vecchi.
- Aggiunta regressione: crea contemporaneamente un handle di ricerca e un handle file, verifica che siano distinti, che `CloseHandle` non accetti il token di ricerca, che la ricerca continui, e che il token di ricerca chiuso venga rifiutato senza influenzare l'handle file.
- Nessuna modifica all'ABI pubblica o alle implementazioni duplicate della libc. Gli handle `_findfirst` del runtime CRT sono un sottosistema distinto e non sono stati modificati.
- Limite residuo: il mutex rende sicuro il registro di ricerca Win32, ma non definisce una semantica di cancellazione per `FindClose` simultaneo a una `FindNextFile` già in corso oltre alla serializzazione delle chiamate; l'ordine effettivo è quello imposto dal mutex.


## C48 — sessione pesante: concorrenza FindNextFile/FindClose e coerenza documentale

- Aggiunta una regressione concorrente che avvia tre chiamate `FindNextFile` e una `FindClose` sullo stesso token mediante una condition variable condivisa (nessun polling). L'ordine è intenzionalmente non deterministico: la chiusura deve riuscire una sola volta, mentre le chiamate di avanzamento possono completare prima della chiusura oppure rifiutare il token se la chiusura ha già vinto il mutex.
- Ripetuto lo scenario 100 volte per esporre use-after-free, accessi alla voce rimossa o doppie chiusure nel registro. Dopo il join, il token deve essere rifiutato sia da `FindNextFile` sia da `FindClose`.
- Corretto il riepilogo storico della Fase B, che dichiarava ancora non protetto il registro di ricerca nonostante la correzione introdotta in C47.
- Questa regressione verifica la sicurezza della struttura del registro, non garantisce fairness o un particolare ordine tra `FindNextFile` e `FindClose`; tale ordine resta deliberatamente quello del mutex.

- C48 — l'audit ha inoltre rilevato che `FindFirstFile`, `FindNextFile` e `FindClose` aggiornano `errno` ma non sempre `GetLastError`, violando il contratto osservabile Win32. Le tre API ora mappano gli errori principali a `ERROR_FILE_NOT_FOUND`, `ERROR_PATH_NOT_FOUND`, `ERROR_NO_MORE_FILES`, `ERROR_INVALID_HANDLE`, `ERROR_INVALID_PARAMETER`, `ERROR_NOT_ENOUGH_MEMORY`, `ERROR_FILENAME_EXCED_RANGE` e `ERROR_TOO_MANY_OPEN_FILES` nei percorsi corrispondenti. Aggiunte regressioni per pattern senza risultati, fine enumerazione e token chiuso.
- Analisi statica esplorativa con GCC `-fanalyzer -Werror`: non superata a causa di segnalazioni preesistenti in `FormatMessage` (allocazione non verificata e buffer locale non restituibile tramite il parametro passato per valore) e nel sottosistema CRT `_findfirst`/`_findnext` (analisi della ricorsione wildcard e della gestione `strdup`). Queste aree non sono state modificate in C48: `_find*` è una duplicazione CRT da tenere fuori dall'integrazione, mentre `FormatMessage` non ha attualmente una dichiarazione nell'header pubblico. I risultati sono registrati come debito tecnico e non vengono presentati come test superati.


## C49 — audit della qualità delle regressioni concorrenti

Durante l'audit pesante è stata corretta una debolezza nel test concorrente introdotto in C48: una condizione booleana tautologica accettava qualsiasi esito di `FindNextFile`. Ora ogni worker registra il last-error thread-local del proprio thread e il test controlla gli esiti ammessi e il numero massimo di avanzamenti riusciti. Questo rafforza il test, senza modificare l'ABI o il comportamento della libreria.

## C49 — heavy audit: validità delle asserzioni di concorrenza

L'audit ha trovato una condizione tautologica nella regressione C48 (`result == TRUE || result == FALSE`), che non convalidava il risultato delle chiamate concorrenti. Il test è stato rafforzato: ogni worker acquisisce il last-error nel proprio thread, i fallimenti sono controllati contro gli errori ammessi e si verifica il numero massimo di `FindNextFile` riusciti. Aggiunto `HANDLE_REGISTRY_INVENTORY.md` per rendere espliciti i sottosistemi e la separazione intenzionale del registro CRT.

Verifiche C49 eseguite: build standard e completa con `-Werror`; CTest in entrambe; 30 ripetizioni della suite completa; ASan+UBSan con leak detection; ThreadSanitizer; build di produzione con `BUILD_TESTING=OFF`; controllo con `nm` che la build di produzione non contenga simboli di fault injection. Tutte superate.

Limiti non chiusi: il registro CRT `_findfirst`/`_findnext` resta separato; l'audit statico GCC `-fanalyzer` non è ancora pulito (segnalazioni già note in `FormatMessage` e CRT search); questa fase non dimostra la correttezza di tutte le API che restituiscono `HANDLE` né la piena compatibilità Windows.


## Fase C50 — audit incrociato BabyPOSIX ↔ libWINAPI

- Esaminato `posix.zip` fornito dall'utente: moduli BabyPOSIX `unistd`, `fd`, `dirent` e `time`.
- Aggiunto `BABYPOSIX_CROSS_API_AUDIT.md` con confronto in entrambe le direzioni, classificazione delle equivalenze dirette/parziali/assenti e raccomandazioni sui namespace di handle.
- Gap principali BabyPOSIX → libWINAPI: `getpid`, `access`, `dup`/`dup2`, `pipe`, `fcntl`, API di file descriptor POSIX, directory stream POSIX e `gettimeofday`; `sleep`/`usleep`, `chdir`, `getcwd`, `unlink` e `rmdir` hanno solo corrispondenze parziali.
- L'audit è statico: il pacchetto BabyPOSIX non include l'intero repository BabyC, quindi la registrazione complessiva nell'interprete resta da verificare. Nessuna modifica ABI o al codice runtime in questa fase.


## C51 — correzione del contratto `FormatMessage`

- Risolta la vecchia implementazione che allocava un buffer locale senza restituirlo al chiamante, ignorava `dwMessageId` e poteva dereferenziare un'allocazione fallita.
- `FormatMessage` ora implementa esplicitamente il sottoinsieme con buffer fornito dal chiamante e `FORMAT_MESSAGE_FROM_SYSTEM`; usa `str_werror`, controlla la capacità prima della copia e imposta `GetLastError` in caso di input non valido, buffer insufficiente o flag non supportati. `FORMAT_MESSAGE_ALLOCATE_BUFFER` è rifiutato esplicitamente perché la firma attuale non consente un contratto corretto di allocazione tramite puntatore a puntatore.
- Aggiunti test di regressione per successo, buffer insufficiente, ID sconosciuto e flag non supportati.
- Nessuna modifica ABI ai tipi o alle API esistenti; dichiarazione pubblica aggiunta per rendere esplicito il sottoinsieme implementato.
- Il sottosistema CRT `_findfirst`/`_findnext` resta fuori perimetro d'integrazione; l'analizzatore statico continua a segnalare lì casi da esaminare separatamente (matcher wildcard ricorsivo e gestione allocazioni).

## C51 — chiusura di difetti residui in diagnostica e stringhe

- `FormatMessage`: eliminata l'allocazione persa e l'uso errato di `errno` al posto di `dwMessageId`; implementato e documentato il sottoinsieme con buffer fornito dal chiamante, controllo dimensioni e rifiuto esplicito dei flag non supportati. Test anti-regressione aggiunti.
- `str_replace`: controlli degli argomenti prima di ogni dereferenziazione, comportamento definito per pattern vuoto, controlli di overflow/allocazione e sostituzione in un singolo passaggio sul testo originale (evita loop quando la sostituzione contiene il pattern).
- `RtlInitUnicodeString`: gestito `SourceString == NULL` secondo l'annotazione opzionale e protetto il puntatore destinazione nullo.
- Build standard e build completa con `-Werror` e test CTest superati dopo le modifiche.
- L'analisi GCC `-fanalyzer` non segnala più `FormatMessage` o `wstring.c`; restano warning concentrati nel sottosistema CRT `_findfirst`/`_findnext` in `wfiles.c`. Questo sottosistema resta deliberatamente separato dall'integrazione e non viene dichiarato pulito.

## C52 — hardening del matcher wildcard CRT

- Sostituito il matcher ricorsivo `_match_spec` con un algoritmo iterativo: elimina la ricorsione non limitata e il backtracking esponenziale dei pattern avversari, mantenendo `*`, `?` e la regola Windows speciale `*.*`.
- `match_spec` e `_match_spec` ora rifiutano puntatori nulli senza dereferenziazioni.
- Gestito il fallimento di `strdup(spec)` in `findfirst_in_directory`: chiude la directory, libera la struttura e restituisce `ENOMEM`.
- Reso esplicito il cleanup di tutti i tre elementi (`DIR`, pattern allocato, handle) quando la prima lettura della directory fallisce.
- Aggiunte regressioni per wildcard normali, pattern vuoti, puntatori nulli, `*.*` e un pattern avversario lungo che prima provocava un backtracking ricorsivo molto costoso.
- Build standard e completa (`WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`) con `-Werror` superate; suite completa ripetuta 30 volte; ASan+UBSan con leak detection superati.
- `GCC -fanalyzer -Werror` non è ancora pulito: continua a riportare un percorso di leak in `is_win_path`/`update_drive_list` legato alla lista temporanea dei drive e avvisi sul cleanup CRT. Non si dichiara quindi conclusa l'analisi statica globale; la prossima revisione deve verificare il ciclo di vita della lista drive e la propagazione degli errori di `closedir`/`fstatat`.

## C53 — cleanup delle liste drive e hardening del CRT `_findfirst`

- `clear_drive_list()` ora rilascia sempre le risorse indipendentemente dallo stato di inizializzazione della libreria; il cleanup non deve essere bloccato da un controllo di stato.
- `is_win_path()` controlla il risultato di `update_drive_list()` e ripulisce anche le liste parziali in caso di errore, senza continuare su dati incompleti.
- `_findfirst()` valida subito `filespec` e `fileinfo`, restituendo `EINVAL` per argomenti nulli.
- `fsearch_open()` controlla l'allocazione prima di dereferenziare/inserire la voce. Se fallisce, `_findfirst()` chiude l'handle POSIX che non è stato preso in carico.
- `update_fhandle_list()` controlla gli input e ogni allocazione; in caso di errore rilascia le voci parziali e chiude la ricerca sottostante. Evitata inoltre un'allocazione persa per le voci `.` e `..`, che venivano allocate ma intenzionalmente non inserite nella lista.
- Aggiunte regressioni per `_findfirst(NULL, ...)` e `_findfirst(..., NULL)` con verifica di `EINVAL`.
- Build standard e completa con `-Werror` e CTest superati. La build con GCC `-fanalyzer -Werror` ora compila senza segnalazioni e CTest passa (1/1), chiudendo i warning di allocazione/null-dereference precedentemente concentrati in `is_win_path()` e nel registro CRT.
- Il sottosistema CRT resta separato dalle API Win32 `FindFirstFile`/`FindNextFile`/`FindClose`; non è stato fuso con il registro HANDLE Win32.

## Round C-prio — priorità logica dei thread (`SetThreadPriority`/`GetThreadPriority`)

- Supera il comportamento del Round C5: per una priorità Win32 valida (i 7 livelli `THREAD_PRIORITY_*`) e un handle thread valido, `SetThreadPriority` ora memorizza la priorità **logica** nel record del thread (`thread.priority`, default `THREAD_PRIORITY_NORMAL` alla `CreateThread`) e restituisce `TRUE`. Lettura e scrittura avvengono sotto `g_wait_lock`.
- Nuova `int WINAPI GetThreadPriority(HANDLE)`: restituisce la priorità logica; handle non valido o chiuso → `ERROR_INVALID_HANDLE` e `THREAD_PRIORITY_ERROR_RETURN` (`0x7FFFFFFF`, macro aggiunta in `wthreads.h`).
- Invariati: priorità non valida → `FALSE` + `ERROR_INVALID_PARAMETER` (valore memorizzato invariato); handle non valido → `FALSE` + `ERROR_INVALID_HANDLE`.
- **Limite onesto:** la priorità NON viene applicata allo scheduler del sistema operativo (nessun `setpriority`/nice, nessuna policy real-time). La chiamata ha quindi effetto solo sul valore restituito da `GetThreadPriority`. La mappatura verso un nice per-thread (best-effort, con rifiuto OS non fatale) resta aperta, così come le priorità di processo/classe, `THREAD_MODE_BACKGROUND_*` e gli pseudo-handle (`GetCurrentThread()`).
- Test: `tests/smoke.c` copre set/get sui 7 livelli, valore non valido e handle non valido per Set e Get; rimosse le asserzioni C5 che attendevano `ERROR_CALL_NOT_IMPLEMENTED`. Build standard e completa (`WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`) con `-Werror` e CTest superati (1/1); smoke completo pulito anche sotto ASan e TSan. Compilazione MSVC non verificata in questo round.

## Round C-filetime — FILETIME / conversioni temporali (2026-10-19)

### Implementato

- Aggiunto un helper interno condiviso `winapi_timespec_to_filetime()` in `wtime.c` (dichiarato in `wprivate.h`). Converte `timespec` in tick da 100 ns a partire dal 1601-01-01 UTC (`EPOCH_DIFF = 11644473600 s`), tronca i nanosecondi al tick inferiore e rifiuta timestamp anteriori al 1601, `tv_nsec` non valido e overflow.
- Implementate `GetSystemTimeAsFileTime`, `SystemTimeToFileTime`, `FileTimeToSystemTime`, `CompareFileTime`, `FileTimeToLocalFileTime` e `LocalFileTimeToFileTime`, incluse validazione del calendario gregoriano, anno bisestile, ignoranza di `wDayOfWeek` in input, giorno della settimana in output e rifiuto dei FILETIME con bit alto impostato in `FileTimeToSystemTime`.
- Implementato `GetFileTime` sugli handle `CreateFile` gestiti dal registro `wfile_handles.c`. Gli output sono indipendentemente opzionali (anche tutti NULL); il handle viene sempre validato. `wfile_begin_io`/`wfile_end_io` assicurano lease, lock e lifetime rispetto a `CloseHandle` concorrente.
- `FindFirstFile` e `FindNextFile` ora valorizzano creation/access/write FILETIME da `stat`, preservando la precisione `st_atim`/`st_mtim` disponibile e troncando a 100 ns. Rimosso `get_filetime()`, che restituiva il clock corrente e non era chiamato.
- Aggiunti i binding guest in entrambi i rami di `src/cstdlib_ext/win.c`: native Win32 sotto `WIN32`, libWINAPI sull'host POSIX. Nessun header POSIX è stato aggiunto al ramo Windows.
- Estesi `tests/smoke.c` con i vettori noti, calendari non validi, confronti high/low, timestamp `utimensat` esatti per FindFirstFile/GetFileTime, output NULL, handle non valido/chiuso, controllo del clock corrente e prove di conversione sotto `TZ=UTC` e `TZ=Europe/Rome`. Aggiunto il test guest non numerato `src/tests/windows/winapi_filetime.c`.
- Follow-up di copertura: il test smoke ora imposta timestamp distinti e nanosecondi noti su due file e verifica entrambi i risultati dell'enumerazione `FindFirstFile` + `FindNextFile`, senza assumere un ordine di directory specifico. Questo chiude il buco per cui l'esattezza temporale di `FindNextFile` era coperta indirettamente dal codice condiviso ma non verificata direttamente.

### Politiche e limiti espliciti

- **Creation time POSIX:** non esiste un campo birth-time portabile in `struct stat`. In questo backend `ftCreationTime` usa `st_ctim` come proxy best-effort (tempo di ultima modifica dei metadati), non come garanzia di vera data di creazione. Accesso e scrittura usano rispettivamente `st_atim` e `st_mtim`.
- **Bias locale:** le conversioni FileTime↔LocalFileTime applicano l'offset corrente del fuso, incluso il DST in vigore adesso, invece di ricalcolare il DST per la data contenuta nel FILETIME. È la politica Win32 scelta e documentata; non è adatta a ricostruire un orario civile storico con regole di fuso variabili.
- **MSVC:** la build nativa Windows/MSVC non è stata disponibile in questo ambiente; la portabilità del ramo `WIN32` è quindi non verificata a livello di toolchain. Da confermare compilando `src/cstdlib_ext/win.c` con MSVC e l'SDK Windows, oltre a provare il test guest su Windows.
- Le API escluse dal round restano escluse: `SetFileTime`, conversioni DOS date/time e conversione SYSTEMTIME con fuso esplicito.

### Verifiche eseguite su questa baseline modificata (Linux/GCC)

- CMake standard, `-Werror`: compilazione riuscita; `winapi_smoke` 1/1.
- CMake full (`WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`), `-Werror`: compilazione riuscita; CTest 1/1.
- Full + ASan/UBSan (`detect_leaks=1`, `halt_on_error=1`): compilazione riuscita; CTest 1/1, nessun report sanitizer.
- `cd src && make clean && make -j2`: completato; nessun warning emesso dal compilatore. GNU make ha segnalato clock skew dovuto ai timestamp dei sorgenti estratti, warning ambientale non relativo al codice.
- `./babyc tests/windows/winapi_filetime.c`: `WinAPI FILETIME: ok`, rc=0. Anche `winapi_createthread.c`, `winapi_exitcode_cs.c`, `winapi_full_surface.c` e `winapi_console_file.c` hanno restituito rc=0 in questa esecuzione.
- Matrice: Tree ACCEL=0 `1009/1009`; Linear STRICT ACCEL=1 `1009/1009`; Linear ACCEL=1 `1009/1009`, gate no-tree-fallback `violations=0`, `tree_body=0`.

### Follow-up di copertura — FindNextFile (2026-10-19)

- Modificato `tests/smoke.c`: due file distinti ricevono access/write timestamp impostati tramite `utimensat` con nanosecondi non multipli di 100; ogni risultato restituito dall'enumerazione Win32 viene confrontato esattamente con il FILETIME atteso, dopo troncamento a 100 ns. Il test non dipende dall'ordine di enumerazione.
- Verifica reale: build CMake standard con `-Werror` riuscita; `ctest --output-on-failure`: `100% tests passed, 0 tests failed out of 1`.
- Follow-up DST (2026-10-19): sotto `TZ=Europe/Rome`, il test costruisce FILETIME UTC per una data estiva e una invernale e verifica che `FileTimeToLocalFileTime` applichi a entrambe lo stesso delta del bias corrente; verifica inoltre il round-trip Local→UTC per entrambe. Questo rende esplicita e verificabile la scelta di non ricostruire il DST storico dalla data rappresentata.
- Verifiche ripetute dopo il follow-up: build standard `-Werror` + CTest 1/1; build full (`WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`) `-Werror` + CTest 1/1; build full ASan+UBSan con leak detection + CTest 1/1. MSVC resta una verifica esterna a carico dell'utente.


## Round C-prio2 — mappatura best-effort delle priorità sullo scheduler POSIX/Linux (2026-10-19)

- `SetThreadPriority` mantiene il valore logico Win32 per `GetThreadPriority` e ora prova anche ad applicare una priorità OS per-thread su Linux tramite `setpriority(PRIO_PROCESS, tid, nice)`. La mappatura è esplicita: IDLE→10, LOWEST→5, BELOW_NORMAL→2, NORMAL→0, ABOVE_NORMAL→-2, HIGHEST→-5, TIME_CRITICAL→-10.
- Il TID Linux viene acquisito nel thread stesso con `SYS_gettid`, così non si confonde `pthread_t` con un PID/TID numerico. La priorità memorizzata prima dell'avvio viene applicata al bootstrap del thread. L'aggiornamento e l'applicazione best-effort sono serializzati con il lock dello stato dei thread. Il TID viene azzerato al cleanup per non poter colpire un TID riutilizzato dopo la terminazione.
- Errori di `setpriority` non annullano il successo della modifica logica: su Linux non privilegiato, abbassare il valore nice (aumentare la priorità) normalmente richiede `CAP_SYS_NICE`; perciò l'effetto sullo scheduler non è garantito. Questa scelta è documentata e non simula l'equivalenza completa con le sette classi Win32. Su sistemi non Linux resta la priorità logica.
- Verifica eseguita: build CMake completa con `WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`, `-Werror`; CTest `1/1` passato. La prova non asserisce un valore nice OS preciso, perché i permessi del runner possono impedirne l'applicazione.
- **Aperto:** verificare il mapping con permessi scheduler controllati e valutare un backend per sistemi POSIX non Linux; il contratto Win32 completo delle priorità non è riproducibile tramite nice.


## C21 — semantica EOF di ReadFile

- Corretto `ReadFile` sincrono su file ordinari: EOF ora restituisce `TRUE` con conteggio trasferito pari a zero, coerentemente con il contratto Win32 per file sincroni.
- Il comportamento precedente (`FALSE` + `ERROR_HANDLE_EOF`) interrompeva i normali cicli di lettura che terminano quando il conteggio è zero.
- Aggiornato il test di regressione nel test di fumo WinAPI. Le richieste overlapped restano esplicitamente non supportate e non sono state modificate.

## Heavy bug-hunting round — file APIs and static analysis (2026-10-19)

Interventi verificati in questo round:
- `CreateFile(CREATE_ALWAYS)` ora richiede accesso in scrittura prima di impostare `O_TRUNC`. Evita una perdita di dati: su Linux `O_RDONLY|O_TRUNC` può comunque troncare il file, mentre la richiesta Win32 senza accesso in scrittura deve fallire.
- `CreateFile` azzera `GetLastError()` in caso di successo ordinario e conserva `ERROR_ALREADY_EXISTS` per `CREATE_ALWAYS`/`OPEN_ALWAYS` quando il percorso esisteva già.
- `GetCurrentDirectory`, `SetCurrentDirectory` e `GetDiskFreeSpaceEx` impostano errori Win32 deterministici in caso di errore e `ERROR_SUCCESS` in caso di successo. Il percorso di `GetCurrentDirectory` troppo piccolo non viene scritto e segnala la capacità richiesta.
- `GetDiskFreeSpaceEx` controlla l'overflow delle moltiplicazioni dei contatori `statvfs` e non aggiorna output parziali in caso di errore.
- `_findfirst` ora tiene distinta la stringa del chiamante dalla stringa temporanea convertita e libera quest'ultima in un unico punto.
- Regressioni aggiunte per la mancata troncatura in sola lettura, `GetLastError`, percorsi non esistenti e output di spazio disco invariati al fallimento.

Verifiche:
- Build standard con `-Werror` e CTest: superati.
- Build completa `WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`, con `-fanalyzer -Werror`, e CTest: superati.
- Build ASan+UBSan con leak detection: CTest superato in 20 ripetizioni consecutive.
- GCC `-fanalyzer -Werror` inizialmente ha segnalato un leak in `_findfirst`; la diagnosi era un falso positivo dovuto al confronto fra puntatore originale e puntatore convertito. La funzione è stata rifattorizzata con una variabile separata e il controllo statico completo è ora pulito.

Limiti aperti: le API esplicitamente non supportate (I/O overlapped, condivisione Win32 dei file, eventi nominati, `TerminateThread`, `SuspendThread`/`ResumeThread`, alcune opzioni di mapping e priorità) restano tali dove una simulazione parziale potrebbe violare le garanzie di sincronizzazione o sicurezza. Non sono state mascherate come implementazioni complete. La build nativa MSVC/Windows deve ancora essere eseguita nell'ambiente dell'utente.

## Round C-prio3 — transazioni di chiusura handle (2026-10-20)

- `CloseHandle` per un evento nominato non elimina più definitivamente il record dal registro se `munmap` fallisce: reinserisce il record, mantiene il descrittore di backing aperto e restituisce `FALSE` con `ERROR_GEN_FAILURE`, così il chiamante non perde l'unico handle ancora valido.
- La chiusura di un handle thread controlla ora il risultato di `pthread_join`. In caso di errore, conserva e ripristina il record nel registro invece di liberare strutture native non confermate come chiuse.
- Questi percorsi sono difese di rollback del ciclo di vita; gli eventi nominati restano comunque non supportati dall'API pubblica corrente e non sono dichiarati come implementati.
- Verifica Linux: build CMake completa con `WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`; CTest 1/1 superato. MSVC/Windows non eseguito in questo ambiente.

## Round C-prio4 — apertura atomica con CREATE_ALWAYS / OPEN_ALWAYS (2026-10-20)

- Eliminata la coppia `stat()` + `open()` usata per dedurre `ERROR_ALREADY_EXISTS`: era soggetta a una race interprocesso e poteva riferire uno stato diverso da quello effettivamente aperto.
- `CREATE_ALWAYS` e `OPEN_ALWAYS` tentano ora la creazione con `O_CREAT|O_EXCL`; se il nome esiste, riaprono secondo la disposizione richiesta e, se nel frattempo il file viene rimosso, ritentano la creazione. Il flag `O_TRUNC` è escluso dal tentativo esclusivo iniziale, così non si tronca un file prima di aver determinato il ramo corretto.
- Aggiunti test per `OPEN_ALWAYS` su file nuovo/esistente, preservazione dei dati esistenti, `ERROR_ALREADY_EXISTS`, e `CREATE_ALWAYS` su nome assente.
- Verifica Linux: build completa con `WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`, `-Werror`, ASan e UBSan; CTest 1/1 superato con leak detection attiva.
- Limite residuo: la semantica di sharing Win32 rimane non implementata e viene rifiutata esplicitamente; la build MSVC/Windows va ancora eseguita nell'ambiente nativo.


## Round C-prio5 — FlushFileBuffers per handle di file (2026-10-20)

- Aggiunta `FlushFileBuffers(HANDLE)` al subset WinAPI: valida e mantiene un lease sull'handle, richiede accesso in scrittura e usa `fsync()` per chiedere al sistema operativo di sincronizzare i dati del file. La chiusura concorrente attende la fine dell'operazione tramite il ciclo di vita già presente nel registro handle.
- In caso di errore, l'API conserva il mapping `errno`→Win32; in caso di successo imposta `ERROR_SUCCESS`. Gli handle read-only falliscono con `ERROR_ACCESS_DENIED`; gli handle chiusi/non validi con `ERROR_INVALID_HANDLE`.
- Aggiunti test di regressione per flush riuscito, rifiuto di handle read-only e handle già chiuso.
- Limite: la sincronizzazione è quella fornita da `fsync()` sul backend POSIX e non promette equivalenza fisica assoluta con ogni filesystem/driver Windows; handle di eventi, pipe e altri oggetti non-file non sono accettati da questa API del subset. Verifica nativa MSVC/Windows ancora necessaria.

## Round C-prio6 — SetEndOfFile (2026-10-20)

- Aggiunta `SetEndOfFile(HANDLE)`: valida l'handle tramite il registro condiviso, richiede accesso in scrittura e tronca/estende il file alla posizione corrente con `ftruncate()`.
- La lettura della posizione corrente e il troncamento avvengono sotto il lease esclusivo dell'handle, evitando interferenze con le operazioni sincrone concorrenti sullo stesso file pointer.
- Gli errori di dimensione massima (`EFBIG`) vengono mappati a `ERROR_DISK_FULL`.
- Test aggiunti: troncamento alla posizione 5, estensione alla posizione 8 con verifica dei byte intermedi a zero, rifiuto su handle read-only e handle già chiuso.
- Verifica Linux: build standard `-Werror`, build completa `WINAPI_THREADS=ON`/`WINAPI_SHMEM=ON` con `-fanalyzer -Werror`, build ASan+UBSan con leak detection; CTest 1/1 superato in tutte e tre.
- Limite: non verificato in compilazione nativa MSVC/Windows in questo ambiente.

## Round C-prio7 — GetFileType per handle di file (2026-10-20)

- Aggiunta `GetFileType(HANDLE)` con costanti `FILE_TYPE_*` e dichiarazione pubblica.
- Il registro file attualmente ammette solo file regolari; un handle valido restituisce `FILE_TYPE_DISK`, mentre un handle non valido restituisce `FILE_TYPE_UNKNOWN` e conserva `ERROR_INVALID_HANDLE`. Il risultato è protetto da lease, quindi la chiusura concorrente non può liberare il record durante la verifica.
- Aggiunti test per tipo disco, `GetLastError` in caso di successo e handle già chiuso.
- Verifica Linux: build standard `-Werror`, build completa con thread/shared-memory e `-fanalyzer -Werror`, e build ASan+UBSan con leak detection; CTest 1/1 superato in tutte e tre.
- Limite: gli altri tipi Win32 (console/character, pipe, remote) non sono registrati come file in questo backend e non vengono simulati; build MSVC/Windows da verificare nell'ambiente nativo.

## Round C-prio8 — GetFileInformationByHandle (2026-10-20)

- Aggiunta la struttura pubblica `BY_HANDLE_FILE_INFORMATION` e `GetFileInformationByHandle(HANDLE, ...)`.
- La funzione acquisisce un lease del registro handle, ottiene uno snapshot con `fstat()` e assegna l'output soltanto dopo aver validato dimensione e conversione dei timestamp. La chiamata rifiuta output nullo e handle non validi senza sovrascrivere parzialmente la struttura di output.
- Popolati attributi di base, tre timestamp, volume serial derivato da `st_dev`, dimensione 64 bit, numero link e indice file derivato da `st_ino`.
- Limiti documentati: `ftCreationTime` usa `ctime` come proxy poiché POSIX non offre un birth time portabile; volume serial e file index sono valori host-derived; sono supportati soltanto file regolari del backend.
- Test: file vuoto e non vuoto, attributi, link/index, timestamp, output nullo, handle chiuso.
- Verifica Linux: build standard `-Werror`, build thread/shared-memory con `-fanalyzer -Werror`, build ASan+UBSan con leak detection; CTest 1/1 in tutte e tre. MSVC/Windows non verificato in questo ambiente.


## Round C-prio9 — GetFileAttributes / GetFileAttributesEx (2026-10-20)

- Aggiunte le API `GetFileAttributes` e `GetFileAttributesEx`, con `WIN32_FILE_ATTRIBUTE_DATA` e `GET_FILEEX_INFO_LEVELS`. Gli output vengono pubblicati solo dopo che `stat` e tutte le conversioni dei timestamp sono riusciti.
- La conversione dei percorsi Windows riusa `win2posix_path`; gli errori impostano `GetLastError`, inclusi nome assente, componente non-directory, input nullo/vuoto e livello informativo non supportato.
- Attributi derivati dal backend: directory, archive per file regolari, readonly quando nessun bit POSIX di scrittura è presente, hidden per basename che inizia con punto. `SYSTEM` e `TEMPORARY` non vengono inventati. I timestamp di creazione restano un proxy `ctime`; la dimensione delle directory è esposta come zero.
- La stessa classificazione degli attributi è riutilizzata da `FindFirstFile`, riducendo divergenze tra API di enumerazione e interrogazione metadata.
- Test di regressione: attributi/size/timestamp su file non vuoto, livello informativo non supportato, output nullo e percorso inesistente. Build Linux standard `-Werror`, ASan+UBSan con thread/shared-memory e leak detection, e `-fanalyzer -Werror`: CTest 1/1 in tutte e tre. Build nativa MSVC/Windows non eseguita in questo ambiente.

## Round C-prio10 — mutazioni filesystem e spazio disco (2026-10-20)

- Aggiunte `DeleteFile`, `MoveFile`, `CreateDirectory`, `RemoveDirectory` e `SetFileAttributes`, usando la conversione condivisa dei percorsi e un mapping centralizzato degli errori POSIX→Win32.
- `DeleteFile` rifiuta directory e file senza bit di scrittura; `MoveFile` non sovrascrive intenzionalmente una destinazione già esistente; `CreateDirectory` non dichiara supporto per `SECURITY_ATTRIBUTES` non nulli; `RemoveDirectory` usa il backend POSIX diretto per non ereditare la semantica legacy di `win_rmdir` che considera successo una directory mancante.
- `SetFileAttributes` supporta la proiezione readonly via `chmod` e valida coerenza di directory/device. Il flag hidden è derivato dal nome POSIX: non può essere cambiato in-place, quindi richieste che lo alterano falliscono esplicitamente con `ERROR_NOT_SUPPORTED`. I flag Windows senza equivalente mutabile non vengono simulati.
- Aggiunta `GetDiskFreeSpace`: legge i contatori `statvfs`, restituisce cluster/settori in un formato Win32-like e controlla overflow e output nulli. Il backend usa settori logici convenzionali da 512 byte e un cluster almeno grande quanto il fragment del filesystem; i contatori oltre il limite DWORD sono saturati.
- Test aggiunti per attributi directory, dimensione directory, readonly, rifiuto di cancellazione readonly, rename file/directory, destinazione già esistente, percorsi mancanti, security attributes non supportati, spazio disco e argomenti nulli.
- Verifica Linux: build standard `-Werror`, build thread/shared-memory con `-fanalyzer -Werror`, e build ASan+UBSan con leak detection; CTest superato in tutte e tre. La compilazione nativa MSVC/Windows va eseguita nell'ambiente Windows.
- Limiti residui: la verifica di destinazione e `rename()` in `MoveFile` non costituiscono un'operazione atomica no-replace contro processi concorrenti; la semantica di attributi resta una proiezione POSIX, non una replica completa del filesystem Windows.
- Durante il turno è stata anche corretta `GetDiskFreeSpaceEx`: ora usa `f_frsize` come unità di allocazione (con fallback a `f_bsize`), invece di moltiplicare sempre per il block size di trasferimento; testati output coerenti e mancata pubblicazione parziale in caso di errore.

## Round C-prio11 — CopyFile e API dei file temporanei (2026-10-20)

- Aggiunta `CopyFile`: copia in un file temporaneo creato in modo esclusivo nella directory di destinazione, gestisce letture/scritture parziali e `EINTR`, sincronizza il contenuto prima della pubblicazione e conserva i permessi POSIX di base e i timestamp quando supportato.
- Il file finale viene pubblicato solo a copia completata. Con `bFailIfExists != 0`, `link()` realizza il commit no-overwrite in modo atomico; con `FALSE`, `rename()` sostituisce la destinazione. Le copie sulla stessa sorgente/inode e le sorgenti non regolari sono rifiutate. La copia cross-volume è naturalmente gestita perché il temporaneo è creato nella directory finale.
- Aggiunte `GetTempPath` e `GetTempFileName`: scelta di `TMPDIR` solo se è una directory valida, fallback a `/tmp` e poi `/`; dimensione buffer controllata; creazione del nome temporaneo con `O_CREAT|O_EXCL`; `uUnique != 0` calcola il nome senza creare il file, coerentemente con il contratto Win32. Distinto inoltre il caso `stat()` fallito dal caso percorso esistente che non è una directory, evitando di usare `errno` stale.
- Corretto `_splitpath`: libera in modo non ambiguo il buffer di conversione e continua a separare i componenti anche per percorsi POSIX assoluti quando la mappa drive non è inizializzata, usando un fallback di normalizzazione dei separatori.
- Test anti-regressione: copia e verifica contenuti piccoli e >160 KB (attraverso più blocchi da 64 KiB), no-overwrite, overwrite, sorgente=destinazione, directory come sorgente, sorgente mancante, creazione e previsione di file temporanei, buffer insufficiente e `_splitpath` su percorso relativo.
- Hardening successivo: `GetTempFileName` rifiuta ora separatori e caratteri vietati Windows nei primi tre caratteri del prefisso. Un prefisso come `../` non può più trasformare il prefisso in componenti di percorso e far uscire il file temporaneo dalla directory richiesta.
- Verifica Linux: build standard `-Werror`, build completa con thread/shared-memory e `-fanalyzer -Werror`, build ASan+UBSan con leak detection; CTest 1/1 superato in tutte e tre.
- Limiti: `CopyFile` copia solo file regolari, non ACL/alternate data streams/security descriptors; timestamp sono best-effort; il backend non replica tutte le semantiche NTFS. `GetTempFileName` presuppone che il chiamante fornisca un buffer `MAX_PATH`, perché la firma Win32 non contiene una capacità esplicita. MSVC/Windows non eseguito in questo ambiente.

## Round C-prio12 — MoveFileEx e spostamenti no-replace (2026-10-20)

- Aggiunta `MoveFileEx` con `MOVEFILE_REPLACE_EXISTING`, `MOVEFILE_COPY_ALLOWED`, `MOVEFILE_WRITE_THROUGH` e rifiuto esplicito di `MOVEFILE_DELAY_UNTIL_REBOOT` (`ERROR_NOT_SUPPORTED`). I flag sconosciuti producono `ERROR_INVALID_PARAMETER`.
- `MoveFile` delega ora a `MoveFileEx(..., 0)`. Su Linux il ramo no-replace usa `renameat2(RENAME_NOREPLACE)` quando disponibile, eliminando la race tra controllo della destinazione e rename. Su POSIX senza questa primitiva resta un fallback check/rename e quindi una race residuale documentata.
- Con `MOVEFILE_COPY_ALLOWED`, un `EXDEV` su file regolari attiva `CopyFile` verso un temporaneo nella directory di destinazione e poi rimuove la sorgente. Se la rimozione sorgente fallisce, l'API segnala l'errore e conserva entrambe le copie per evitare perdita di dati. Le directory cross-volume non vengono copiate.
- `MOVEFILE_WRITE_THROUGH` sincronizza la directory di destinazione dopo lo spostamento; se la sincronizzazione fallisce, la funzione restituisce errore ma lo spostamento potrebbe essere già avvenuto (limite inevitabile dopo il commit).
- Test aggiunti per destinazione esistente, sostituzione, flag non valido, delay non supportato, write-through e, quando `/dev/shm` è un filesystem distinto, spostamento cross-volume con verifica dei byte.
- Verifica Linux: build standard `-Werror`, build ASan+UBSan con leak detection, build con `-fanalyzer -Werror`; CTest 1/1 superato in tutte e tre. La build nativa MSVC/Windows non è stata eseguita.


## Round C-prio13 — SetFileTime e byte-range LockFile/UnlockFile (2026-10-20)

- Aggiunta `SetFileTime`: converte `FILETIME` in `timespec` con controlli di rappresentabilità, usa `futimens` e `UTIME_OMIT` per aggiornare solo accesso/modifica richiesti. La richiesta di cambiare `lpCreationTime` viene rifiutata con `ERROR_NOT_SUPPORTED` prima di qualsiasi mutazione, perché POSIX non offre un setter portabile del birth time. Gli output/timestamp non vengono parzialmente aggiornati in caso di parametro non supportato o conversione non valida.
- Migliorato il mapping degli errori del registro file per overflow e syscall non supportata.
- Aggiunte `LockFile` e `UnlockFile` con intervalli byte a 64 bit, controlli di overflow/range vuoto, lock advisory `fcntl` e bookkeeping per handle. `UnlockFile` richiede l'intervallo esatto precedentemente acquisito; intervalli sovrapposti distinti sullo stesso handle sono rifiutati per evitare che il merge POSIX delle regioni renda imprecisa la tabella locale. I conflitti sono riportati come `ERROR_LOCK_VIOLATION`; chiusura handle rilascia i lock del kernel. `_GNU_SOURCE` è definito prima degli header nel backend/test POSIX, così `F_OFD_SETLK` è effettivamente rilevato e il test fra aperture indipendenti viene eseguito su GNU/Linux. Si usa `F_OFD_SETLK` quando disponibile per isolamento per-open-file-description; il fallback `F_SETLK` conserva la limitazione POSIX dei lock associati al processo.
- Test aggiunti per round-trip di timestamp sub-secondo, rifiuto creation-time senza mutazione parziale, handle read-only/invalidi, no-op senza timestamp, intervalli lock invalidi/overflow, range sovrapposti, conflitto tra aperture indipendenti, unlock errato e rilascio alla chiusura.
- Verifica Linux: build standard `-Werror`, build ASan+UBSan con leak detection e build `-fanalyzer -Werror`, tutte con CTest 1/1 superato. La compilazione nativa MSVC/Windows non è stata eseguita.

### Hardening supplementare — prefisso GetTempFileName (2026-10-20)

- Il prefisso viene validato prima della conversione del percorso: i primi tre caratteri effettivamente usati non possono contenere separatori, caratteri di controllo o caratteri vietati nei nomi Win32 (`<>:"/\|?*`). Questo chiude una possibile traversal cross-directory tramite prefissi come `../`, anche quando il nome non è controllato dal filesystem.
- Test anti-regressione per `../` e `a\\b`, entrambi rifiutati con `ERROR_INVALID_PARAMETER`.

## Round C-prio14 — bug hunting generale libWINAPI (2026-10-20)

- Corretto `FindFirstFile`/`FindNextFile`: i risultati directory di `glob(GLOB_MARK)` non producono più basename vuoti; i nomi nascosti sono inclusi con `GLOB_PERIOD` quando disponibile; se un file sparisce prima dello `stat`, non viene più pubblicato un record metadata a zero. La classificazione `HIDDEN` ignora i separatori finali e riconosce correttamente le directory con nome dot.
- `GetLogicalDriveStrings` ora emette root path con backslash finale e doppio NUL, con dimensionamento e controlli overflow coerenti. `resolve_root_path` usa una capacità esplicita invece di copiare il token di `/proc/cmdline` con `strcpy`.
- `SetFileAttributes` rifiuta symlink con `ERROR_NOT_SUPPORTED` per non cambiare per errore i permessi del target. Corrette le semantiche CRT: `win_remove` usa `remove()` (file o directory vuota), `win_mkdir` non maschera `EEXIST`, `win_rmdir` non maschera `ENOENT`, argomenti nulli/vuoti hanno `errno` deterministico e `win_fclose` propaga errori di flush/sync/close evitando `fdatasync` su device speciali.
- `RtlInitUnicodeString` imposta correttamente `Length` (senza terminatore) e `MaximumLength` (con spazio per il terminatore) e limita le stringhe lunghe senza wrap a 16 bit. `GlobalMemoryStatusEx` controlla overflow, segnala `sysinfo` fallito, limita i valori disponibili ai totali e stima lo spazio virtuale residuo sottraendo il `VmSize` del processo.
- Hardening X11: `WM_DELETE_WINDOW` ora viene riconosciuto tramite `WM_PROTOCOLS` e `data.l[0]`; `MessageBox` restituisce `IDOK` per il solo layout implementato e rifiuta layout pulsanti non supportati. `EnumDisplayDevices` valida i buffer, limita le copie, chiude la directory anche sui ritorni anticipati e marca il display predefinito confrontando endpoint normalizzati. Gli stub `EnumDisplaySettings`/`ChangeDisplaySettings` non dichiarano più un falso successo.
- Test anti-regressione aggiunti per directory hidden, wildcard, root drive strings, wrapper CRT, errori `fclose`, stringhe Unicode corte/lunghe, valori memoria e symlink. Il rapporto dettagliato è `docs/LIBWINAPI_BUGHUNT_2026-10-20.md`.
- Verifiche Linux: build standard `-Werror`; ASan+UBSan con leak detection e thread/shared-memory (20 ripetizioni); GCC `-fanalyzer -Werror`; ThreadSanitizer; build `WINAPI_DISPLAY=ON`; build `WINAPI_X11=ON`. CTest 1/1 superato in ciascuna configurazione. MSVC/Windows e interazione con server X11 reale non verificati.

## Round C-prio15 — durability audit di MoveFileEx (2026-10-20)

- Corretto `MOVEFILE_WRITE_THROUGH`: dopo il commit dello spostamento vengono ora sincronizzate sia la directory di destinazione sia quella sorgente. Un rename tra directory diverse modifica entrambe le entry; sincronizzare solo la destinazione non garantiva la persistenza della rimozione del vecchio nome dopo un crash.
- Se una delle sincronizzazioni fallisce, la funzione restituisce errore e documenta che il rename è già stato committato e non può essere annullato in sicurezza.
- Aggiunto test anti-regressione con spostamento `WRITE_THROUGH` tra due directory distinte, verificando la scomparsa del nome sorgente e la presenza della destinazione.
- Verifiche ripetute dopo la modifica: build standard `-Werror`, ASan+UBSan/leak detection, GCC `-fanalyzer -Werror`, ThreadSanitizer, build `WINAPI_DISPLAY=ON` e `WINAPI_X11=ON`; CTest 1/1 superato in tutte e sei le configurazioni. MSVC/Windows non eseguito.
- Follow-up bug hunt: `SetFileAttributes` ora rifiuta anche con `ERROR_NOT_SUPPORTED` le richieste che pretendono di modificare il bit `FILE_ATTRIBUTE_ARCHIVE` sintetizzato dal backend (incluso `FILE_ATTRIBUTE_NORMAL` su un file ordinario), anziché restituire un falso successo. Test di regressione verifica che l'attributo ARCHIVE rimanga invariato dopo il rifiuto.


## Round C-prio16 — GetFileInformationByHandleEx (2026-10-20)

- Aggiunta `GetFileInformationByHandleEx` con classi `FileBasicInfo`, `FileStandardInfo`, `FileAttributeTagInfo`, `FileIdInfo` e `FileAlignmentInfo`; le classi non implementate sono rifiutate con `ERROR_INVALID_PARAMETER` invece di restituire dati inventati.
- `FileBasicInfo` espone i timestamp POSIX convertiti in FILETIME; `CreationTime` resta un proxy `ctime` e `ChangeTime` riflette `ctime`, non il birth time NTFS. `FileStandardInfo` riporta dimensione logica, blocchi allocati (`st_blocks * 512`), link e flag coerenti con file regolari. `FileAttributeTagInfo` riporta gli attributi proiettati e tag zero perché il backend non espone reparse tag per gli handle registrati.
- Le strutture sono costruite in temporanei e copiate solo dopo una query valida; buffer nullo/corto, classe non supportata e handle invalido producono errori senza scrivere output parziale. I campi booleani di `FILE_STANDARD_INFO` sono byte, così il layout corrisponde alla struttura Win32 (24 byte).
- Test aggiunti per i tre layout ABI (40/24/8 byte), campi, classi non supportate, buffer insufficiente senza mutazione, puntatore nullo e handle invalido. Build standard `-Werror` e CTest superati. La verifica nativa MSVC/Windows non è stata eseguita.
- Limiti: sono supportati solo handle di file regolari registrati da `CreateFile`; restano non implementate le classi di rename/disposition, stream, directory e altri metadati non rappresentabili. `FileIdInfo` è ora disponibile come proiezione POSIX documentata nel round C-prio18.

## Round C-prio17 — contatori CPU e chiusura stream read-only (2026-10-20)

- `GetSystemTimes` non usa più `float` per rappresentare i contatori di `/proc/stat`: converte i tick USER_HZ in unità FILETIME da 100 ns con aritmetica intera, controlli di overflow e output pubblicati solo dopo una conversione completa. Il tempo kernel include il tempo idle, come previsto dal contratto Win32. I campi CPU aggiunti in kernel Linux recenti sono opzionali; i primi quattro restano obbligatori.
- Corretto un difetto del wrapper CRT `win_fclose`: prima tentava `fdatasync` su qualsiasi stream che `stat` classificasse come regolare, inclusi stream read-only su pseudo-filesystem come `/proc/stat`. Ora la sincronizzazione aggiuntiva è limitata a stream scrivibili e file regolari; gli errori di flush, sync e close restano propagati per gli stream scrivibili interessati.
- Test aggiunti per i risultati di `GetSystemTimes`, output nulli, inclusione del tempo idle nel kernel time e chiusura di `/proc/stat` in sola lettura. Il test esistente su `/dev/full` continua a verificare che gli errori di scrittura non vengano nascosti.
- Una build Clang 17 con `-Werror`, `WINAPI_THREADS=ON` e `WINAPI_SHMEM=ON` è stata aggiunta alle verifiche; build e CTest superati. Le build GCC standard, ASan/UBSan, `-fanalyzer`, TSan, DISPLAY e X11 sono state rieseguite dopo le ultime modifiche prima del pacchetto cumulativo.
- Limiti: `GetSystemTimes` resta una funzionalità specifica del backend Linux che legge `/proc/stat`; non è un'implementazione portabile per tutti i sistemi POSIX. La compilazione nativa MSVC/Windows non è stata eseguita.


## Round C-prio18 — SetFileInformationByHandle e FileIdInfo (2026-10-20)

- Aggiunta `SetFileInformationByHandle` per `FileEndOfFileInfo`: valida buffer e valori negativi/overflow, richiede un handle registrato scrivibile e usa `ftruncate`. L'operazione non modifica il file pointer condiviso dell'handle, coerentemente con la separazione fra EOF e posizione corrente.
- Aggiunto supporto `FileBasicInfo` limitato ai timestamp LastAccess/LastWrite. I valori zero sono trattati come “non modificare”; la conversione `FILETIME` usa controlli di rappresentabilità e `futimens` con `UTIME_OMIT`. Le richieste che coinvolgono CreationTime, ChangeTime o attributi Windows non rappresentabili falliscono prima di mutare timestamp supportati.
- Estesa `GetFileInformationByHandleEx` con `FileIdInfo` e `FileAlignmentInfo`; dichiarate le strutture pubbliche `FILE_ID_128` / `FILE_ID_INFO` / `FILE_ALIGNMENT_INFO`. Per file regolari l'allineamento richiesto è zero, dato che il backend POSIX supporta I/O a granularità di byte. Il backend usa `st_dev` come volume serial e l'inode POSIX zero-esteso nei 64 bit bassi dell'identificatore; il valore resta opaco e non va interpretato come ID NTFS nativo.
- Test aggiunti per dimensioni ABI, query ID, buffer corto senza scrittura parziale, timestamp, EOF senza spostamento del file pointer, EOF negativo, buffer insufficiente, classe non supportata, handle invalido e rifiuto di modifica EOF su handle read-only.
- Limiti: `SetFileInformationByHandle` non implementa `FileDispositionInfo`, `FileRenameInfo`, `FileAllocationInfo` né classi di metadati non rappresentabili; non viene dichiarato supporto parziale per tali classi. `FileBasicInfo` rifiuta esplicitamente i campi non supportati. `FileIdInfo` non è stabile tra filesystem/mount e non è garantito compatibile con ID Windows.


## Round C-prio19 — nomi handle e conversione UTF-8/UTF-16 (2026-10-20)

- Esteso `GetFileInformationByHandleEx` con `FileNameInfo` e `FileNormalizedNameInfo` per handle di file regolari. Il nome è letto da `/proc/self/fd` mentre il lease dell'handle è ancora attivo, quindi `CloseHandle` concorrente non può invalidare il descriptor durante la query. Il percorso restituito è quello assoluto risolto dal kernel.
- Aggiunta conversione UTF-8 → UTF-16 con rifiuto di sequenze malformate (`ERROR_NO_UNICODE_TRANSLATION`), coppie surrogate corrette per code point fuori dal BMP e controllo overflow della lunghezza in byte. `FILE_NAME_INFO` usa unità `WORD` a 16 bit esplicite per preservare il layout Win32 anche su host dove `wchar_t` è a 32 bit.
- Buffer insufficienti restituiscono `ERROR_INSUFFICIENT_BUFFER` senza modificare il buffer; handle che riferiscono un file già scollegato dal filesystem vengono rifiutati, anziché restituire il suffisso diagnostico Linux ` (deleted)` come se fosse un nome reale.
- Test anti-regressione per layout, nome ASCII, nome UTF-8 con `è`, classi normalizzata e non normalizzata, buffer corto senza scritture parziali e file eliminato mantenendo aperto l'handle.
- Limiti: il supporto richiede Linux e `/proc/self/fd`; su altri sistemi la query non è implementata. Sono ancora esclusi directory handle, `FileRenameInfo`, `FileDispositionInfo`, `FileAllocationInfo` e le classi non dichiarate supportate.
