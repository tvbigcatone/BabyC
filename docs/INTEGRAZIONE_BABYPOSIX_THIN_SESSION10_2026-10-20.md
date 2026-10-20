# Integrazione BabyPOSIX thin layer — sessione 10 (2026-10-20)

## Integrazione

Integrata sulla baseline fissa `babyc(20261020-064516).zip`, mantenendo integralmente il delta attempt23 (exp29, dichiarazioni variadiche di function pointer e libWINAPI filename info).

La sessione 10 non contiene patch per `ir_call.c`, `ir_lower.c` o altri file IR: contiene sorgenti POSIX e un `include.c` derivato da un albero che aveva già ulteriori sessioni POSIX installate. Perciò non ho sovrascritto `include.c` alla cieca e non ho introdotto modifiche IR senza una patch/correzione semantica concreta. Ho integrato i sorgenti e le registrazioni che sono effettivamente forniti, creando gli header privati mancanti per le nuove unità (`posix_stat.h`, `posix_misc.h`, `posix_cover.h`) e aggiornando Makefile e progetto MSVC.

Componenti aggiunti/integrati:
- `pread`/`pwrite`, `pipe2`, `dup3`, estensione `fcntl`, gestione `F_DUPFD` e tabella fd condivisa;
- `dirent.d_type`, `DT_*`, `dirfd`/`fdopendir` con proprietà dei descriptor coerente;
- `times`, clock aggiuntivi, `getrusage`, `pathconf`/`fpathconf`, process groups, `getgroups`, `nice`;
- `msync`/`mprotect`, `utimensat`/`futimens`, `sys/stat.h` guest ABI;
- header aggiuntivi per `getopt_long`, `uname`, `ioctl`, `termios`, `select`, `poll`, `strings`, `locale`, `fnmatch`, `sched`, `syslog`, `glob`, `mman`, `uio`, `statvfs` e altri dichiarati dalla sessione;
- registrazioni condizionate per non duplicare `getopt.h` se `WITH_GETOPT` è attivo;
- sorgenti aggiunti al progetto MSVC e ai suoi filtri.

## Correzioni d'integrazione

- `AT_FDCWD`: la macro sostitutiva negativa letterale veniva valutata come zero dal preprocessore dell'interprete. La rappresentazione guest `4294967196` mantiene il valore ABI signed 32-bit `-100` quando convertito al parametro `int`, e permette `utimensat(AT_FDCWD, ...)` di funzionare. La definizione è coerente tra `fcntl.h`, `unistd.h` e `sys/stat.h`.
- `sys/stat.h`: rimossa la dipendenza implicita da `sys/types.h`, assente nella baseline fissata e non fornito dal pacchetto sessione 10.
- `uname`: sostituiti `strncpy(..., 64)` che produceva cinque warning GCC con copie bounded sull'intera dimensione guest e terminazione NUL esplicita.
- `include.c`: preservate le registrazioni originali BabyPOSIX/libWINAPI e aggiunte solo quelle sostenute dalle unità realmente integrate; non importate registrazioni per `posix_types.h`, `posix_wait.h`, `posix_signal.h`, `posix_pwd.h` mancanti nel pacchetto.

## Verifiche eseguite

- Build GCC `-O2`: riuscita, **0 warning / 0 errori**.
- Test POSIX su Tree: **36/36 PASS**, compresi tutti i test della sessione 10.
- Test `1081`–`1092`: **12/12 PASS** in Tree e **12/12 PASS** in Linear STRICT con `BABYC_ACCEL=0`.
- Test POSIX con `BABYC_NO_IR=1`: **35/36 PASS**. `more_headers.c` fallisce nel percorso token-only sulla condizione `MAXPATHLEN < 256`, benché il valore stampato sia `4096`; il caso minimo mostra una divergenza di valutazione di macro in condizione nel percorso token-only. Non ho alterato il test per mascherarla.
- Linear STRICT sull'intera cartella POSIX non è green: diversi programmi di test vengono rifiutati per `strict: tree fallback (body)` e `pthread_create_join.c` termina con crash. Non è stato disabilitato lo strict mode né dichiarato questo gate superato.
- La suite generale con aspettative è stata eseguita come controllo esplorativo: 1081/1083 PASS in default e 1059/1083 in `BABYC_NO_IR=1`. I fallimenti includono `019_include.c`, `267_typedef_ptr_local.c` e divergenze token-only; il confronto completo con una build baseline separata è scaduto e quindi non attribuisco tali risultati a questa integrazione né dichiaro la suite generale verde.
- MSVC non disponibile in questo ambiente: il progetto `.vcxproj` è aggiornato, ma la compilazione MSVC resta da verificare localmente.

## File IR

`ir_call.c` e `ir_lower.c` sono rimasti identici alla baseline/attempt23 perché il pacchetto sessione 10 non contiene versioni alternative o patch per quei file. I problemi Linear STRICT elencati sopra sono registrati apertamente, non coperti da fallback o da un report positivo.
