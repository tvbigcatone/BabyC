# BabyC — bisezione delta di sessione e recupero performance Linear (2026-10-18)

## Metodo

Ogni archivio storico è stato estratto sopra una copia pulita della baseline fissa `babyc(20261018-120233).zip`, compilato da zero con GCC/Linux e confrontato con l'eseguibile baseline. Per ciascun delta sono stati eseguiti sette round alternati baseline/variante su `string_scan` e `struct_score`, con `BABYC_PATH=linear`, `BABYC_STRICT=1`, `BABYC_ACCEL=0`. Le misure per delta sono diagnostiche: la macchina ha mostrato variazioni del baseline tra batch, quindi la percentuale di ogni riga non va interpretata come una differenza perfettamente isolata tra due delta consecutivi.

## Bisezione dei delta storici

| Delta | `string_scan` | `struct_score` |
|---|---:|---:|
| `CSE_LOADINDEX` | 283.865 → 281.843 ms (-0.7%) | 61.660 → 61.549 ms (-0.2%) |
| `CSE_LOOP_ROOT` | 268.151 → 265.264 ms (-1.1%) | 63.597 → 62.873 ms (-1.1%) |
| `CSE_LOOP_IMPLEMENTATION` | 269.889 → 276.204 ms (+2.3%) | 61.790 → 60.974 ms (-1.3%) |
| `LOOP_CSE_CHECKPOINT` | 269.946 → 263.052 ms (-2.6%) | 60.304 → 60.252 ms (-0.1%) |
| `LOOP_WRITESET` | 264.394 → 276.484 ms (+4.6%) | 63.331 → 61.909 ms (-2.2%) |
| `LOOP_WRITE_INDEX_ROOTFIX` | 265.088 → 266.555 ms (+0.6%) | 61.259 → 60.791 ms (-0.8%) |
| `LOOP_REGRESSION_ROOTFIX` | 281.318 → 288.658 ms (+2.6%) | 61.889 → 63.573 ms (+2.7%) |
| `REGISTER_LIVENESS` | 271.222 → 276.367 ms (+1.9%) | 61.067 → 64.160 ms (+5.1%) |
| `LIVENESS_FOLLOWUP` | 271.590 → 273.923 ms (+0.9%) | 63.146 → 67.513 ms (+6.9%) |
| `FRAME_CSE_LIVENESS` | 295.077 → 303.795 ms (+3.0%) | 60.683 → 73.783 ms (+21.6%) |

La prima crescita osservata per `string_scan` appare con `LOOP_WRITESET`; per `struct_score` il peggioramento cresce soprattutto nei delta `REGISTER_LIVENESS`, `LIVENESS_FOLLOWUP` e `FRAME_CSE_LIVENESS`. I valori assoluti di baseline oscillano tra batch, perciò non attribuisco tutta la variazione a una singola patch soltanto sulla base di questa tabella.

## Contatori runtime su `dfs_rec`

Esecuzione strumentata di `dfs_rec.c`, `ACCEL=0`:

- Prima della correzione, `FnCodeInstall` aveva **15.001** ingressi, dei quali **10.000** sul ramo same-function. Dopo la correzione: **5.001** ingressi e **0** same-function.
- `FnCodeCompile`: **2** chiamate; **2** compilazioni effettive.
- `LowerStmtRec`: **36** chiamate.
- Nodi visitati dalla scansione del write-set: **295**.
- Ingressi nel corpo `IrExecLinLocalsRegs`: **15.001**.

Quindi lowering, scansione del write-set e compilazione non sono ripetuti a ogni chiamata ricorsiva. `FnCodeInstall` veniva invece invocata a ogni chiamata, ma il ramo same-function non reinstallava le tabelle: cancellava comunque tutte le 32 cache base. Il fix limita l'invalidazione ai soli slot `S->IdxN` attivi per la funzione, mantenendo `IdxDirty = 0`; il call site ricorsivo ora salta anche l'ingresso in `FnCodeInstall` quando le tabelle della stessa funzione sono già installate.

## Correzione del frame

Il controllo `BABYC_LIN_FRAME_PER_FUNCTION` precedente non era realmente collegato al ramo di allocazione; i risultati delle vecchie combinazioni di flag non isolavano quindi il costo del frame. Ora il flag controlla effettivamente il percorso.

La variante `alloca(RegCount)` aveva overhead misurabile. La soluzione finale genera frame locali statici specializzati per ogni conteggio da 1 a `IR_LIN_REGS` (64): il dispatch sceglie un frame della dimensione esatta di `FnCodeSlot.RegCount`, poi il corpo dell'interprete inizializza solo quei registri. Questo evita sia il frame fisso massimo sia l'allocazione dinamica a ogni invocazione. La variante diagnostica con il flag disattivato mantiene invece un buffer fisso.

