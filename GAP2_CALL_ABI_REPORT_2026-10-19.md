# Gap 2 — valori e ABI di chiamata: stato del turno

Baseline del delta cumulativo: `babyc_baseline_2026-10-18_loop_promotion.zip`.

## Test aggiunti prima della verifica

I test `src/tests/1032`–`1038` hanno `.expect` generati eseguendo ogni sorgente con `gcc -std=c11 -O0` sul target Linux LP64 corrente. Evitano UB; `1032` non assume un ordine di valutazione degli argomenti non specificato dal C.

- `1032_abi_scalar_calls`: round-trip argomenti/ritorni per signed/unsigned char, short, int, long, long long, float, double; puntatori, `NULL`, function pointer, callback e side effects nei call arguments.
- `1033_abi_default_promotions`: char/signed char/short/unsigned short/float passati a funzioni con parametri formali e conversioni osservabili.
- `1034_abi_function_pointer_callback`: chiamate indirette e callback con parametri/ritorni narrow e ricorsione con side effects.
- `1035_abi_variadic_user`: `va_start`, `va_copy`, `va_arg(int/double)`, `va_end`.
- `1036_abi_printf_formats`: `%d %ld %lld %u %lu %x %f %lf %s`.
- `1037_abi_variadic_mixed`: `va_arg` per `long`, `long long`, `double`, puntatore e `char` promosso a `int`.
- `1038_abi_printf_all_formats`: seconda combinazione indipendente dei formati printf richiesti.

## Build

`cd src && make clean && make -j8`: exit 0, zero warning, zero errori. Linux/GCC; MSVC non eseguito in questo turno.

## Matrice completa

Corpus numerato: 1030 test. Tutte le configurazioni hanno `CANONICAL_TYPE_GATE reports=1030/1030 invalid_nodes=0`, nessun timeout e nessuno skip. I soli due fail sono `1035` e `1037`, entrambi dovuti al supporto assente per `va_start`/`va_arg` user-defined; non sono stati mascherati.

| Configurazione | Passati | Falliti | Fallback Tree |
|---|---:|---:|---:|
| Tree ACCEL=0 | 1028/1030 | 2 (`1035`, `1037`) | n/a |
| Tree ACCEL=1 | 1028/1030 | 2 (`1035`, `1037`) | n/a |
| Linear STRICT ACCEL=0 | 1028/1030 | 2 (`1035`, `1037`) | n/a |
| Linear STRICT ACCEL=1 | 1028/1030 | 2 (`1035`, `1037`) | n/a |
| Linear non-STRICT ACCEL=0 + `--require-no-tree-fallback` | 1028/1030 | 2 (`1035`, `1037`) | `tree_body=2`, gate fallito |
| Linear non-STRICT ACCEL=1 + `--require-no-tree-fallback` | 1028/1030 | 2 (`1035`, `1037`) | `tree_body=2`, gate fallito |

Errori esatti Tree/non-STRICT:

- `1035_abi_variadic_user.c:7:12 'va_start' is undefined`
- `1037_abi_variadic_mixed.c:11:12 'va_start' is undefined`

In Linear STRICT l'errore è il preflight: `strict: tree fallback (body)` per il corpo della funzione variadica. In Linear non-STRICT i due corpi sono effettivamente inoltrati a Tree, quindi `tree_body=2`: il requisito di zero fallback NON è soddisfatto.

I test 1032, 1033, 1034, 1036 e 1038 passano in tutte le configurazioni e gli output coincidono con gli `.expect` GCC.

## A/B contro Phase 6

Sette coppie alternate per ciascuno dei quattro benchmark e ACCEL 0/1, Linear STRICT; stdout identico per tutti i processi. Misura: CPU user+system per processo, mediana in millisecondi.

| Benchmark | ACCEL | Phase 6 | Gap 2 | Variazione |
|---|---:|---:|---:|---:|
| `string_scan` | 0 | 463.518 | 488.944 | +5.49% |
| `string_scan_bounded` | 0 | 583.617 | 581.127 | -0.43% |
| `struct_score` | 0 | 98.882 | 101.519 | +2.67% |
| `dfs_rec` | 0 | 38.046 | 36.291 | -4.61% |
| `string_scan` | 1 | 8.630 | 8.885 | +2.95% |
| `string_scan_bounded` | 1 | 9.267 | 10.318 | +11.34% |
| `struct_score` | 1 | 2.685 | 2.514 | -6.37% |
| `dfs_rec` | 1 | 16.641 | 14.963 | -10.08% |

Performance is mixed. The `string_scan_bounded` ACCEL=1 median is +11.34%; this is a signal to investigate, not proof by itself of a regression. The complete seven samples are in `docs/reports/gap2/perf_ab7_phase6_vs_gap2.json`.

## Gap 2 remaining

The fixed-parameter scalar ABI, FP and pointer calls, function pointers/callbacks, recursion test, default promotions, and listed `printf` formats pass the new tests. User-defined variadic functions remain unsupported: `stdarg.h` currently exposes only an opaque `va_list` declaration and no usable `va_start`/`va_arg`/`va_copy` runtime model. The implementation must preserve per-call-site effective argument types and payloads in a shared Tree/Linear ABI frame, then provide a portable `va_list` cursor and promotions on both LP64 and LLP64. It is not correct to mark gap 2 closed until 1035/1037 pass and both Linear no-fallback gates report `tree_body=0`.
