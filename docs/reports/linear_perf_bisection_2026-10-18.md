# Linear VM: bisezione prestazionale — 2026-10-18

## Metodo

Le tre modifiche sono rese indipendentemente selezionabili in `src/ir_lin.h`:
- `BABYC_LIN_FRAME_PER_FUNCTION` (default 1)
- `BABYC_LIN_CSE_PINNING` (default 1)
- `BABYC_LIN_STACK_REUSE` (default 1)

Sono stati costruiti i binari per le combinazioni e misurati `struct_score` e `dfs_rec` in STRICT con ACCEL=1 e ACCEL=0. Le prime misure di bisezione sono esplorative (2–3 ripetizioni e compilatione di varianti sequenziali); non vanno trattate come il gate definitivo. Il gate A/B definitivo è separato: sette coppie alternate baseline/corrente, con mediana e campioni conservati in `validation/perf_bisection_2026-10-18/bench_alt7.json`.

## A/B definitivo, sette ripetizioni alternate

Mediane in ms, baseline fissa vs sorgente corrente, 7 campioni per lato e configurazione:

| Benchmark | ACCEL | Baseline | Corrente | Delta |
|---|---:|---:|---:|---:|
| string_scan | 0 | 266.074 | 299.767 | +12.7% |
| string_scan_bounded | 0 | 304.608 | 350.961 | +15.2% |
| struct_score | 0 | 61.582 | 70.789 | +14.9% |
| dfs_rec | 0 | 23.652 | 24.940 | +5.4% |
| string_scan | 1 | 4.725 | 4.779 | +1.1% |
| string_scan_bounded | 1 | 5.790 | 5.893 | +1.8% |
| struct_score | 1 | 1.412 | 1.463 | +3.6% |
| dfs_rec | 1 | 9.433 | 9.233 | -2.1% |

ACCEL=1 non mostra il +121% riportato dalla misura precedente: la differenza osservata non è riproducibile con questa esecuzione alternata. ACCEL=0 resta sopra baseline, quindi il gate globale non è superato.

## Bisezione (misure esplorative)

Le misure con ACCEL=1 indicano risultati misti, senza una singola opzione che spieghi un crollo stabile. Con ACCEL=0 tutte le combinazioni osservate sono più lente della baseline fissa, compresa F0P0S0: quindi la regressione assoluta non può essere attribuita esclusivamente alle tre modifiche del turno; il delta cumulativo rispetto al punto zero contiene anche cambiamenti precedenti. Le combinazioni ACCEL=0 non sono state tutte completate con lo stesso numero di campioni a causa del limite di tempo del ciclo di compilazione: i JSON distinguono esplicitamente i dati parziali.

## Profilo `gprof` su `dfs_rec`

Per ottenere campioni sufficienti è stato usato un file di profiling temporaneo con 100× più iterazioni; il benchmark ufficiale non è stato modificato. Il profilo attribuisce circa il 21.1% del tempo campionato a `IrExecLinLocalsRegs`, 16.9% a `Step`, 12.7% a `FnCodeRun`, 9.9% a `FnCodeInstall`, 5.6% a `IrFramePop` e 5.6% a `IrTryThinIfCallFor`. L'allocazione/init del frame è inclusa in `IrExecLinLocalsRegs`, ma `gprof` non la separa dalla dispatch; non è corretto concludere che `alloca` da solo sia il costo dominante. `perf` non è installato nell'ambiente.

Chiamate osservate nel profilo scalato: `IrTryFastForAfterInit` 2,000,002; `IrTryFastPtrScanFor` 2,000,002; `IrTryFastArrayFill` 500,017. Sono conteggi di chiamata, non conteggi di hit riusciti. I call-site e i matcher fast-path non sono stati modificati dalle nuove opzioni; tutte e tre le opzioni sono attive per default. Il dump IR corrente di `dfs_rec` riporta `dfs=40` istruzioni e `main=105`; il precedente dump del delta consegnato nel turno corrente descrive la stessa versione di codice, quindi non c'è una differenza IR tra quel delta e il default attuale. Il dump è allegato per riproducibilità.

## Matrice anti-regressione

- Tree ACCEL=0: 996/996, 0 fail, 0 timeout
- Tree ACCEL=1: 996/996, 0 fail, 0 timeout
- Linear STRICT ACCEL=0: 979/996, 17 fail, 0 timeout
- Linear STRICT ACCEL=1: 979/996, 17 fail, 0 timeout

Elenco storico pre-rinumerazione: 046→047, 057→058, 061→062, 121→126, 280→288, 441→454, 610→606, 613→609, 622→618, 661 (SQLite)→651, 785→789, 942→873, 943→874, 959→893, 1033→947, 1034→948. Il vecchio 379 corrispondeva a un prefisso duplicato; i candidati oggi identificati come `395_array_fn_loop_local_global` e `396_complex_inventory` passano. Le voci 288, 789 e 874 sono state corrette dopo quel rilevamento. Lista finale: 047, 058, 062, 126, 454, 606, 609, 618, 651, 873, 893, 947, 948. Nessuna regressione nuova di correttezza nella matrice dopo la rinumerazione.

## Conclusione

La correttezza resta stabile e il profilo non supporta l'ipotesi che `alloca`/azzeramento sia da solo la causa del +20% su `dfs_rec`. Il +121% di `struct_score` con ACCEL=1 non è riproducibile nel test alternato. La regressione ACCEL=0 è reale, ma le misure disponibili la collocano nel delta cumulativo più ampio, non in una sola delle tre opzioni. Non si dichiara superato il gate prestazionale; nessuna nuova funzionalità va iniziata finché il gate non rientra.
