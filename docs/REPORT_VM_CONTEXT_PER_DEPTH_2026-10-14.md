# BabyC — isolamento dello stato VM per profondità di chiamata

Data: 2026-10-14

## Obiettivo

Rimuovere la causa architetturale che impediva l'esecuzione compatta rientrante: cache del bytecode e stato transazionale della VM erano singleton globali. Un'esecuzione annidata poteva sovrascrivere lo stream del chiamante o azzerarne log e snapshot. Non è stata rimossa la guardia di rientranza lasciando invariato quello stato: lo stato è stato separato prima di aprire la strada alle chiamate annidate.

## Intervento

File modificato in questa tranche: `src/ir_vm.c`.

- Introdotto `VmProdContext`, selezionato tramite un wrapper per profondità, con otto contesti riutilizzabili (profondità 0–7).
- Separati per contesto: cache compilata e copia delle istruzioni sorgente, parole bytecode, tabelle token/flag/immediati wide, log undo delle scritture, snapshot degli scalari globali, snapshot degli array e stato di commit/abort.
- Separato per contesto anche lo stato undo degli accessi indicizzati (`VmIdx*`), così una chiamata annidata non sovrascrive lo stato del chiamante.
- Il wrapper ripristina il contesto padre in ogni ritorno dell'implementazione interna, inclusi rifiuti in preflight e abort transazionali.
- Il log undo delle scritture è ora allocato pigramente e cresce per raddoppio fino al limite preesistente di 65.536 voci. I contesti e i buffer restano riutilizzabili: il percorso scalare ordinario non alloca un log se non esegue scritture indicizzate transazionali.
- Oltre la profondità massima, l'esecuzione compatta rifiuta il caso e lascia operare il percorso già esistente; non forza l'esecuzione né elude i controlli di sicurezza.

La memoria dei contesti è statica e riutilizzabile; non viene copiata l'intera cache del chiamante a ogni chiamata. I buffer dei log transazionali sono invece allocati solo quando servono e conservati per il riuso.

## Cosa non è dichiarato risolto

Questa modifica isola lo stato necessario per la rientranza, ma **non rende automaticamente eleggibili tutti i corpi funzione**. La lowerazione e il bridge delle chiamate mantengono ancora restrizioni sui corpi con locali, controllo di flusso e chiamate annidate. In particolare, i test `818_vm_nested_pure_scalar_call.c` e `819_vm_branchy_call_side_effect.c` continuano a verificare il fallback compatto previsto: non sono stati reinterpretati come prova di esecuzione annidata in bytecode.

La prossima tranche dovrà estendere l'eleggibilità solo dopo aver verificato preflight completo, semantica dei parametri/locali, rollback e assenza di replay degli effetti collaterali. Va aggiunto un test che dimostri esplicitamente l'esecuzione compatta a più profondità prima di dichiarare chiuso il supporto alle chiamate annidate.

## Validazione

Build Linux/GCC pulita eseguita con `make -C src clean` e `make -C src -j2`: **PASS**, nessun warning/error nel log di build.

`python3 tools/run_suite.py`:

- 906 PASS
- 1 FAIL aggregato (`bytecode-vm-regression`)
- 23 SKIP
- FP modes: 28/28 PASS
- string-scan fastpath: PASS
- sparse-local-snapshot: PASS
- bytecode VM: 25/29 gruppi PASS

I quattro gruppi VM segnalati dal gate sono `831_vm_multidim_array.c`, `833_vm_multidim_3d.c`, `834_vm_multidim_4d.c` e `extras`. I tre casi multidimensionali continuano a essere problemi preesistenti di copertura/semantica della VM lineare/compatta e non sono attribuiti a questa modifica senza una prova differenziale specifica.

Benchmark end-to-end `tools/bench_vm_compact.py --rounds 5` (una warm-up inclusa, 5 campioni misurati):

- Tree: mediana 20,467 ms
- Linear VM: mediana 11,250 ms
- Compact VM: mediana 13,713 ms

Il benchmark è stato eseguito dopo la modifica; non è un confronto A/B controllato sulla stessa sessione. La precedente misurazione disponibile aveva mediana compact di 14,062 ms. La differenza è modesta e soggetta a rumore: non viene rivendicato un guadagno prestazionale certo. La compact VM resta più lenta della linear VM su questo carico.

## Stato finale

Implementato il prerequisito architetturale: stato della VM compatta isolato per profondità, senza snapshot completo del chiamante e senza allocazioni del log sul percorso che non ne ha bisogno. La rientranza effettiva e l'allargamento dell'eleggibilità restano da dimostrare con test dedicati prima di poter considerare chiuso il problema.
