# BabyC — audit costi chiamate i01/i03 e prima ottimizzazione senza nuovi rami

Data: 2026-10-16. Baseline cumulativa: `babyc(20261016-120858).zip`.

## Risultato dell'audit

Profilazione indicativa GCC `-O2 -pg` su `i01_collatz.c` e `i03_primes.c` (strumentazione non adatta a misurare benchmark comparativi):

- i01: `IrExecLinLocals` 60.000 chiamate; `Step` 6.254.915; `IrUninitChecksOn` 25.139.950 chiamate secondo il profilo.
- i03: `IrExecLinLocals` 200.001 chiamate; `Step` 417.985; `IrUninitChecksOn` 22.501.296 chiamate secondo il profilo.
- Entrambi i test hanno completato il body nel motore linear (`fncode_body=1`, `tree_body=0`, `body_runs=1`, `body_run_fail=0`). Output: i01 `52527 339 6134916`; i03 `17984 1709600813`.

Il conteggio gprof attribuito a `IrLinSetDiagnosticLocation` è incoerente con i punti di chiamata del sorgente e non è stato usato come prova di costo hot-path.

## Modifica applicata

Nei call site di `src/ir_eval.c`, `src/ir_lower.c`, `src/ir_stmt.c` e `src/ir_vm.c`, sostituita la chiamata a `IrUninitChecksOn()` con `IrSafeChecksOn()`.

Le due funzioni sono semanticamente identiche: `IrUninitChecksOn()` era soltanto un wrapper che ritornava `IrSafeChecksOn()`. La modifica elimina un livello di chiamata esterna nei punti coinvolti senza aggiungere rami, modalità, cache o stato; non disabilita né modifica la politica dei controlli. L'API wrapper resta disponibile per eventuali chiamanti esterni.

Non sono state toccate le chiamate a `IrSafeChecksOn()` già presenti, né i costi di frame/call setup: richiedono un'analisi separata perché una modifica indiscriminata potrebbe alterare la gestione di ricorsione, locali e side effect.

## Verifiche eseguite

- Build GCC `make -C src -j2`: completata senza errori e senza warning emessi in questo build.
- Test mirati in modalità Tree, Linear VM, strict bytecode e compact bytecode:
  - `141_bp_win64`: `10000` in tutte le modalità.
  - `221_ir_nested_calls_locals`: `11` in tutte le modalità.
  - `253_array_struct_member_uninit`: diagnostica mantenuta, posizione `7:25` in tutte le modalità.
  - `545_ir_struct_array_uninit`: output `1 2` e diagnostica mantenuta, posizione `8:23` in tutte le modalità.
- i01/i03 rieseguiti in Linear VM con output corretti riportati sopra.
- `tools/run_suite.py src` è stato avviato ma ha superato il timeout di 120 secondi; quindi la suite completa è **non verificata**, non dichiarata passata.
- MSVC e sanitizer non eseguiti in questa tranche.

## Limiti e prossimo candidato

La sostituzione rimuove il wrapper superfluo nel sorgente, ma non si dichiara un guadagno temporale quantificato (come richiesto, non si confrontano tempi tra versioni). Il prossimo candidato è il costo ripetuto di snapshot/restore per chiamata (`FnCodeSnapshot`, `IrFramePush/Pop`, `IdxRunPush/Pop`, `LinTabsRestore`), da ridurre solo se si può conservare lo stesso percorso di esecuzione e tutti gli invarianti.

## Tranche successiva — rimozione di una scrittura ridondante nello snapshot

**Modifica applicata in `src/ir_lower.c`:** `FnCodeSnapshot()` non azzera più `S->WideOn[WideMax..IR_LIN_STMT_MAX)`. Lo snapshot salva `WideMax` e copia i soli elementi attivi; i ripristini leggono esclusivamente l'intervallo `[0, WideMax)`. Il contenuto della coda inattiva non partecipa quindi allo stato ripristinato. Eliminare questa `memset` rimuove una scrittura completa della tabella a ogni snapshot senza aggiungere condizioni o percorsi d'esecuzione.

Con `IR_LIN_STMT_MAX` pari a 2048, si evita di scrivere fino a 2048 byte per snapshot (circa 120 MB su 60.000 snapshot e 400 MB su 200.000 snapshot, come volume cumulativo teorico massimo; non è una misura di tempo né implica che tutta la coda fosse sempre fisicamente scritta dal compilatore). La copia dei dati attivi resta invariata. Non sono stati introdotti nuovi rami, modalità o fast path.

### Verifiche della tranche

- Build GCC `-O2`: completata con exit code 0; nessun warning/error nel log.
- `141_bp_win64.c`: output `10000` in Tree, VM, strict e compact.
- `221_ir_nested_calls_locals.c`: output `11` in Tree, VM, strict e compact.
- `216_ir_local_recurse.c`: output `15` in tutte e quattro le modalità.
- `879_vm_mutual_pointer_recursion.c`: output `46 23 6` in tutte e quattro le modalità.
- `870_vm_recursive_helper_effects.c`: output `200 604 604` in tutte e quattro le modalità.
- `253_array_struct_member_uninit.c`: diagnostica mantenuta a `7:25` in tutte e quattro le modalità.
- `545_ir_struct_array_uninit.c`: output `1 2` e diagnostica mantenuta a `8:23` in tutte e quattro le modalità.
- `i01_collatz.c`: output `52527 339 6134916` in VM.
- `i03_primes.c`: output `17984 1709600813` in VM.
- Non è stata eseguita la suite completa, né MSVC o sanitizer in questa tranche.

La modifica riduce lavoro certamente ridondante nel sorgente; non è stato effettuato un confronto cronometrico tra versioni, in linea con la richiesta di valutare direttamente i costi e non fare benchmark comparativi.
