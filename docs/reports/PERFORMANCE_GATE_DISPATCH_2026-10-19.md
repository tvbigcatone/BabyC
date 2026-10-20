# BabyC — chiusura del gate prestazionale Linear STRICT (2026-10-19)

## Baseline e risultato

Baseline esclusiva: `babyc_baseline_2026-10-18_loop_promotion.zip` (SHA-256 `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`). Il confronto è contro questa baseline pulita, non contro il turno precedente. Il candidato include il delta funzionale già verificato, la rinumerazione e il fix prestazionale documentato qui.

**Causa radice confermata e corretta:** i controlli per l’accesso a un array fisso raggiunto attraverso membri erano inseriti nel corpo condiviso di `IrLinLoadIndex` e `IrLinStoreIndex`. La condizione legacy (`Tok==0`, indice valido, consultazione `IdxMetaKind`, `IX`, `LinParser`) veniva valutata anche per gli accessi ordinari, una volta per accesso indicizzato nel loop caldo. Non è una deduzione dai soli tempi: è stato eseguito un test controllato che ripristina solo la guardia larga, mantenendo il resto del candidato uguale.

## Prove della causa radice

1. **Bisezione per file, `string_scan` ACCEL=0, 7 coppie alternate, core CPU 1:** tornando al solo `src/ir_lin_exec.c` della baseline, mediana baseline 319,672 ms, probe 320,131 ms, delta +0,14% (mediana appaiata +1,72%). Revertendo invece `src/ir_lin_emit.c` il delta resta +3,31%; revertendo `src/ir_lin_compile.c` resta +5,25%. Il guasto è localizzato al dispatch executor.
2. **A/B causale sulla guardia, con output invariato:** riattivare la condizione larga `Tok==0` rallenta `string_scan` da 301,582 a 334,894 ms (+11,046% tra mediane; mediana appaiata +10,61%). In `string_scan_bounded` il tempo passa da 358,247 a 397,228 ms (+10,881%; mediana appaiata +10,881%). Tutte le sette coppie di entrambi i casi sono più lente con la guardia larga; output in entrambi i casi `380000`.
3. La prova spiega la vecchia regressione +5,8% su `string_scan` ACCEL=0: il lavoro extra non era nel loop unrolled della promozione, ma nelle verifiche metadata/bounds introdotte nel percorso d’accesso comune.

## Correzione implementata

- Aggiunti in coda a `IrLinOp` due opcode dedicati: `IrLinLoadMemberArray` e `IrLinStoreMemberArray`. I valori degli opcode precedenti rimangono invariati.
- `src/ir_lin_emit.c` emette questi opcode soltanto per l’elemento di un array fisso raggiunto via catena di membri, per assegnamenti e ++/--.
- `src/ir_lin_exec.c` gestisce questi opcode nei case dedicati. I blocchi speciali sono rimossi dai case comuni `IrLinLoadIndex`/`IrLinStoreIndex`; l’accesso ordinario non esegue più il confronto `Tok==9` né la catena di controlli per `IdxMetaKind==5`.
- `src/ir_lower.c` riconosce i nuovi opcode come accessi memoria e ne tratta correttamente gli effetti/registri nei passaggi d’analisi.
- Il caso mirato `src/tests/874_array_member_storage.c` stampa `10 22 30 61`, exit 0, con ACCEL=0 e ACCEL=1. I controlli di bounds e dell’init/ownership map restano nel percorso dedicato.

## Build e matrice complete

Comando di build finale:

```sh
make -C src clean && make -C src -j2
```

Output effettivo: `BUILD_RC=0`, `WARNING_COUNT=0`, `ERROR_COUNT=0` (GCC/Linux, `-Wall -O2`).

Comandi della matrice, eseguiti dopo clean build:

```sh
python3 tools/run_numbered_matrix.py --path tree --accel 0 --log docs/reports/matrix_perf_final_tree_accel0_2026-10-19.log
python3 tools/run_numbered_matrix.py --path tree --accel 1 --log docs/reports/matrix_perf_final_tree_accel1_2026-10-19.log
python3 tools/run_numbered_matrix.py --path linear --strict --accel 0 --log docs/reports/matrix_perf_final_linear_accel0_STRICT_2026-10-19.log
python3 tools/run_numbered_matrix.py --path linear --strict --accel 1 --log docs/reports/matrix_perf_final_linear_accel1_STRICT_2026-10-19.log
```

