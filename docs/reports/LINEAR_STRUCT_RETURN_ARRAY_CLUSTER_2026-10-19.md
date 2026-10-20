# Linear STRICT — ritorno struct assegnato ad array locale (2026-10-19)

## Baseline e risultato

Fonte esclusiva: `babyc_baseline_2026-10-18_loop_promotion.zip` (SHA-256 `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`). Applicato il cumulativo verificato fino a `qsort_comparators`, quindi una modifica di lowering in `src/ir_lin_compile.c`. Nessun `.expect` è stato modificato e non è stato necessario aggiungere un test: `893_vm_struct_return_effect_fallback.c/.expect` è già il repro diretto e ora passa.

## Causa radice dimostrata

Il test contiene:

```c
struct S { int a; int b; };
struct S v[1];
v[0] = make_effect(7);
```

Prima del fix il dump era:

```text
[call] make_effect ...
[call] args=1
[call] success
[call] printf ...
[call] args=4
[call] success
[babyc-exec-profile] compile_reject=lower body_kind=2 n=19 ok=1 skipped_call=1 calls=2
[vm-rej] compile-null
893_vm_struct_return_effect_fallback.c:1:18 strict: tree fallback (body)
```

Il valore `skipped_call=1` era fuorviante: le tracce `CALL_TRACE` provano che `make_effect` e `printf` venivano entrambi accettati. Il dump dei metadati AST (`struct_return_array_ast_metadata_2026-10-19.log`) individua la dichiarazione che faceva fallire il corpo:

```text
[decl-meta] line=12 name=v slot=0 arr=1 ptr=1 elemPtr=0 fptr=0 from=16 fromSize=8 elemSize=8 expr=-1
```

`DeclArraySize==1`, `DeclIsPtr=1`, tipo elemento `TypeStruct` (`from=16`) e nessun inizializzatore descrivono un array locale di un solo struct. Il lowering gestiva l’allocazione degli array non-pointer solo con `DeclArraySize > 1`, perché il valore 1 viene anche usato come sentinella per uno struct scalare. L’array `v[1]` saltava quindi l’allocatore `IR_K_DECL_ARRAY` e cadeva nel ramo generico delle dichiarazioni pointer/array, che marchiava il corpo come parzialmente saltato. Di conseguenza la copia dell’aggregato restituito non raggiungeva il compilatore IR.

Un primo criterio tentativo basato soltanto su `DeclIsPtr` ha fatto fallire quattro test di temporanei struct inizializzati da chiamate (`506`, `530`, `635`, `946`). Ho ristretto la condizione: il percorso array unitario viene ammesso solo quando `DeclArraySize==1`, `DeclIsPtr` è vero, `Expr==NULL` e non esistono inizializzatori. I temporanei aggregati inizializzati continuano a usare il percorso di copia già previsto. I quattro test temporaneamente rotti sono stati rieseguiti e sono tornati conformi.

## Dump dopo e correttezza

Il dump post-fix (`struct_return_array_after_2026-10-19.log`) contiene 20 istruzioni per `main`, inclusi:

```text
[lin-ir] function=main ... instructions=20
[lin-ir] 0001 op=15 ... tok=7 imm=0
...
[lin-ir] 0010 op=12 dest=0 a=1 b=6 tok=3 imm=8
```

L’istruzione `tok=3 imm=8` è la copia by-value degli 8 byte dallo stable return blob della chiamata alla destinazione dell’elemento di array. `make_effect` ha un body Linear emesso (18 istruzioni), nessun `compile_reject`, nessun `compile-null`, e il trace bytecode riporta zero tentativi/runs: il percorso non usa bytecode né fallback Tree.

Output di `893` in Tree e Linear STRICT con entrambi ACCEL:

```text
7 1 1
```

Tutti i quattro run terminano con exit code 0 e l’output corrisponde all’`.expect`. Vedere `struct_return_array_target_outputs_2026-10-19.txt`.

## Build e matrice finale

Comandi eseguiti:

