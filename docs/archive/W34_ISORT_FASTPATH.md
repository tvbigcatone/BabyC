# W34 — Isort: generalizzazione sicura del fast path insertion-shift

**Data:** 2026-10-11  
**Baseline:** archivio originale `babyc(3).zip`; delta cumulativo.

## Obiettivo

Rimuovere il rischio prestazionale evidenziato da `isort` e dal benchmark perturbato `isort_v2`, senza modificare il risultato del programma e senza promuovere pattern incompleti.

## Causa profonda

`IrTryFastInsertShift` riconosceva soltanto un corpo `while` composto esattamente da due istruzioni: lo shift `a[j+1] = a[j]` e il decremento di `j`. Un'istruzione locale semanticamente inerte (`key = key`) faceva rifiutare il pattern, benché non cambiasse lo stato osservabile del ciclo. Di conseguenza `isort_v2` eseguiva l'interprete generale per tutte le iterazioni.

## Modifica

In `src/ir_fast.c`, il riconoscitore ora accetta il pattern canonico più fino a sei istruzioni aggiuntive, ma ignora esclusivamente assegnamenti semplici `key = key` con entrambi gli operandi riferiti allo stesso slot locale. La rappresentazione AST di un'assegnazione semplice usa `Op == 0`; non viene accettato un `+=`, un'altra assegnazione o una chiamata come no-op.

Le guardie mantengono esattamente un'operazione di shift e un decremento. Qualsiasi istruzione ulteriore diversa dall'autoassegnamento ammesso fa rifiutare il fast path. Il pattern canonico a due istruzioni non paga il controllo aggiuntivo dell'autoassegnamento.

È una generalizzazione circoscritta del riconoscitore, non una sostituzione dell'algoritmo né una scorciatoia basata sul nome del benchmark.

## Benchmark comparativo

17 run alternati baseline/corrente per ciascuna modalità, CPU affinity su CPU 0, mediana dei tempi end-to-end. L'output atteso e osservato è `29665` in ogni caso.

| Workload / modalità | Baseline | Corrente | Delta mediano |
|---|---:|---:|---:|
| isort — default | 26,893 ms | 26,720 ms | -0,64% |
| isort — Tree | 27,681 ms | 24,219 ms | -12,51% |
| isort — VM lineare | 25,931 ms | 25,017 ms | -3,52% |
| isort_v2 — default | 446,079 ms | 18,732 ms | -95,80% |
| isort_v2 — Tree | 482,641 ms | 19,479 ms | -95,96% |
| isort_v2 — VM lineare | 458,073 ms | 16,210 ms | -96,46% |

`isort_v2` passa da circa 0,45–0,48 secondi a circa 16–19 ms: un miglioramento mediano di circa 24–29x, senza differenze nell'output. Il test di controllo nello stesso binario, con `BABYC_NO_SHAPE_FP=1`, produce ancora `29665` ma richiede una mediana di 374,124 ms contro 14,304 ms con il fast path attivo (11 campioni per modalità). Questo conferma che la riduzione è associata alla selezione del kernel, non a un output scorciato o a un benchmark che termina prima.

I tempi assoluti presentano jitter, anche con CPU affinity; le mediane e il divario di oltre un ordine di grandezza sono però consistenti con l'attivazione del kernel. Il confronto su `isort` canonico non mostra regressione mediana. I campioni grezzi sono in `W34_ISORT_COMPARISON.json` e `W34_ISORT_BENCH_RAW.json`.

## Verifiche di correttezza

- Build GCC pulita dopo `make clean`: PASS, zero warning.
- Suite standard: **842 pass, 0 fail, 23 skip**.
- `tools/test_bytecode_vm.py`: PASS, inclusi i test strict/bytecode preesistenti.
- `bench/isort_v2.c`: output `29665` identico tra GCC nativo, baseline e versione corrente; output identico con fast path attivo/disattivato.
- Nessun rifiuto/fallback viene contato come esecuzione del bytecode compatto: questa ottimizzazione riguarda il kernel insertion-shift nei percorsi Tree/VM lineare, non rivendica copertura bytecode compatta.

## Stato

**Isort: rischio principale affrontato.** Il benchmark canonico non mostra regressione rispetto alla baseline; la variante perturbata accelera di circa 96% in mediana. La verifica prestazionale globale del progetto resta separata e non viene dichiarata PASS sulla base di un solo benchmark.

## Prossimo passo

Conservare `isort_v2` e `tools/bench_isort_fastpath.py` come controllo permanente del riconoscitore. La successiva ottimizzazione dovrebbe profilare i carichi `string_proc` e i loop con accessi indicizzati, senza ampliare il matcher insertion-shift oltre le forme semanticamente dimostrate.
