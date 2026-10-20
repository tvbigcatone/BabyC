# Linear STRICT — chiusura `873_alignment_extended` (2026-10-19)

## Baseline e perimetro

Baseline esclusiva: `babyc_baseline_2026-10-18_loop_promotion.zip`, SHA-256 `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`. Il candidato contiene i delta cumulativi già verificati (rinumerazione, gate performance, member/call, qsort comparator, struct-return array) più questo fix.

La sola modifica al motore in questo turno è `src/ir_lin_emit.c`. È aggiunto il test `1011_nested_embedded_volatile_inc.c/.expect`. Nessun `.expect` preesistente è stato modificato; diff byte-per-byte contro il candidato cumulativo precedente: 1009 aspettative precedenti, zero mancanti, zero differenze. Il test 1011 verifica incrementi prefix/postfix su catene di membri incorporati, con root globale e locale.

## Causa radice provata

Il costrutto respinto è l'incremento postfix `n.wide.vcount++` (riga 16 di `873_alignment_extended.c`), dove `Nest` incorpora `Wide` e `Wide.vcount` è `volatile int`. Il problema non è una direttiva `_Alignas` che richiedesse un'estensione del parser: il test mette sotto stress layout/padding di aggregati, qualificatore `volatile`, union e bitfield. Il rifiuto nasce nel percorso STRICT del lowering di incremento su membro annidato.

Comando pre-fix, dalla directory `src/tests` con binario della build candidata precedente:

```sh
env BABYC_PATH=linear BABYC_STRICT=1 BABYC_ACCEL=0 BABYC_DUMP_LIN=1 BABYC_VM_PROFILE=1 ../babyc 873_alignment_extended.c
```

Output diagnostico pre-fix:

```text
[babyc-exec-profile] lower_reject stmt_kind=1 line=16 expr_kind=6 expr_op=33
[babyc-exec-profile] compile_reject=lower body_kind=2 n=0 ok=0 skipped_call=0 calls=0
#include <stdio.h>
      ^
873_alignment_extended.c:1:6 strict: tree fallback (body)
[babyc-bytecode-vm-prod] attempts=0 runs=0
```

`expr_kind=6` individua l'espressione unary e `expr_op=33` è `TokenIncrement`. Il corpo viene rifiutato durante il lowering, prima di generare IR utilizzabile: questo è il punto di divergenza Tree/Linear e prova che non è una differenza nell'esecuzione runtime.

La condizione errata in `src/ir_lin_emit.c` faceva seguire la catena di `IrExMember` solo se un membro intermedio era di tipo puntatore (`has_ptr`). Una catena con struct incorporata (`n.wide`) non contiene quel puntatore e veniva scartata, benché il layout dei membri fosse disponibile. In più, il type walk per un root globale non interrogava il tipo dichiarato globale quando non c'era un tipo locale/parametro. Questi due vincoli rendevano incompleta la risoluzione del membro, non il tipo volatile o il padding.

## Fix generale

Il lowering ora conserva il tipo dell'aggregato incorporato senza richiedere un membro puntatore intermedio, segue il tipo dichiarato di ogni campo e recupera il tipo radice di un identificatore globale tramite il resolver globale condiviso. Per un membro padre incorporato genera l'operazione esistente di indirizzamento sicuro del membro (`IrLinLoadIndex` con `Tok=5` e metadata AST); quindi calcola l'offset del campo finale e applica load/arithmetic/store tipizzati. I percorsi in cui il parent è davvero un puntatore mantengono il precedente flusso. Non ci sono controlli sul nome del test o del campo, nuovi header o estensioni GCC-specifiche.

Dump post-fix: `docs/reports/alignment_873_after_2026-10-19.log`. Il tratto IR per la riga incriminata contiene l'indirizzamento del membro, l'offset `4` di `vcount`, un load di `4` byte, incremento di uno e store tipizzato di `4` byte; il body compila in 80 istruzioni e non invoca Tree né bytecode. L'output atteso e osservato è:

```text
1 9 4.75 5 5 17 25
42 43
```

La stessa sorgente e il nuovo test `1011_nested_embedded_volatile_inc` sono stati eseguiti in Tree e Linear STRICT, entrambi con ACCEL=0/1: exit code 0, output identico all'`.expect` in tutti i casi. Il test 1011 stampa `5 3 5 9 7 9`.

## Build e matrice completa

Comandi eseguiti:

```sh
make -C src clean
make -C src -j2
env -i PATH="$PATH" HOME="$HOME" python3 tools/run_numbered_matrix.py --path tree --accel 0 --log docs/reports/matrix_alignment_tree_accel0_2026-10-19.log
env -i PATH="$PATH" HOME="$HOME" python3 tools/run_numbered_matrix.py --path tree --accel 1 --log docs/reports/matrix_alignment_tree_accel1_2026-10-19.log
env -i PATH="$PATH" HOME="$HOME" python3 tools/run_numbered_matrix.py --path linear --strict --accel 0 --log docs/reports/matrix_alignment_linear_accel0_STRICT_2026-10-19.log
env -i PATH="$PATH" HOME="$HOME" python3 tools/run_numbered_matrix.py --path linear --strict --accel 1 --log docs/reports/matrix_alignment_linear_accel1_STRICT_2026-10-19.log
```

