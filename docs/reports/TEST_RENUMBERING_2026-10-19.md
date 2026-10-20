# Rinumerazione del corpus test BabyC — 2026-10-19

## Ambito e algoritmo

Baseline esclusiva: `babyc_baseline_2026-10-18_loop_promotion.zip` (SHA-256 `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`) più il delta cumulativo di sessione del 18 ottobre. Nessun codice di motore è stato modificato per questo compito; il delta precedente è mantenuto integralmente.

Inventario iniziale di `src/tests`: 1.009 sorgenti numerate `.c`, 1.001 coppie numerate `.c/.expect`, 8 sorgenti numerate senza `.expect` usate come fixture di test specializzati. Le 1.009 sorgenti vengono ordinate per numero precedente, poi per suffisso descrittivo alfabetico (case-insensitive, con il nome originale come spareggio); a ogni caso viene assegnato un ID da 001 a 1009, con tre cifre fino a 999 e quattro da 1000. I file associati allo stesso stem (`.expect`, `.expect.noir`, `.h`, `.expect.win32` e ogni altro asset trovato) si spostano insieme. Non sono presenti asset numerati `.args` o `.stdin` nel corpus inventariato. I fixture di scratch senza prefisso numerico restano fuori dal corpus numerato.

`TEST_RENUMBER_MAP.csv` è la mappa completa `old_name` → `new_name` per tutti i file legati ai 1.009 sorgenti numerati, incluse le 10 voci a percorso invariato (cinque `.c` e cinque `.expect` già sul numero assegnato): 2.035 record totali, di cui 2.025 con percorso modificato. `TEST_RENUMBER_RESIDUALS.csv` conserva il vecchio e il nuovo numero dei 13 residui. I riferimenti a stem dei test sono aggiornati in `tools/`, `validation/`, include dei test e documenti/report. Nessuna lista skip/expected-fail basata su un file di test è stata trovata in `tools/` o `validation/`; i riferimenti letterali ai file presenti negli script sono stati riscritti e verificati.

## Comandi eseguiti e output

Build pulita (directory corretta `src/`):

```sh
make -C src clean
make -C src -j2
```

Output di verifica effettivo:

```text
BUILD_RC=0
warning/error count: 0
make: Leaving directory '.../src'
```

La matrice riproducibile è stata aggiunta come `tools/run_numbered_matrix.py`; accetta `--path tree|linear`, `--accel 0|1` e `--strict` per Linear. I comandi per le quattro configurazioni sono:

```sh
env -u BABYC_NO_IR -u BABYC_EXEC -u BABYC_VM_STRICT -u BABYC_NO_SHAPE_FP -u BABYC_NO_KERNEL -u BABYC_BYTECODE_VM python3 tools/run_numbered_matrix.py --path tree --accel 0 --log docs/reports/matrix_renumbered_tree_accel0_2026-10-19.log
env -u BABYC_NO_IR -u BABYC_EXEC -u BABYC_VM_STRICT -u BABYC_NO_SHAPE_FP -u BABYC_NO_KERNEL -u BABYC_BYTECODE_VM python3 tools/run_numbered_matrix.py --path tree --accel 1 --log docs/reports/matrix_renumbered_tree_accel1_2026-10-19.log
env -u BABYC_NO_IR -u BABYC_EXEC -u BABYC_VM_STRICT -u BABYC_NO_SHAPE_FP -u BABYC_NO_KERNEL -u BABYC_BYTECODE_VM python3 tools/run_numbered_matrix.py --path linear --strict --accel 0 --log docs/reports/matrix_renumbered_linear_accel0_STRICT_2026-10-19.log
env -u BABYC_NO_IR -u BABYC_EXEC -u BABYC_VM_STRICT -u BABYC_NO_SHAPE_FP -u BABYC_NO_KERNEL -u BABYC_BYTECODE_VM python3 tools/run_numbered_matrix.py --path linear --strict --accel 1 --log docs/reports/matrix_renumbered_linear_accel1_STRICT_2026-10-19.log
```

Output terminale reale delle quattro esecuzioni:

```text
SUMMARY tree_accel0: PASSED=1001 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1001 SECONDS=4.89
SUMMARY tree_accel1: PASSED=1001 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1001 SECONDS=4.56
SUMMARY linear_accel0_STRICT: PASSED=988 FAILED=13 TIMEOUT=0 SKIPPED=0 TOTAL=1001 SECONDS=4.24
FAILURES: 047_grep.c 058_macro_bug.c 062_initializers.c 126_csmith_modulo_zero.c 454_complex_dispatch.c 606_storm_inc_dec_member_call.c 609_storm_enum_union_fptr.c 618_inc_member_call_arg.c 651_sqlite_exec_null_varargs.c 873_alignment_extended.c 893_vm_struct_return_effect_fallback.c 947_qsort_bsearch.c 948_qsort_stable.c
SUMMARY linear_accel1_STRICT: PASSED=988 FAILED=13 TIMEOUT=0 SKIPPED=0 TOTAL=1001 SECONDS=3.89
FAILURES: 047_grep.c 058_macro_bug.c 062_initializers.c 126_csmith_modulo_zero.c 454_complex_dispatch.c 606_storm_inc_dec_member_call.c 609_storm_enum_union_fptr.c 618_inc_member_call_arg.c 651_sqlite_exec_null_varargs.c 873_alignment_extended.c 893_vm_struct_return_effect_fallback.c 947_qsort_bsearch.c 948_qsort_stable.c
```