```sh
make -C src clean
make -C src -j2
env -i PATH="$PATH" HOME="$HOME" python3 tools/run_numbered_matrix.py --path tree --accel 0 --log docs/reports/matrix_893_tree_accel0_2026-10-19.log
env -i PATH="$PATH" HOME="$HOME" python3 tools/run_numbered_matrix.py --path tree --accel 1 --log docs/reports/matrix_893_tree_accel1_2026-10-19.log
env -i PATH="$PATH" HOME="$HOME" python3 tools/run_numbered_matrix.py --path linear --strict --accel 0 --log docs/reports/matrix_893_linear_accel0_STRICT_2026-10-19.log
env -i PATH="$PATH" HOME="$HOME" python3 tools/run_numbered_matrix.py --path linear --strict --accel 1 --log docs/reports/matrix_893_linear_accel1_STRICT_2026-10-19.log
```

Build: exit code 0, zero warning/error. Matrice reale (1.002 test con `.expect`):

```text
SUMMARY tree_accel0: PASSED=1002 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1002 SECONDS=4.13
SUMMARY tree_accel1: PASSED=1002 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1002 SECONDS=4.04
SUMMARY linear_accel0_STRICT: PASSED=996 FAILED=6 TIMEOUT=0 SKIPPED=0 TOTAL=1002 SECONDS=3.56
SUMMARY linear_accel1_STRICT: PASSED=996 FAILED=6 TIMEOUT=0 SKIPPED=0 TOTAL=1002 SECONDS=3.54
```

I sei residui, identici con ACCEL=0/1, sono `047_grep`, `058_macro_bug`, `062_initializers`, `126_csmith_modulo_zero`, `651_sqlite_exec_null_varargs`, `873_alignment_extended`. Nessun timeout, nessuna regressione nuova. Il caso `893` è passato; `506`, `530`, `635`, `946` sono stati controllati dopo il tentativo non corretto e passano nella versione finale.

## Benchmark A/B contro la baseline originale

Baseline e candidato sono stati compilati separatamente dalla baseline originale e dal cumulativo finale con GCC `-Wall -O2`, stessa configurazione Linear STRICT, `BABYC_ACCEL=0/1`; ogni benchmark/config ha almeno sette coppie alternate AB/BA e output identico. Le quattro combinazioni ACCEL=1 sono state estese a 21 coppie totali (7 iniziali + 14 aggiuntive). Il tempo comprende l’avvio di BabyC, parsing/lowering e l’esecuzione; i casi ACCEL=1 sono brevi e quindi più rumorosi.

| Benchmark | ACCEL | n coppie | Baseline mediana | Candidato mediana | Delta mediane | Mediana delta appaiato | IQR delta appaiato |
|---|---:|---:|---:|---:|---:|---:|---:|
| `string_scan` | 0 | 7 | 317.912 ms | 306.738 ms | −3.51% | −4.38% | −7.22…−3.55% |
| `string_scan_bounded` | 0 | 7 | 379.407 ms | 364.986 ms | −3.80% | −3.30% | −5.93…−2.79% |
| `struct_score` | 0 | 7 | 70.537 ms | 67.598 ms | −4.17% | −4.62% | −5.78…−2.95% |
| `dfs_rec` | 0 | 7 | 26.156 ms | 24.464 ms | −6.47% | −1.66% | −2.47…+0.81% |
| `string_scan` | 1 | 21 | 5.995 ms | 5.796 ms | −3.32% | −1.24% | −18.50…+1.73% |
| `string_scan_bounded` | 1 | 21 | 7.538 ms | 7.207 ms | −4.40% | −2.67% | −4.19…+1.80% |
| `struct_score` | 1 | 21 | 1.489 ms | 1.548 ms | +3.95% | −0.68% | −3.61…+6.90% |
| `dfs_rec` | 1 | 21 | 10.260 ms | 10.262 ms | +0.02% | +0.80% | −4.44…+5.05% |

I campioni sfavorevoli dei due workload ACCEL=1 più brevi non sono persistenti nelle differenze appaiate: gli intervalli attraversano lo zero e le mediane appaiate sono entro l’1% per `struct_score` e `dfs_rec`. Tutti gli output sono identici (`380000`, `7500000`, `15000`). I grezzi e il riepilogo sono in `struct_return_perf_ab_samples_2026-10-19.csv`, `struct_return_perf_ab_accel1_repeat_2026-10-19.csv` e `struct_return_perf_ab_summary_2026-10-19.csv`.

## Stato aperto

Il residuo `893` è chiuso. Restano sei fallimenti STRICT elencati sopra. Il codice aggiunto è ISO C portabile (nessun header POSIX o simbolo OS-specifico), ma la build MSVC/Windows non è stata eseguita in questo ambiente. Bytecode rimane fuori ambito.
