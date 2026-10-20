# Audit finale — assi canonici, accelerazioni e benchmark unificato (2026-10-17)

## Contratto verificato

BabyC espone esattamente tre assi ortogonali per i test prestazionali:

- `BABYC_EXEC_MODE=tree|linear|bytecode`
- `BABYC_ACCEL=0|1`
- `BABYC_FALLBACK=0|1`

`BABYC_STRICT`, `BABYC_VM_STRICT`, `BABYC_EXEC=vm_strict`, `BABYC_PATH`, `BABYC_EXEC`, `BABYC_LOWER`, `BABYC_LOWER_CALL` e gli override per singola accelerazione non sono più interfacce runtime per selezionare l'esecuzione. `BABYC_NO_IR=1` resta solo per il test differenziale token-vs-IR e viene rimosso dall'ambiente del benchmark.

## Bonifica completata

1. Runtime: `ir_lower.c` legge esclusivamente `BABYC_EXEC_MODE`, `BABYC_ACCEL`, `BABYC_FALLBACK` per i tre assi. Valori sconosciuti di `EXEC_MODE` non attivano modalità legacy: si ricade su Tree.
2. Accelerazioni: gli acceleratori opzionali, compresi i matcher AST/kernel, mono-call, memoizzazione, fused expression, leaf-call/thin-kernel e fast host printf, sono subordinati a `BABYC_ACCEL`. Non ci sono più interruttori `BABYC_NO_MEMO`, `BABYC_NO_KERNEL` o `BABYC_NO_SHAPE_FP` nel runtime.
3. Tooling: il runner unificato produce le sei colonne complete `TREE`, `TREE_ACCEL`, `LINEAR`, `LINEAR_ACCEL`, `BYTECODE`, `BYTECODE_ACCEL`; imposta tutti e tre gli assi e rimuove gli assi ereditati e l'overlay no-IR.
4. Alias: rimosse le condizioni e i wrapper che duplicavano la semantica di fallback/strict; i tool usano `FALLBACK=0` per misurare la copertura senza fallback. I benchmark legacy `tools/bench/ab.sh` e `src/tools/fase4_measure.sh` delegano ora al framework unificato.
5. Test runner a tranches: `src/tests/run_suite_tranches.py` supporta `--start/--count`, mantiene isolati i test e non conta timeout o crash da segnale come PASS.

## Validazione ripetibile

- Build pulita GCC (`make clean && make -j2` in `src/`): completata, zero warning/errori nel log.
- Suite Tree predefinita: **973/973 PASS**, eseguita in 10 tranches (nove da 100 e una da 73), zero fail e zero timeout.
- Gate Bytecode VM: **53/53 gruppi PASS**, completati in tranches più piccole per rispettare i limiti di esecuzione; inclusi i controlli `FALLBACK=0`, diagnostica bounds/division-by-zero e casi con esecuzione compatta verificata.
- FP mode differential: **32/32 PASS**.
- `test_string_scan_fastpath.py`: PASS su entrambi i casi; output identico a GCC, acceleratore hit con `ACCEL=1`, nessun hit con `ACCEL=0`.
- `test_sparse_local_snapshot.py`: PASS in Linear/Bytecode, con esecuzione compatta confermata e prova no-fallback.
- Debug smoke con configurazione Tree canonica: **2/2 PASS**. Le vecchie asserzioni di step/breakpoint dipendevano dal flag di shadow-lowering `BABYC_LOWER`, ora rimosso; non vengono usate per selezionare un motore.
- Il target `Makefile.minimal` è stato provato e fallisce per simboli già mancanti nel punto zero (catalogo `DiagnosticMsg*`, helper IR e guest qsort/bsearch); la stessa failure è stata riprodotta anche nel tree baseline. Non è quindi dichiarato verde né attribuito a questo delta.

## Benchmark A/B e copertura

Tutti i tempi seguenti sono secondi end-to-end, localmente, con fallback disabilitato. Non sono una dichiarazione di prestazioni globali: i casi e il numero di ripetizioni sono riportati esplicitamente.

| Caso | TREE | TREE_ACCEL | LINEAR | LINEAR_ACCEL | BYTECODE | BYTECODE_ACCEL | Esito |
|---|---:|---:|---:|---:|---:|---:|---|
| `micro.fib_iter` (3 runs, output tra i sei motori) | 0.032 | 0.001 | 0.031 | 0.012 | 0.030 | 0.013 | OK |
| `micro.isort` (1 run, con GCC) | 0.845 | 0.006 | 0.295 | 0.007 | 0.299 | 0.007 | OK |
| `micro.struct_score` (1 run, con riferimenti) | 0.148 | 0.001 | 0.233 | 0.234 | 0.229 | 0.230 | OK |
| `micro.string_scan` (3 runs, test dedicato GCC) | 0.386 | 0.017 | — | — | — | — | COVERAGE su Linear/Bytecode |
| `real.i06_tak` (1 run, con riferimenti) | 1.155 | 1.135 | 1.284 | 1.299 | 1.316 | 1.276 | OK |
| `real.a02_qsort` (1 run, con riferimenti) | 0.863 | 0.787 | — | — | — | — | COVERAGE su Linear/Bytecode |
| `real.a03_heapsort` (1 run, con riferimenti) | 1.090 | 1.098 | — | — | — | — | COVERAGE su Linear/Bytecode |

`micro.fib_iter`, `micro.isort` e `micro.struct_score` mostrano che le accelerazioni sono riattivate sotto `BABYC_ACCEL=1`; `isort` accelera anche nel percorso Linear/Bytecode, mentre il matcher `struct_score` è applicabile al percorso Tree ma non dà un beneficio misurabile negli altri due motori. `string_scan` conferma un miglioramento significativo nel Tree; Linear/Bytecode con `FALLBACK=0` restano correttamente visibili come gap di copertura. Qsort/heapsort non sono dichiarati coperti in Linear/Bytecode sulla base di esecuzioni con fallback.

Un invocazione aggregata troppo ampia del benchmark ha superato il timeout del tool; le misure sono state quindi separate per tranche/caso e le tabelle sopra riportano soltanto esecuzioni completate. Il test compatto dedicato (`849_vm_compact_loop_bench.c`, 3 round dopo warm-up) ha dato mediane end-to-end: Tree 19.630 ms, Linear 12.396 ms, Bytecode 13.824 ms; esecuzione compatta confermata.

## Comando canonico

```bash
python3 bench/runners/run_bench.py --set all --runs 3
```

Il runner scrive `FALLBACK=0` per tutte le colonne. `COVERAGE` indica un costrutto ancora non coperto dal motore scelto; non si deve mascherarlo con fallback né eliminare il caso dal benchmark.
