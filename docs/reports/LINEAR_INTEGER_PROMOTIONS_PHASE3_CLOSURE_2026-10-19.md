# BabyC — Fase 3: promozioni intere, conversioni usuali e signedness

Data: 2026-10-19. Baseline cumulativa: `babyc_baseline_2026-10-18_loop_promotion.zip`, con `DELETE_FILES.txt` applicato prima dell'overlay.

## Esito sintetico

La correzione e la batteria iniziale sono consegnate. I sei nuovi test numerati `1018`–`1023` hanno `.expect` identici all'output GCC `-std=c11 -O0` sullo stesso host. Le quattro matrici richieste passano 1015/1015; il gate Linear non-STRICT certifica `tree_body=0` con ACCEL 0 e 1. `run_numbered_matrix.py --log /tmp/...` è stato rieseguito realmente e termina con exit code 0, stampando il path assoluto.

**Il gap è parzialmente chiuso, non esaustivo.** La selezione usual-arithmetic-conversion è ora centralizzata, ma restano forme di espressione il cui tipo è ricostruito conservativamente da flag IR, target non Linux/LP64 e combinazioni di tipo/operatori non rappresentate nei test.

## Causa radice

L'emettitore Linear e l'evaluatore condiviso sceglievano signedness/width con euristiche sui singoli operandi (per esempio “uno degli operandi è unsigned/wide”), invece di promuoverli e scegliere il tipo comune secondo rank e capacità di rappresentazione. Per gli shift, il tipo dell'operando destro influenzava indebitamente la decisione: il tipo promosso del solo operando sinistro determina signedness e larghezza dell'operazione.

## Implementazione

- `src/ir_ops.c`: introdotti `IrIntegerPromotionBase`, `IrUsualArithmeticCommonIntegerBase`, `IrExprBinaryUsesUnsigned` e `IrExprBinaryUsesWide`. Il tipo comune considera rank, signedness e larghezza del tipo sul target host; gli shift usano una regola distinta basata sull'operando sinistro promosso.
- `src/expression.c`: l'operazione intera condivisa usa la stessa classificazione del tipo comune per determinare signedness/width, incluse le operazioni a 32/64 bit.
- `src/ir_lin_emit.c`: i flag `IR_LIN_F_UNSIGNED`/`IR_LIN_F_WIDE` delle operazioni binarie vengono ricavati dalla regola comune; rimossa la scorciatoia che trasformava certi shift su cast in unsigned-wide.
- `src/ir_runtime.h`: dichiarazioni degli helper condivisi.
- `tools/run_numbered_matrix.py`: il rendering del path passato a `--log` cattura `ValueError` da `relative_to(project)` e stampa il percorso assoluto quando il log è esterno al repository.
- `docs/LINEAR_COVERAGE_MAP.md`: aggiornato lo stato del gap intero e le limitazioni ancora aperte.

## Nuovi test e GCC

| Test | Aree coperte |
|---|---|
| `1018_integer_promotions_signedness` | Promozioni di `char`, `signed/unsigned char`, `short`, `unsigned short`; confronti signed/unsigned fra `int`, `long`, `unsigned int` e `unsigned long`; ternario. |
| `1019_integer_shift_divmod` | Shift unsigned al bit alto a 32/64 bit, right-shift signed negativo, divisione e resto con segni misti e con unsigned. |
| `1020_integer_narrow_call_return` | Cast e conversioni restringenti/estendenti, passaggio argomenti e ritorno, tipi stretti e `unsigned long`. |
| `1021_unsigned_wrap_compound` | Wraparound unsigned e `+=`, `<<=`, `>>=`, `/=`, `%=` su vari tipi, inclusi tipi stretti. |
| `1022_integer_conditional_conversions` | `?:` con tipi interi di rank e signedness differenti, confronti misti. |
| `1023_integer_bitfields` | Bitfield signed/unsigned, larghezze 5/9, aggiornamenti composti, divisione e shift. |

Ogni `.expect` è stato confrontato con il programma costruito da GCC usando `-std=c11 -O0`; tutti e sei i diff sono vuoti (log `phase3_integer_gcc_o0_expect_check.log`). Gli `1018` e `1019` erano presenti come bozze nel sandbox e i relativi `.expect` sono stati riallineati a GCC; `1020`–`1023` sono stati verificati e non hanno richiesto una modifica dell’aspettativa. Nessun `.expect` dei test storici `001`–`1017` è stato modificato. I test intenzionalmente contengono confronti fra signed/unsigned, quindi un GCC invocato con `-Wall -Wextra` può riportare avvisi `-Wsign-compare`; questo non è un warning della build di BabyC.

Nota di portabilità: `-8 >> 2` è implementation-defined in C; il test 1019 fissa l'output GCC della piattaforma di validazione, non una verità portabile a tutte le implementazioni. I casi undefined (signed overflow, shift count fuori range, `INT_MIN / -1`) non vengono usati per asserire output conforme a GCC.

