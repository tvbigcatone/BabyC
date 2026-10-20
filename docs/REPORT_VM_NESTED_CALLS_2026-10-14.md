# BabyC — chiamate compatte annidate ed effetti collaterali

Data: 2026-10-14

## Obiettivo della tranche

Proseguire dopo l'isolamento dei contesti VM per profondità: ammettere chiamate scalari branchy e chiamate annidate quando il percorso compatto può preflightare il corpo prima di eseguirlo, e rendere i test capaci di distinguere l'esecuzione compatta effettiva dal semplice output corretto via fallback.

## Modifiche

- `src/ir_lower.c`: la verifica di eleggibilità non rifiuta più un callee solo perché contiene `Jmp`, `BrZ` o `BrLT`. Il controllo resta conservativo sui tipi, sugli argomenti scalari e sui locali ordinari.
- Le chiamate annidate non sono più rifiutate per il solo fatto di essere presenti nel corpo. Il preflight runtime del contesto chiamato controlla il suo stream prima di eseguire istruzioni; il contesto padre conserva il proprio bytecode e il proprio stato transazionale.
- `tools/test_bytecode_vm.py`: i casi 842 e 843 ora richiedono almeno due esecuzioni compatte (caller e callee), anziché aspettarsi il fallback. Aggiunti al gate i test esistenti 931 e 932: il primo richiede esecuzione compatta annidata; il secondo mantiene esplicitamente l'aspettativa di fallback per la ricorsione con effetti collaterali, non ancora dimostrata sicura nel percorso compatto.

## Evidenze

Build GCC Linux: **PASS**, nessun warning/error nel log.

Test mirati:

- `818_vm_nested_pure_scalar_call.c`: `attempts=2, runs=2`, output `13`.
- `819_vm_branchy_call_side_effect.c`: `attempts=2, runs=2`, output `9 1`. Il valore finale `g=1` verifica che l'effetto collaterale non sia stato ripetuto dal fallback.
- `864_vm_nested_global_calls.c`: `attempts=4, runs=4`, output `21 23 2341`. Verifica chiamate annidate e aggiornamenti globali.
- `865_vm_recursive_global_effects.c`: `attempts=1, runs=0`, output `20 4`. Il risultato è corretto, ma il test conferma che la ricorsione resta sul percorso canonico: non viene dichiarata supportata dalla compact VM.

`tools/test_bytecode_vm.py`: **27/31 gruppi PASS**. Restano i quattro gruppi già noti: `831_vm_multidim_array.c`, `833_vm_multidim_3d.c`, `834_vm_multidim_4d.c` ed `extras`.

`tools/run_suite.py`: **906 PASS, 1 FAIL aggregato, 23 SKIP**; l'unico FAIL aggregato è `bytecode-vm-regression`, per i quattro casi sopra. FP modes: 28/28 PASS; string-scan fastpath e sparse-local-snapshot PASS.

Benchmark end-to-end `tools/bench_vm_compact.py --rounds 5`:

- Tree: mediana 21,186 ms
- Linear VM: mediana 11,420 ms
- Compact VM: mediana 14,291 ms

Non è stato effettuato un A/B controllato con la baseline nella stessa sessione; non si rivendica un guadagno prestazionale. La compact VM resta più lenta della linear VM sul carico 907.

## Limiti e prossimo passo

La ricorsione con effetti collaterali non è stata resa eleggibile: `932` resta fallback intenzionale e verificato. Non allentare ulteriormente i controlli finché il callee ricorsivo non può essere preflightato transitivamente e finché il test non dimostra che gli effetti avvengono esattamente una volta a ogni livello. La prossima tranche deve isolare la causa precisa dell'ineligibilità di `descend` e introdurre supporto ricorsivo solo con test di profondità, effetti globali, rollback e fallback al limite di profondità.