La modifica a `FnCodeInstall` è stata mantenuta perché riduce il lavoro ripetuto nella ricorsione. Il dump IR di `dfs_rec` resta invariato rispetto al delta `FRAME_CSE_LIVENESS`: `main` 105 istruzioni, `dfs` 40. Il dump di `struct_score` mantiene 48 istruzioni in `main`; distribuzione degli opcode invariata. Questo indica che il recupero di prestazioni proviene dal percorso di esecuzione/frame, non da una riduzione artificiale del programma o da una forma di IR specifica del benchmark. Linear IR non ha un opcode `Move` dedicato.

## Benchmark finale — 45 round alternati aggregati, CPU fissata, ACCEL=0/1

Tre batch indipendenti da 15 round ciascuno, ordine baseline/corrente alternato e processi fissati alla CPU 1. La tabella riporta la mediana aggregata dei 45 tempi per ogni binario, benchmark e modalità.

| Benchmark | ACCEL | Baseline ms | Corrente ms | Variazione |
|---|---:|---:|---:|---:|
| `string_scan` | 0 | 268.851 | 254.519 | -5.33% |
| `string_scan_bounded` | 0 | 313.011 | 295.078 | -5.73% |
| `struct_score` | 0 | 64.046 | 63.232 | -1.27% |
| `dfs_rec` | 0 | 25.957 | 24.822 | -4.37% |
| `string_scan` | 1 | 6.504 | 6.447 | -0.88% |
| `string_scan_bounded` | 1 | 7.495 | 7.619 | +1.65% |
| `struct_score` | 1 | 1.520 | 1.554 | +2.24% |
| `dfs_rec` | 1 | 10.438 | 10.519 | +0.78% |

Con ACCEL=0 tutti e quattro i benchmark sono alla baseline o più veloci. Con ACCEL=1 tre sono entro −0,9%/+1,7%; `struct_score` è +2,24%, ma i tempi assoluti sono nell'ordine di 1,5 ms e la dispersione osservata è elevata (outlier fino a 5,8 ms), quindi non è evidenza di una regressione stabile. Il residuo `dfs_rec` ACCEL=1 è +0,78% aggregato, anch'esso inferiore alla dispersione misurata. Non si nascondono questi valori: i JSON allegati contengono tutte le 45 misure per confronto indipendente.

## Matrice anti-regressione

| Configurazione | Passati | Falliti | Timeout |
|---|---:|---:|---:|
| Tree, ACCEL=0 | 996 | 0 | 0 |
| Tree, ACCEL=1 | 996 | 0 | 0 |
| Linear STRICT, ACCEL=0 | 979 | 17 | 0 |
| Linear STRICT, ACCEL=1 | 979 | 17 | 0 |

Elenco storico scritto prima della rinumerazione: 046→047, 057→058, 061→062, 121→126, 280→288, 441→454, 610→606, 613→609, 622→618, 661 (SQLite)→651, 785→789, 942→873, 943→874, 959→893, 1033→947, 1034→948. Il vecchio 379 era ambiguo per la presenza di due sorgenti con quel prefisso; oggi sono `395_array_fn_loop_local_global` e `396_complex_inventory`, entrambi PASS. I casi 288, 789 e 874 sono stati risolti successivamente. I 13 residui correnti sono 047, 058, 062, 126, 454, 606, 609, 618, 651, 873, 893, 947, 948. Build Linux/GCC pulita; MSVC non è stato eseguito in questo ambiente.

## Replica indipendente

Un secondo batch di 15 round alternati, anch'esso fissato alla CPU 1, ha confermato il risultato entro la variabilità del timer:

| Benchmark | ACCEL | Baseline ms | Corrente ms | Variazione |
|---|---:|---:|---:|---:|
| `string_scan` | 0 | 269.229 | 257.924 | −4.20% |
| `string_scan_bounded` | 0 | 310.103 | 289.644 | −6.60% |
| `struct_score` | 0 | 62.758 | 61.953 | −1.28% |
| `dfs_rec` | 0 | 24.734 | 23.883 | −3.44% |
| `string_scan` | 1 | 5.928 | 5.864 | −1.08% |
| `string_scan_bounded` | 1 | 7.226 | 7.159 | −0.93% |
| `struct_score` | 1 | 1.777 | 1.577 | −11.26% |
| `dfs_rec` | 1 | 10.292 | 10.354 | +0.60% |

`dfs_rec` ACCEL=1 è l'unico punto lievemente sopra la baseline in entrambi i batch (+1.07% e +0.60%); la differenza è inferiore alla dispersione osservata nei tempi di baseline. Non viene nascosta: è riportata come residuo da monitorare, non come miglioramento dimostrato.
