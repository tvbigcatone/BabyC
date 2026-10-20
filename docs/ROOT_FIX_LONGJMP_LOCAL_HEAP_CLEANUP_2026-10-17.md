# BabyC — chiusura leak su errore con frame locali attivi

Data: 2026-10-17

Delta cumulativo sul punto zero `babyc(20261017-074837).zip`, mantenendo i delta precedenti già consolidati.

## Difetto riprodotto

Una build ASan/UBSan con `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` ha riprodotto
un leak nel percorso token-only (`BABYC_NO_IR=1`) del test
`100_array_type_mismatch.c`: 15 byte in tre allocazioni, originate da
`VariableRealloc()` e `VariableDefine()`. Il programma emetteva la diagnostica
di tipo corretta, ma `ProgramFail()` usciva tramite il punto di uscita host
prima che i frame locali venissero normalmente chiusi. Le allocazioni heap dei
locali rimanevano così irraggiungibili dalle tabelle globali pulite da
`PicocCleanup()`.

## Correzione

`VariableCleanup()` ora percorre i frame ancora attivi e libera i valori locali
che possiedono memoria heap, prima di pulire le tabelle globali e prima dello
smantellamento dell'heap. Le entry e i descrittori locali residenti nell'arena
stack **non** vengono liberati singolarmente: restano proprietà dell'arena e
vengono reclamati dalla normale pulizia dell'heap. Questo dettaglio evita di
passare a `free()` indirizzi interni allo stack arena.

Il percorso normale non cambia: dopo un'esecuzione regolare `TopStackFrame` è
NULL e non viene eseguito alcun lavoro per singolo statement o chiamata. La
correzione opera soltanto durante il cleanup finale quando un errore ha lasciato
frame attivi. Non si rivendica un guadagno prestazionale; il costo sul percorso
normale è limitato al controllo del puntatore al frame attivo in fase di cleanup.

Aggiunti `bad_assignment.c` e `tools/test_parse_failure_lsan.sh` per riprodurre
l'errore in una funzione chiamata da `main`, sia nel percorso IR sia in
`BABYC_NO_IR=1`. Il gate controlla exit non-zero atteso e assenza di report
ASan/LSan/UBSan.

## Correzione del build sanitizer

Il primo tentativo di usare `tools/asan_build.sh` non compilava: mancava
`-Iwinapi/include`; aggiungendolo emergevano poi riferimenti irrisolti perché
lo script non replicava `-DENABLE_WTHREADS=1 -DENABLE_WSHMEM=1` del Makefile
principale. Lo script ora riusa anche questi flag e l'include path, ed è
eseguibile direttamente. Build ASan/UBSan completa riuscita in
`/tmp/babyc_turn_asan_20261017/src/babyc`.

## Verifiche eseguite

- Build standard pulita `make -C src clean && make -C src -j2`: **PASS**, 0 warning.
- Suite `run_suite_tranches.py`, modalità `IR`: **960/960 PASS**, 0 FAIL, 0 timeout.
- Suite `run_suite_tranches.py`, modalità `IR_SHADOW0`: **960/960 PASS**, 0 FAIL, 0 timeout.
- Gate mirati: catalogo diagnostico PASS; FP **32/32**; string scan fast path PASS; sparse local snapshot PASS; empty-expect exit status PASS.
- Build ASan/UBSan completa: **PASS**.
- `tools/test_parse_failure_lsan.sh` con leak detection e halt-on-error attivi: **5/5 PASS** (quattro fixture in modalità IR e `bad_assignment.c` in token-only); nessun report sanitizer.
- Test ASan/UBSan mirati per `097`, `252`, `258`, `520_bitfield_struct_array_loop`, `905_vm_multidim_struct_array_copy`, `1014` e `1015`: esito atteso e nessun report sanitizer.

## Limiti residui

Questa tranche non esegue il gate bytecode aggregato, non certifica la suite
ufficiale end-to-end in tutte le modalità VM, non verifica MSVC/Windows e non è
una scansione sanitizer dell'intero corpus. I test riportati sono quelli
realmente completati in questa tranche. Nessuna modifica a output diagnostici,
aspettative esistenti o timeout della suite.
