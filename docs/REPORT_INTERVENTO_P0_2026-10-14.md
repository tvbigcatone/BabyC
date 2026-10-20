# BabyC — intervento P0 (14 ottobre 2026)

## Ambito e risultato

Baseline di partenza: `babyc(20261014-061913).zip`.

Questa revisione corregge due difetti di sicurezza/correttezza nel percorso VM e rimuove un warning di compilazione. **La fase P0 non è ancora chiudibile**: rimangono le discrepanze IR/no-IR dei test 402/611/966/967 e i casi multidimensionali 831/833/834 nel gate bytecode. Non sono stati mascherati aggiornando gli expected o riducendo il gate.

## Modifiche

1. `src/ir_lower.c`
   - Il percorso lineare VM rifiuta ora il lowering di dereferenziazioni annidate (`**pp`, `***ppp`) che la baseline eseguiva con risultati errati. `BABYC_EXEC=vm` usa quindi il percorso canonico per questi casi; `vm_strict` li segnala come non supportati invece di dichiarare copertura inesistente.
   - Eliminato il confronto fra un valore intero (`DeclInitVals[zi]`) e un puntatore nullo: l'array contiene valori interi, quindi il controllo corretto è `!= 0`.

2. `src/ir_lower.c`
   - La bridge della VM compatta rifiuta funzioni chiamate che scrivono globali o memoria indicizzata (`StoreGlobal`, `IncG`, `StoreIndex`) e operazioni con stato locale mutabile non compatibile. Ciò impedisce di ammettere chiamate con effetti collaterali come se fossero chiamate scalari pure.

## Verifiche eseguite

- Build GCC `make -C src -j4`: OK; il warning puntatore/intero è scomparso.
- Esecuzione mirata con `BABYC_EXEC=vm`:
  - `420_multi_level_ptr.c`: output `15 15` (percorso canonico; non viene più eseguito il lowering VM non sicuro).
  - `607_storm_multi_ptr_loop_ternary.c`: output atteso `37` e `1 10 19 28`.
  - `906_multi_level_ptr.c`: output atteso `3`, `43 43`, `9`.
  - `907_triple_ptr.c`: output atteso `4`, `28 28`.
- Gate bytecode: 25/29 gruppi superati. I test 839 e 840 ora mostrano `attempts=1, runs=0`, come richiesto; output invariato.
- Suite generale: 902 PASS, 5 FAIL, 23 SKIP; il riepilogo include i quattro test differenziali che intenzionalmente contengono output no-IR e IR distinti, oltre al gate bytecode non ancora chiuso. Non considero la suite verde.
- FP: 28/28.

## P0 ancora aperti

1. Discrepanze semantiche IR/no-IR: 402, 611, 966, 967. Gli expected registrano entrambi i risultati; va corretta la causa nell'esecutore IR, non aggiornati gli expected.
2. Gate bytecode:
   - 831: l'output lineare è corretto ma la VM compatta rifiuta il corpo (`runs=0`), quindi il test non dimostra copertura compatta.
   - 833 e 834: la VM lineare produce risultati errati sugli array globali 3D/4D.
   - `extras`: resta da diagnosticare.

Non è stata eseguita validazione MSVC/Windows né una campagna sanitizer completa. Tutti i test esistenti restano in `src/tests/`; non è stata creata `new_tests/`.