## Matrice reale sulla build del candidato

Build clean: `phase3_integer_build_clean.log` e `phase3_integer_build_clean_retry.log`; nessun match `warning:` o `error:`.

```text
SUMMARY tree_accel0: PASSED=1015 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1015 SECONDS=14.76
FAILURES: none
SUMMARY tree_accel1: PASSED=1015 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1015 SECONDS=13.94
FAILURES: none
SUMMARY linear_accel0_STRICT: PASSED=1015 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1015 SECONDS=10.71
FAILURES: none
SUMMARY linear_accel1_STRICT: PASSED=1015 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1015 SECONDS=9.29
FAILURES: none
SUMMARY linear_accel0: PASSED=1015 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1015 SECONDS=13.84
TREE_FALLBACK_GATE: enabled violations=0 tree_body=0
FAILURES: none
SUMMARY linear_accel1: PASSED=1015 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1015 SECONDS=9.76
TREE_FALLBACK_GATE: enabled violations=0 tree_body=0
FAILURES: none
```

I log completi sono in `docs/reports/phase3_integer_matrix/`. Il test separato della regressione `--log` esterno ha riportato `RC=0`, `PASSED=1015/1015`, `tree_body=0`, e `LOG=/tmp/babyc_phase3_abs_log_test.log`; stdout conservato in `phase3_integer_external_log_path_test_stdout.log`.

## A/B alternato vs binario 047

Il confronto ha usato `BABYC_PATH=linear`, output verificati identici a ogni coppia e `AB`/`BA` alternati. I conteggi finali sono: 21 coppie per `string_scan` ACCEL 0/1; 7 e 21 per `string_scan_bounded` ACCEL 0/1; 21 per `struct_score` ACCEL 0/1; 14 e 21 per `dfs_rec` ACCEL 0/1. I dati grezzi sono conservati in `phase3_integer_ab_samples.csv` (misure wall e CPU, hash stdout).

| Benchmark | ACCEL | Coppie | Mediana wall 047 → candidato | Delta mediane wall | Mediana delta appaiato wall | Mediana delta appaiato CPU |
|---|---:|---:|---:|---:|---:|---:|
| `string_scan` | 0 | 21 | 0,844124 → 0,737319 s | −12,65% | −6,06% | −6,22% |
| `string_scan` | 1 | 21 | 0,013749 → 0,013379 s | −2,69% | −0,56% | +2,70% |
| `string_scan_bounded` | 0 | 7 | 1,015245 → 1,028714 s | +1,33% | −8,33% | −8,61% |
| `string_scan_bounded` | 1 | 21 | 0,016497 → 0,015685 s | −4,92% | −12,92% | −2,18% |
| `struct_score` | 0 | 21 | 0,169186 → 0,184015 s | +8,76% | +8,06% | +6,88% |
| `struct_score` | 1 | 21 | 0,005388 → 0,005830 s | +8,21% | +8,30% | −0,68% |
| `dfs_rec` | 0 | 14 | 0,129007 → 0,145932 s | +13,12% | −3,72% | −1,75% |
| `dfs_rec` | 1 | 21 | 0,037426 → 0,033409 s | −10,73% | +8,07% | +2,98% |

### Interpretazione del gate prestazionale

Non dichiaro in assoluto “nessuna perdita” sulla base di questi campioni: `struct_score` conserva mediane appaiate wall sfavorevoli, ma con IQR molto ampi che attraversano zero (per ACCEL 0: −38,68%…+22,93%; per ACCEL 1: −7,85%…+17,27%). Per `struct_score` il dump conferma **47 istruzioni identiche** prima/dopo, a parte l'indirizzo funzione. `dfs_rec` ha delta grezzi e appaiati divergenti e outlier marcati. Questo indica un gate prestazionale inconclusivo per i microbenchmark brevi su questo host; non provo una regressione stabile del percorso emesso, ma non maschero i delta sfavorevoli.

## Cosa resta aperto

1. Il modello usa `sizeof` per approssimare la capacità/rappresentabilità e deve diventare un target model esplicito basato sui value bits; il caso MSVC/LLP64 non è stato compilato o testato.
2. `IrExprIntegerBase()` inferisce correttamente i casi rappresentati dai nuovi test, ma alcune forme composite/member/call possono ricadere sui flag IR conservativi anziché su un `ValueType` canonico.
3. Restano da coprire sistematicamente tutte le coppie rank×signedness per operatori binari e compound, promozioni degli enum, plain-char ABI, bitfield layout variabile e conversioni intere attraverso FP/varargs.
4. Il right shift signed negativo dipende dall'implementazione. Gli input undefined non sono candidati a un `.expect` portabile; servono test separati per gli eventuali diagnostici didattici BabyC.

La Fase 3 ha chiuso una parte misurabile del gap 1, ma non prova ancora l'intera semantica integer del C su tutti i target.