Output reale dopo clean build e verifica sulla copia ricostruita dallo ZIP finale:

```text
BUILD_RC=0 WARNINGS=0 ERRORS=0
SUMMARY tree_accel0: PASSED=1003 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1003 SECONDS=4.31
FAILURES: none
SUMMARY tree_accel1: PASSED=1003 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1003 SECONDS=3.98
FAILURES: none
SUMMARY linear_accel0_STRICT: PASSED=998 FAILED=5 TIMEOUT=0 SKIPPED=0 TOTAL=1003 SECONDS=3.58
FAILURES: 047_grep.c 058_macro_bug.c 062_initializers.c 126_csmith_modulo_zero.c 651_sqlite_exec_null_varargs.c
SUMMARY linear_accel1_STRICT: PASSED=998 FAILED=5 TIMEOUT=0 SKIPPED=0 TOTAL=1003 SECONDS=3.50
FAILURES: 047_grep.c 058_macro_bug.c 062_initializers.c 126_csmith_modulo_zero.c 651_sqlite_exec_null_varargs.c
```

## Benchmark A/B contro la baseline originale

Baseline e candidato sono stati compilati separatamente dalla baseline originale con gli stessi flag. Lo script temporaneo `run_perf_ab.py` fissa l'affinità a CPU 0, neutralizza i selettori `BABYC_*` ereditati, esegue `BABYC_PATH=linear BABYC_STRICT=1` e verifica output/exit code per ogni esecuzione. Sono state eseguite 7 coppie alternate AB/BA per ciascuna delle 8 combinazioni; dove la prima mediana grezza era oltre +1%, `string_scan_bounded` ACCEL=1 e `struct_score` ACCEL=1 sono stati estesi a 21 coppie. Totale 168 esecuzioni, tutte con output identici.

| Benchmark | ACCEL | Coppie | Baseline mediana | Candidato mediana | Delta delle mediane | Mediana delta appaiato | Q1…Q3 delta appaiato |
|---|---:|---:|---:|---:|---:|---:|---:|
| dfs_rec | 0 | 7 | 24.904 ms | 24.541 ms | -1.46% | -1.02% | -1.78%…+0.13% |
| dfs_rec | 1 | 7 | 10.476 ms | 10.290 ms | -1.78% | -1.57% | -3.92%…+1.29% |
| string_scan | 0 | 7 | 319.207 ms | 305.066 ms | -4.43% | -4.36% | -5.40%…-3.79% |
| string_scan | 1 | 7 | 6.183 ms | 4.826 ms | -21.95% | -18.87% | -21.91%…-6.32% |
| string_scan_bounded | 0 | 7 | 372.368 ms | 374.486 ms | +0.57% | +0.07% | -1.07%…+1.77% |
| string_scan_bounded | 1 | 21 | 7.255 ms | 7.051 ms | -2.81% | -0.48% | -5.40%…+1.62% |
| struct_score | 0 | 7 | 72.581 ms | 68.604 ms | -5.48% | -7.95% | -8.77%…-5.83% |
| struct_score | 1 | 21 | 1.478 ms | 1.478 ms | +0.05% | +1.55% | -6.35%…+5.08% |

Le serie ACCEL=1 da 1.5–7 ms hanno una dispersione molto grande fra le coppie (ad esempio `struct_score` ACCEL=1: IQR appaiato circa −6.35%…+5.08%; rapporto fra mediane grezze +0.05%). Sono conservati campioni individuali in `alignment_perf_ab_samples_2026-10-19.csv`; riepilogo con quartili in `alignment_perf_ab_summary_2026-10-19.csv`. Il segnale non supporta un peggioramento persistente >1% oltre il rumore misurato. `string_scan_bounded` ACCEL=0 ha una differenza grezza +0.57% e mediana appaiata +0.07%, entrambe entro il gate.

## Residui e limiti

I cinque residui Linear STRICT attuali sono `047_grep`, `058_macro_bug`, `062_initializers`, `126_csmith_modulo_zero`, `651_sqlite_exec_null_varargs`. Nessuno condivide la causa dimostrata del membro incrementato in una catena di struct incorporati, quindi non è stato alterato speculativamente. Tree resta il default/oracolo finché questi residui non vengono chiusi.

Modifica motore: soltanto `src/ir_lin_emit.c`. Nuovo codice non usa header POSIX, builtins GCC o estensioni specifiche del compilatore; tuttavia MSVC/Windows non è stato eseguito in questo turno. Bytecode non è stato coinvolto.
