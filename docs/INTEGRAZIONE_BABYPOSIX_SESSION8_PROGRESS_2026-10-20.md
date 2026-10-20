# BabyPOSIX sessione 8 — avanzamento integrazione (2026-10-20)

## Perimetro reale di questo delta

Questo delta è cumulativo rispetto alla baseline fissa `babyc(20261020-064516).zip` e conserva l'integrazione precedente attempt24/session10.

Sono state integrate le estensioni `#if` della sessione 8 e le API pthread mancanti per TLS, attributi mutex e attesa temporizzata delle condition variable.

- `pthread_key_t` e registrazione guest di `pthread_key_create`, `pthread_key_delete`, `pthread_setspecific`, `pthread_getspecific`;
- `pthread_mutexattr_init/destroy/settype/gettype`, con mutex ricorsivi;
- `pthread_cond_timedwait` con deadline assoluta e controllo del campo nanosecondi;
- storage per-thread indicizzato dallo slot BabyPOSIX, senza esporre gli handle pthread nativi;
- esecuzione del distruttore registrato quando un thread BabyPOSIX termina normalmente o tramite `pthread_exit`;
- cancellazione dei valori e del distruttore quando la chiave viene eliminata.

Il test `src/tests/posix/experimental/pthread_key_tls.c` copre isolamento tra thread, ritorno del valore, esecuzione del distruttore sia al ritorno normale sia tramite `pthread_exit`, valore della chiave nel thread chiamante e cancellazione della chiave. `pthread_key_mutexattr.c` verifica mutex ricorsivi e `pthread_cond_timedwait` su deadline scaduta. Il test `src/tests/posix/experimental/hash_if_full.c` verifica `defined`, macro object-like e function-like, unary `~`, moltiplicazione, shift, operatori bitwise, ternario, precedenza e parsing completo di catene logiche con short-circuit semantico. Per la sessione 9 sono stati aggiunti test sperimentali per `select/poll` (incluso `POLLNVAL`), header di copertura con `glob` `GLOB_DOOFFS`/`GLOB_APPEND`, `mmap`, `readv/writev`, `statvfs`, e `ffs(0x80000000)`; il test standard `fcntl_basic.c` ora verifica anche attivazione/disattivazione di `O_NONBLOCK`. I test sperimentali sono collocati nella sottocartella `experimental` perché il runner POSIX standard esegue solo i file `.c` al livello superiore e alcuni percorsi con function pointer/thread non sono ancora coperti in Linear STRICT; i sei test mirati sono stati verificati in Tree e Linear non-STRICT.

Il progetto Visual Studio è stato aggiornato per mostrare il test nella cartella `Tests\POSIX` come elemento non compilato nell'eseguibile principale. `posix_pthread.c` è già incluso nel progetto `babyc.vcxproj` da attempt24, quindi non serve una seconda voce di compilazione per il file sorgente.

## Verifiche eseguite in questa sessione

- Build GCC `make -j2`: completata, 0 warning nel log della build. Il controllo è stato ripetuto anche estraendo da zero la baseline fissa e applicando il delta attempt28: build pulita, 0 warning.
- Test dedicato `pthread_key_tls.c`: PASS in Tree.
- Test dedicato `pthread_key_tls.c`: PASS in Linear non-STRICT.
- Test mirati (`select_poll.c`, `cover_headers.c`, `ffs_32.c`, `hash_if_full.c`, `pthread_key_tls.c`, `pthread_key_mutexattr.c`): tutti PASS in Tree e Linear non-STRICT, anche sulla copia ricostruita dalla baseline più il delta.
- Test standard `fcntl_basic.c` esteso con `O_NONBLOCK`: verificato nella matrice POSIX aggiornata.
- XML parse di `babyc.vcxproj` e `babyc.vcxproj.filters`: PASS.
- Suite POSIX standard (36 test, senza il test sperimentale): Tree 36/36; NO_IR 35/36 (fallisce `more_headers`, `MAXPATHLEN`); Linear 35/36 (fallisce `pthread_exit_retval`); Linear STRICT 29/36, con i sette problemi già osservati in attempt24: `dup_basic`, `fcntl_basic`, `fd_errors`, `fd_rollback`, `msync_utimens`, `pthread_create_join` (crash/strict fallback) e `pthread_exit_retval`.

La suite è stata rieseguita dopo le modifiche pthread e parser; il risultato Tree 37/37 e Linear 36/37 includeva temporaneamente il test TLS al livello superiore. Spostato il test in `experimental`, i conteggi standard tornano al perimetro precedente. Questi risultati sono misure della copia di lavoro corrente, non una dichiarazione di piena conformità POSIX.

## Lavoro ancora aperto

- Il parser `#if` è stato esteso con operatori bitwise, shift, `~`, ternario e macro function-like; il test mirato passa in Tree e Linear non-STRICT. Restano da coprire i casi limite delle macro (in particolare macro function-like senza parametri e diagnostica per espressioni malformate).
- Le correzioni sessione 9 per `fcntl`, `poll`, `glob` e `ffs` risultano già presenti in attempt24. I test mirati ricreati localmente per queste aree passano in Tree e Linear non-STRICT; non è stato rieseguito l’intero suite storico 48/48 della sessione 9.
- Il test della sessione 7 `define_unary.c`, eseguito separatamente, fallisce su `NEG1` nel percorso di valutazione delle macro nelle espressioni ordinarie. Lo stesso esito è stato riprodotto compilando da zero la baseline fissa, quindi non è una regressione introdotta da questo delta; resta comunque un difetto aperto e non viene dichiarato risolto.
- MSVC non è disponibile nell'ambiente di lavoro: i file di progetto sono stati controllati come XML e i riferimenti al test verificati, ma la compilazione MSVC deve essere eseguita localmente.

Nessun esito non verificato viene marcato come PASS. Il completamento dell'integrazione delle sessioni 8 e 9 rimane aperto.