I log completi includono tutti i checkpoint `CUMULATIVE` a passi di 100, e per i fail `EXPECT`, `ACTUAL`, return code e tempo osservato.

## Vecchi e nuovi numeri dei 13 residui

| Vecchio ID | Nuovo ID | Sorgente canonicale |
|---:|---:|---|
| 046 | 047 | `047_grep.c` |
| 057 | 058 | `058_macro_bug.c` |
| 061 | 062 | `062_initializers.c` |
| 121 | 126 | `126_csmith_modulo_zero.c` |
| 441 | 454 | `454_complex_dispatch.c` |
| 610 | 606 | `606_storm_inc_dec_member_call.c` |
| 613 | 609 | `609_storm_enum_union_fptr.c` |
| 622 | 618 | `618_inc_member_call_arg.c` |
| 661 | 651 | `651_sqlite_exec_null_varargs.c` |
| 942 | 873 | `873_alignment_extended.c` |
| 959 | 893 | `893_vm_struct_return_effect_fallback.c` |
| 1033 | 947 | `947_qsort_bsearch.c` |
| 1034 | 948 | `948_qsort_stable.c` |

Attenzione alla collisione storica `661`: il vecchio `661_fp_tight_sum_i` è un test diverso ed è stato rinominato `650_fp_tight_sum_i`; il residuo SQLite è il caso che era `661_sqlite_exec_null_varargs` e ora è `651_sqlite_exec_null_varargs`.

## Controlli di integrità

- Sorgenti numerate `.c`: 1.009; ID min/max: 001/1009; nessun ID duplicato e nessun buco.
- Casi numerati con `.expect`: 1.001; gli otto fixture source-only sono rinominati e i riferimenti negli strumenti dedicati sono aggiornati. Tutte le 2.035 righe della mappa puntano a target esistenti e univoci.
- Audit contenuti: tutte le 2.035 coppie file pre/post coincidono dopo la sola sostituzione dei riferimenti agli stem; zero mismatch. Nei quattro file engine `src/*.c` con diff rispetto allo snapshot pre-rinumerazione, le sole righe cambiate sono commenti che citano test rinominati; nessuna modifica al codice eseguibile.
- Audit riferimenti `tools/` e `validation/`: zero riferimenti non risolti a sorgenti numerate; gli include locali dei test si risolvono, salvo `no_such_header_xyzzy.h` (fixture intenzionale che testa un include mancante) e `sqlite3.h` (header esterno). Le fixture dedicate in `tools/fixtures/` sono indipendenti dal corpus `src/tests` e i loro percorsi continuano a esistere.
- Tree: 1001/1001 con ACCEL 0 e 1. Linear STRICT: 988/1001 con ACCEL 0 e 1. I 13 residui sono esattamente gli stessi per descrizione; zero timeout e zero regressioni nuove.
- Non è stato eseguito MSVC/Windows (verifica locale) e non sono stati eseguiti gate bytecode, fuori ambito.

La matrice di rinumerazione convalida soltanto la neutralità della rinomina e dei riferimenti: non cambia il precedente stato del gate prestazionale Linear, che resta non certificato.


## Delta cumulativo e prova di applicazione

Il pacchetto `/mnt/data/babyc_cumulative_delta_2026-10-19.zip` è costruito come differenza file-per-file rispetto allo ZIP originale allegato, non rispetto allo snapshot già rinumerato. Contiene 2.170 file nuovi/modificati e l'elenco `DELETE_FILES.txt` con 2.023 percorsi precedenti che vanno rimossi prima dell'overlay. Gli artefatti compilati (`src/babyc`, `.o`, `.obj`, `.a`, `.exe`, `.so`, `.pyc`) sono esclusi.

Comandi di verifica usati:

```sh
unzip -t /mnt/data/babyc_cumulative_delta_2026-10-19.zip
# Copia fresca del baseline; eliminazione dei 2.023 percorsi in DELETE_FILES.txt; quindi:
unzip -oq /mnt/data/babyc_cumulative_delta_2026-10-19.zip -d /mnt/data/delta_apply_verify
```

Output effettivo:

```text
No errors detected in compressed data of /mnt/data/babyc_cumulative_delta_2026-10-19.zip.
REMOVED_OLD_PATHS 2023 LISTED 2023
APPLY_VERIFY_CURRENT_FILES 3081
APPLY_VERIFY_APPLIED_FILES 3081
MISSING_AFTER_APPLY 0 []
EXTRA_AFTER_APPLY 0 []
CONTENT_DIFFS_AFTER_APPLY 0 []
PASS: cumulative delta applied on the original baseline reproduces current distributable tree byte-for-byte (excluding build artifacts)
```