Output dei quattro riepiloghi:

```text
SUMMARY tree_accel0: PASSED=1001 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1001 SECONDS=4.19
SUMMARY tree_accel1: PASSED=1001 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1001 SECONDS=4.31
SUMMARY linear_accel0_STRICT: PASSED=988 FAILED=13 TIMEOUT=0 SKIPPED=0 TOTAL=1001 SECONDS=3.88
SUMMARY linear_accel1_STRICT: PASSED=988 FAILED=13 TIMEOUT=0 SKIPPED=0 TOTAL=1001 SECONDS=3.59
```

I due comandi Linear ritornano codice 1 perché il runner usa il codice finale per segnalare i 13 fail già noti, non un errore di build o timeout. L’elenco è identico fra ACCEL=0/1: `047_grep`, `058_macro_bug`, `062_initializers`, `126_csmith_modulo_zero`, `454_complex_dispatch`, `606_storm_inc_dec_member_call`, `609_storm_enum_union_fptr`, `618_inc_member_call_arg`, `651_sqlite_exec_null_varargs`, `873_alignment_extended`, `893_vm_struct_return_effect_fallback`, `947_qsort_bsearch`, `948_qsort_stable`. Nessuna regressione nuova.

## Benchmark A/B ufficiale sui quattro sorgenti originali

Protocollo: baseline e candidato costruiti separatamente da sorgenti; variabili `BABYC_*` ereditate rimosse, quindi `BABYC_PATH=linear BABYC_STRICT=1 BABYC_ACCEL=0/1`; core CPU 1; un warm-up non misurato; sette coppie alternate AB/BA per ciascuna delle otto combinazioni; output ed exit code confrontati. I valori in tabella sono le mediane wall-clock delle sette esecuzioni per binario; la colonna appaiata è la mediana dei sette delta `(candidato/baseline - 1)`.

| Benchmark | ACCEL | Baseline mediana (ms) | Candidato mediana (ms) | Delta fra mediane | Mediana delta appaiato |
|---|---:|---:|---:|---:|---:|
| `string_scan` | 0 | 327,652 | 307,187 | −6,25% | −6,74% |
| `string_scan_bounded` | 0 | 397,104 | 397,311 | +0,05% | −1,08% |
| `struct_score` | 0 | 81,115 | 74,417 | −8,26% | −7,28% |
| `dfs_rec` | 0 | 28,826 | 27,623 | −4,17% | −5,11% |
| `string_scan` | 1 | 7,426 | 7,629 | +2,73% | −1,61% |
| `string_scan_bounded` | 1 | 9,631 | 9,296 | −3,49% | −8,57% |
| `struct_score` | 1 | 4,262 | 4,388 | +2,96% | −4,08% |
| `dfs_rec` | 1 | 13,641 | 14,624 | +7,20% | +3,70% |

Output identici nelle coppie: `string_scan` e `string_scan_bounded` → `380000`; `struct_score` → `7500000`; `dfs_rec` → `15000`. Tutti i tempi grezzi e i delta per coppia sono in `performance_ab_samples_2026-10-19.csv`; per rendere esplicito il rumore sono registrati anche i campioni CPU-time indipendenti.

**Interpretazione necessaria:** i workload ACCEL=1 durano solo circa 4–14 ms. In queste misure originali alcuni rapporti di mediane e alcune medie appaiate cambiano segno fra serie consecutive. Perciò non presento la singola serie wall-clock come prova di regressione stabile, né scelgo soltanto le prove favorevoli. Ho aggiunto ripetizioni di confronto in copie temporanee con dieci volte le iterazioni (non incluse nel progetto/delta), mantenendo la stessa forma del programma, lo stesso binario e la stessa affinità CPU.

Risultati supplementari ACCEL=1, 21 coppie alternate su sorgenti scalate ×10:

