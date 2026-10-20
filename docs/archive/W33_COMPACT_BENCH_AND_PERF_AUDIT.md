# W33 — Benchmark compatto reale e audit prestazionale

**Data:** 2026-10-11  
**Baseline:** archivio utente `babyc(3).zip`; delta cumulativo rispetto a quella baseline.

## Obiettivi del turno

1. Rendere misurabile un workload realmente eseguito dal bytecode compatto, senza scambiare fallback per throughput VM.
2. Aggiungere una guardia automatica: il benchmark è valido solo se output e contatori attestano `attempts >= 1` e `runs >= 1`.
3. Ricompilare pulitamente baseline e versione corrente, ripetere i controlli di correttezza e misurare le differenze prestazionali con campioni ripetuti.

## Implementazione

- Aggiunti `src/tests/849_vm_compact_loop_bench.c` e `.expect`: ciclo scalare con 200.000 iterazioni e checksum deterministico `200000 700000`.
- Aggiunto `tools/bench_vm_compact.py`: warm-up, ordine dei modi ruotato, nove campioni per Tree/VM lineare/bytecode compatto, confronto esatto dell'output e rifiuto del risultato se il contatore compatto non prova una vera esecuzione (`runs >= 1`).
- Integrato il fixture 907 in `tools/test_bytecode_vm.py`, compreso il controllo strict senza fallback.

Il benchmark misura **tempo end-to-end** (startup, parsing/lowering ed esecuzione), non isola il solo dispatch del bytecode. Questa limitazione è riportata esplicitamente nei dati.

## Verifiche

- Build pulita GCC corrente: PASS, zero warning.
- Suite standard: **842 pass, 0 fail, 23 skip**.
- `tools/test_bytecode_vm.py`: PASS. Il fixture 907 produce `attempts=1, runs=1`; output uguale in Tree, VM lineare e compatta, e strict passa senza fallback.
- La baseline originale compila e produce l'output atteso del fixture 907, ma mostra un warning preesistente in `ir_call.c` sulla dichiarazione di `struct ParseState`.
- I benchmark `bench/isort.c` e `bench/matmul_small.c` sono stati ripetuti 7 volte per modalità e versione; stdout è identico tra baseline e corrente (`29665` e `2733800`). I campioni sono rumorosi, per cui non si attribuisce automaticamente ogni delta al codice.

## Benchmark reale del bytecode compatto

Nove campioni misurati dopo un warm-up, baseline e corrente ricompilate pulitamente con GCC. Mediane end-to-end:

| Modalità | Baseline | Corrente | Delta mediano |
|---|---:|---:|---:|
| Tree | 32,575 ms | 31,621 ms | -2,9% |
| VM lineare | 18,068 ms | 16,552 ms | -8,4% |
| Bytecode compatto | 18,349 ms | 16,627 ms | -9,4% |

Il bytecode compatto è stato realmente eseguito per tutti i campioni (il runner verifica `runs >= 1`). I delta sono incoraggianti ma vanno letti con prudenza: le misure sono wall-clock end-to-end e alcuni campioni presentano jitter.

## Audit dei benchmark precedentemente anomali

| Workload | Baseline Tree | Corrente Tree | Baseline VM | Corrente VM |
|---|---:|---:|---:|---:|
| isort | 8,041 ms | 8,892 ms | 7,847 ms | 9,256 ms |
| matmul_small | 3,054 ms | 2,901 ms | 3,445 ms | 3,913 ms |

Le mediane non confermano in modo uniforme le regressioni inizialmente osservate: `matmul_small` migliora in Tree e peggiora in VM, mentre `isort` peggiora in entrambe le modalità. I range dei campioni sono ampi, specialmente nei run VM. Non è corretto dichiarare un gate prestazionale globale PASS: `isort` resta un rischio aperto da ripetere con controllo più stretto della variabilità e misure per fase.

## Stato e limiti

- Il test 907 colma il vuoto di misurazione: non si benchmarka più un workload generico che fa fallback e viene etichettato erroneamente come throughput compatto.
- Non è stata introdotta una nuova ottimizzazione runtime in questo turno: la priorità è stata creare una base di misurazione affidabile e mantenere l'espansione VM verificabile.
- I gap compatti esistenti, inclusi accessi indicizzati e array di puntatori, restano espliciti; nessun fallback è contato come supporto.
- Gate prestazionale globale: **APERTO**. Prossimo passo: isolare parse/lowering/execution e investigare `isort` con profilazione per fase prima di modificare hot path.
