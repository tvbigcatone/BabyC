# BabyC — consolidamento e verifica integrata

Data: 2026-10-17

## Punto di partenza e perimetro

La verifica parte dalla baseline `babyc(20261017-074837).zip` e applica il delta cumulativo `BABYC_DELTA_ROOT_FIX_LIGHT_CALL_AND_FIB_PROFILE_2026-10-17.zip`, che include gli interventi precedenti su helper di metadati indicizzati, struct-array/diagnostica bytecode e il root fix delle chiamate leggere Linear.

Questa è una verifica di consolidamento; non introduce nuove modifiche al codice runtime.

## Build riproducibile

- `make clean && make -j2` in `src/`: PASS.
- GCC `-O2`; nessun warning nel log di build.
- La build è stata eseguita da zero per evitare oggetti stale dopo le modifiche agli header `FnCodeSlot`.

## Verifiche funzionali

- `tools/test_fp_modes.py`: **32/32 PASS**.
- `tools/test_sparse_local_snapshot.py src/babyc`: PASS; output Linear/compact/strict corretto e conferma di esecuzione compact.
- `tools/test_string_scan_fastpath.py --babyc src/babyc`: PASS.
- `tools/test_empty_expect_exit_status.py --babyc src/babyc`: PASS.
- Corpus mirato di 29 casi (ricorsione/chiamate, struct-array multidimensionali, bitfield, alias e diagnostica): **29/29 PASS** in ciascuna delle modalità Tree/default, token-only (`BABYC_NO_IR=1`) e Linear VM strict (`BABYC_EXEC=vm`, `BABYC_VM_STRICT=1`).
- Gate benchmark `bench/runners/run_bench.py --set stress --only fib_global,fib_multiarg,fib_ptr,call_loop --runs 3 --skip-python --babyc src/babyc --tag consolidation_20261017`: **4/4 OK**, nessuna differenza d'output nei modi verificati.

I casi OOB compact `968` e `969` sono gestiti dal gate bytecode dedicato, non dal corpus ordinario usato per il conteggio 29/29. Il precedente gate dedicato aveva confermato le diagnostiche attese e `runs=0` prima dell'errore, come previsto.

## A/B prestazionale aggiuntivo

È stata ricostruita e compilata da zero una versione pre-root-fix applicando la delta cumulativa `BABYC_DELTA_BYTECODE_STRUCT_ARRAY_DIAGNOSTICS_FIX_2026-10-17.zip` alla stessa baseline. Il binario finale è quello prodotto dal tree con il root fix. Entrambi sono GCC `-O2`; stessi file input e variabili d'ambiente:

`BABYC_EXEC=vm BABYC_NO_KERNEL=1 BABYC_NO_SHAPE_FP=1 BABYC_NO_MEMO=1`

Le esecuzioni sono state intercalate, con output/exit status controllati.

| Caso | Pre-root-fix, mediana (4 misure) | Finale, mediana (4 misure) | Rapporto finale/pre | Variazione |
|---|---:|---:|---:|---:|
| `fib_ptr` | 1.1409 s | 0.3753 s | 0.329 | **−67.1%** |
| `call_loop` | 1.1881 s | 1.0011 s | 0.843 | **−15.7%** |

Output di `fib_ptr` invariato: `832040 2692537`. I tempi assoluti sono più alti rispetto alla precedente sessione di misura, ma l'A/B intercalato nella stessa sessione conferma il guadagno relativo. Non si confrontano direttamente questi tempi con quelli del benchmark runner, che usa una configurazione di esecuzione diversa.

## Suite completa e limiti dichiarati

- `tools/run_suite.py src` non è terminato entro il limite operativo di 300 secondi. Il runner stampa il riepilogo soltanto alla fine; quindi questa esecuzione **non produce un conteggio valido** di pass/fail e non viene dichiarata PASS.
- Il gate completo `tools/test_bytecode_vm.py` aveva già superato decine di gruppi, ma è scaduto per timeout prima del riepilogo. Si mantiene esplicitamente non verificato il passaggio completo; i gate mirati elencati nei report precedenti restano la prova disponibile.
- MSVC/Windows e la build a 32 bit non sono stati verificati in questa sessione.

## Conclusione

Il consolidamento conferma la build pulita, la parità del corpus mirato nei tre percorsi e un miglioramento A/B riproducibile sui due stress test. Non si dichiara una suite completa verde: il limite di tempo del runner generale e del gate bytecode resta aperto. Non sono state introdotte scorciatoie legate alla forma dei benchmark né fallback aggiuntivi.