| Benchmark scalato | N coppie | Tempo CPU: mediana base → candidato | Delta fra mediane | Mediana delta appaiato CPU | IQR dei delta appaiati CPU |
|---|---:|---:|---:|---:|---:|
| `string_scan` | 21 | vedi CSV raw | +1,41% | +0,04% | −1,47% … +2,07% |
| `string_scan_bounded` | 21 | vedi CSV raw | −0,09% | −1,23% | −4,14% … +3,13% |
| `struct_score` | 28 (7+21) | vedi CSV raw | +2,08% | +0,07% | −5,53% … +7,06% |
| `dfs_rec` | 7 | vedi CSV raw | −3,24% | −4,14% | −6,13% … +0,07% |

Per `struct_score`, il test scalato aggiuntivo da 21 coppie ha da solo mediana appaiata CPU −1,67%; la combinazione di due serie da 7+21 coppie è sensibile all’ordine/deriva (rapporto delle mediane +2,08%, mediana appaiata +0,07%, IQR −5,53%…+7,06%). Ho esteso anche `dfs_rec` ACCEL=1 a 21 coppie scalate: tempo CPU 83,43→82,88 ms (rapporto delle mediane −0,66%, mediana dei delta appaiati +0,30%, IQR −1,33%…+2,58%); wall 85,55→85,52 ms (−0,03% tra mediane, +0,57% appaiato, IQR −1,25%…+2,69%). I dati non mostrano un peggioramento persistente >1% nelle mediane appaiate scalate. Questa è la base per trattare le deviazioni ACCEL=1 delle esecuzioni originali brevi come rumore misurato, non per affermare che ogni singolo campione sia più veloce.

## Stato del gate e limiti

- **La regressione originale `string_scan` ACCEL=0 è chiusa e la causa è provata:** sul benchmark ufficiale finale il candidato è −6,74% nella mediana appaiata; la guardia larga ripristinata causa invece +10,61% misurato nella prova controllata.
- Nelle altre sette combinazioni non emerge una regressione >1% persistente oltre la variabilità osservata quando si considerano mediana appaiata, serie CPU e ripetizioni scalate. Le esecuzioni ACCEL=1 originali molto brevi sono rumorose; i dati completi sono distribuiti, non occultati.
- La matrice di correttezza conserva esattamente i 13 residui già esistenti. Quindi il gate prestazionale di questo set Linux/GCC è considerato chiuso secondo il criterio “nessuna regressione >1% distinta dal rumore misurato”; il gate di promozione a Linear come default resta aperto perché Strict ha 13 fallimenti.
- Non eseguiti: MSVC/Windows e bytecode, esclusi dall’obiettivo di questo turno.

## Comandi A/B effettivi

Comandi effettivamente eseguiti (gli script erano temporanei e usavano gli alberi assoluti in `/mnt/data/perf_gate_work`):

```sh
python3 /mnt/data/perf_gate_work/bisect_stable.py
python3 /mnt/data/perf_gate_work/isolate_guard_cost.py
python3 /mnt/data/perf_gate_work/bench_alt_final_opcode.py
python3 /mnt/data/perf_gate_work/bench_scaled_scans21.py
python3 /mnt/data/perf_gate_work/bench_scaled_struct21.py
python3 /mnt/data/perf_gate_work/bench_scaled_dfs21.py
```

La serie ufficiale `bench_alt_final_opcode.py` ha eseguito un warm-up e sette coppie per ciascuno degli 8 casi, alternando `AB/BA`; le copie temporanee `bench/perf_scaled/*.c` aumentano di 10× soltanto il numero d’iterazioni e sono state rimosse dopo le misure. Ogni esecuzione usa `BABYC_PATH=linear BABYC_STRICT=1 BABYC_ACCEL=<0|1>`, rimuove le altre variabili `BABYC_*`, fissa la CPU 1 e confronta exit code e stdout. I dati sono persistiti in `performance_ab_samples_2026-10-19.csv`; `performance_guard_isolation_2026-10-19.csv` contiene tutti i sette delta della prova causale; `performance_file_bisection_2026-10-19.csv` documenta la bisezione. La matrice completa è stata eseguita con i quattro comandi sopra riportati e output nei corrispondenti `.log`.
