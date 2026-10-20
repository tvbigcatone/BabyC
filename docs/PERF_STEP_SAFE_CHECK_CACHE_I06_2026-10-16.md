# BabyC — cache locale dei safe-check nel dispatcher `Step()` (I06)

Data: 2026-10-16. Baseline cumulativa fissa: `babyc(20261016-120858).zip`.

## Modifica

`Step()` consultava `IrSafeChecksOn()` in 18 punti distinti del proprio corpo. Poiché la funzione restituisce una configurazione statica memorizzata al primo accesso (o una costante nelle build `BABYC_SAFE_FORCE_*`), ho introdotto una sola lettura locale `const int SafeChecks = IrSafeChecksOn();` all'ingresso di `Step()` e riutilizzato quel valore nei controlli interni.

Non sono stati aggiunti rami, modalità o percorsi d'esecuzione; i controlli e le condizioni che dipendono dal flag restano gli stessi. La lettura è locale alla singola chiamata e non viene mantenuta attraverso chiamate/ricorsione. La cache di `IrExecLinLocals()` già introdotta in I04 resta invariata.

## Build e correttezza

- GCC `-O2`, build di controllo G e candidata H: completate, senza `warning:` né `error:` nei log.
- 7 test × 4 modalità (Tree, Linear, Bytecode, Strict) × 2 varianti confrontate: **28/28 output, stderr e return code identici**.
- Casi: `141_bp_win64`, `216_ir_local_recurse`, `221_ir_nested_calls_locals`, `253_array_struct_member_uninit`, `545_ir_struct_array_uninit`, `870_vm_recursive_helper_effects`, `879_vm_mutual_pointer_recursion`.
- Suite completa, MSVC e sanitizer non eseguiti.

## Misura prestazioni

15 ripetizioni intercalate per variante e benchmark, più un warmup per ciascuna. Cronometraggio wall-clock del processo completo; ogni esecuzione ha verificato return code e stdout. GCC `-O2`, `BABYC_EXEC=vm`, `BABYC_NO_KERNEL=1`, `BABYC_NO_SHAPE_FP=1`. Dati grezzi completi in `PERF_STEP_SAFE_CHECK_CACHE_I06_RESULTS.json`.

| Benchmark | G mediana (s) | H mediana (s) | H vs G | MAD G / H (s) |
|---|---:|---:|---:|---:|
| `i01_collatz` | 0.466788 | 0.448289 | −3.96% | 0.008633 / 0.017094 |
| `i03_primes` | 0.310649 | 0.307524 | −1.01% | 0.007609 / 0.015288 |
| `gcd` | 0.047165 | 0.046038 | −2.39% | 0.001288 / 0.001777 |

Tutte e tre le mediane migliorano, ma la dispersione della candidata è maggiore e i guadagni sono piccoli. Il risultato è quindi **promettente ma non conclusivo**: la modifica è mantenuta perché elimina 17 chiamate ripetute a una funzione di configurazione in ogni invocazione di `Step()`, senza cambiare i rami semantici, ma non va interpretata come prova di un guadagno stabile su ogni carico. I dati sono riproducibili con gli script e le misure allegate al workspace della tranche; nessun benchmark ulteriore è stato dichiarato completato.

## Limiti

Non sono stati eseguiti benchmark aggiuntivi oltre ai tre indicati. Il test funzionale confronta controllo e candidata nello stesso ambiente; non sostituisce una suite completa o una verifica MSVC/sanitizer.
