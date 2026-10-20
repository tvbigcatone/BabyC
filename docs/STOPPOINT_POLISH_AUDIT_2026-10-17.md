# BabyC — audit di rifinitura del punto di sosta

Data: 2026-10-17

## Scopo

Verificare la coerenza dei conteggi e dei report prima di ricevere la seconda valutazione. Nessuna modifica al runtime o ai criteri di test è stata introdotta in questa rifinitura.

## Conteggi della suite diagnostica senza gate bytecode aggregato

Il log salvato in `suite_no_bytecode.log` riporta `passed=963 failed=0 skipped=28`. Questo conteggio è coerente con il runner diagnostico descritto nel punto di sosta: 960 casi C con aspettativa completati con esito positivo, più tre gate ausiliari superati (fast path stringhe, snapshot locali compatti e gate FP). I 28 file C che non hanno un `.expect` non sono casi falliti: sono saltati secondo il contratto di `tools/run_suite.py`. La dicitura 963 PASS non deve essere interpretata come 963 soli file C.

Il gate bytecode aggregato è stato intenzionalmente escluso da questa esecuzione diagnostica; perciò il suo timeout non è mascherato né conteggiato come PASS. Il runner ufficiale completo rimane non conclusivo finché non termina con il proprio riepilogo.

## Controllo diagnostico aggiuntivo

- `python3 tools/check_diagnostic_catalog.py`: PASS — 10 literal canonici confinati in `src/diagnostic.c`.
- Non sono state cambiate aspettative, diagnostiche, sorgenti runtime o timeout.

## Lettura delle misure prestazionali

I report della sessione riportano tempi assoluti differenti per `fib_ptr` e `call_loop`, raccolti in sessioni/configurazioni di misura diverse. Il dato da conservare è il confronto A/B intercalato nella stessa sessione: miglioramento di circa 67% per `fib_ptr` e circa 16% per `call_loop`, con output invariato. Non confrontare i tempi assoluti tra sessioni come se fossero una singola serie; il benchmark runner e la prova A/B con `BABYC_NO_KERNEL=1 BABYC_NO_SHAPE_FP=1 BABYC_NO_MEMO=1` hanno condizioni diverse.

## Stato per il secondo valutatore

**Confermato:** build GCC pulita documentata; test FP 32/32; suite diagnostica senza gate bytecode 963 PASS, 0 FAIL, 28 SKIP; controllo catalogo diagnostico PASS; gate bytecode isolati 53/53 secondo il report dedicato; miglioramento A/B delle chiamate ricorsive.

**Aperto:** esecuzione completa del gate bytecode aggregato e del runner ufficiale, build MSVC/Windows e verifiche sanitizer complete. Nessun esito inconclusivo viene promosso a PASS.
