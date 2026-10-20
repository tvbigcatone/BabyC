# BabyC v3 → W37 — report dei problemi rilevati e dei fix

Data: 2026-10-13. Base: zip v3 (`babyc/`). Riferimento di correttezza: gcc -O0 e path a token.

## 1. Sintesi

La valutazione di v3 ha trovato **errori di risultato silenziosi con `double`/`float`** nel
path IR (quello di default), una **regressione di copertura del bytecode compatto**,
test/tool rotti dallo spostamento dei file bench, un **manifest obsoleto** e una
**documentazione in contraddizione col codice**. Tutto corretto e coperto da test.

| Verifica | v3 | W37 |
|---|---|---|
| 27 programmi-sonda FP, output = gcc (default, vm, no-kernel, token) | 1/27 (default) | **27/27** |
| `python3 tools/run_suite.py src` | fallimenti (string-scan, bytecode) | **862 pass, 0 fail, 23 skip** |
| `tools/test_bytecode_vm.py` | si fermava al primo errore | **29/29 gruppi**, exit≠0 se fallisce |
| bytecode compatto su 820/825/898 | preflight a vuoto (runs=0) | runs = 1 / 1 / 12499 |
| `sha256sum -c MANIFEST_SHA256.txt` | 644 FAILED | **0 FAILED** (rigenerabile con `tools/gen_manifest.py`) |

## 2. Problemi e fix

### P1 — Funzione utente che ritorna `double` stampa i bit IEEE come intero
`IrAnnotateExpr` cercava il tipo di ritorno solo via `CachedFunc`, ma le funzioni utente non vi sono legate → call marcata INT.
**Fix** (`ir_compile.c`): fallback `IrLookupFuncReturnType` su `E->Left->Ident` / `E->Ident`.

### P2 — Intrinseci matematici troncati (`sqrt(2.0)` → 0)
Il risultato veniva letto con `IrValueAsInt64`. **Fix**: macro `IR_RET_BITS` (`ir_runtime.h`) usata in `ir_call.c` (3 siti) e `ir_eval.c`.

### P3 — Indicizzazione di `double*`, array locali/globali FP marcata INT
**Fix** (`ir_compile.c`): `elem_fp` derivato dal tipo dichiarato (locale, parametro, globale); l'`Index` è marcato FP/I64. Analogo per `*p` su puntatore a double.

### P4 — Kernel (fill/accum/matmul/…) applicati a dati FP
I kernel trattano gli elementi come interi → risultati errati.
**Fix** (`ir_fast.c`, `ir_lower.c`, `ir.h`): `IrStmtTouchesFP` (cache in `S->FpTouch`) e `IrKernelFpEnabledFor(S)`; tutti i punti di dispatch usano il guard per-statement.

### P5 — Ricorsione su double riscritta da mono/memo in aritmetica intera
`fib/fact/sum` su `double` calcolati in `long long`. **Fix**: `IrFuncUsesFP` (ritorno o parametro FP) disabilita `MonoElig`/`MemoElig` (`ir_call.c`, `ir_lower.c`, `ir_fast.c`).

### P6 — `return n;` (int) in funzione `double` ritorna 0; int→double mancante
Più scorciatoie di ritorno bypassano `IrStReturn`. **Fix**: il return viene avvolto a compile-time in un cast `(double)` (`ir_compile.c`, tipo di ritorno passato da `parse.c` via `IrCompileSetReturnType`); `ir_eval.c` converte int→FP nel cast; il ramo leaf di `ir_call.c` non applica più `IrApplyUnary` ai cast.

### P7 — Store di intero in elemento `double/float` di array
`a[i] = i` scriveva i bit dell'intero. **Fix** (`ir_eval.c`): conversione di assegnamento int↔FP sul ramo Index (e viceversa FP→int).

### P8 — Regressione bytecode compatto (820/825/898)
In v3 le scritture dei tipi di decl nel frame erano protette da `if (!LinCompileActive || CompileFrameSp == 0)`, quindi il preflight di `IrVmExecProd` non vedeva i tipi e ricadeva sempre sul fallback.
**Fix** (`ir_lower.c`): `CompileNoteScalarDecls` / `CompileApplySlotTypes` registrano e applicano i tipi scalari all'ingresso del corpo. Trovato per bisezione dei hunk.

### P9 — Tool/test rotti dallo spostamento dei bench (`bench/cases/...`)
Corretti `test_string_scan_fastpath.py`, `bench_isort_fastpath.py`, `bench_string_scan_fastpath.py`, `src/tools/perf_gate.py`.
`test_bytecode_vm.py` ora esegue un figlio per sorgente, riporta tutti i fallimenti e ritorna exit≠0.

### P10 — Documentazione vs codice (shape FP)
`W29rc_HONEST_SHAPE_FP.md` diceva "default OFF / `BABYC_SHAPE_FP=1`"; il codice è **default ON** con `BABYC_NO_SHAPE_FP=1`. Documento riscritto; resta la regola: i numeri "onesti" usano `BABYC_NO_KERNEL=1 BABYC_NO_SHAPE_FP=1`.

### P11 — Manifest obsoleto
Nuovo `tools/gen_manifest.py`; `MANIFEST_SHA256.txt` rigenerato.

## 3. Nuovi test
- `src/tests/950`–`956_fp_*` (.c/.expect, expect generati da gcc -O0): ritorno double utente, intrinseci, `double*` indicizzato, kernel su array FP, int→double nel return, `*p` double, store int in array FP.
- `tools/test_fp_modes.py`: differenziale in 4 modalità (default, `EXEC=vm`, no-kernel+no-shape, token) → 28 esecuzioni.
- `tools/run_suite.py` ora esegue anche `test_fp_modes.py` e `test_bytecode_vm.py` (che impone `attempts≥1, runs≥1` per i casi bytecode, cioè il "coverage floor").

## 4. Limiti noti (non risolti)
- Il supporto FP nell'IR resta **parziale**: esiste un solo tipo FP (double), la verità sui tipi è decisa da flag strutturali; casi non coperti dalle sonde possono ancora divergere. Il path a token è l'oracolo.
- Il mono-rewrite (fib/fact/sum) dipende dal riconoscimento di forme canoniche: fragile per costruzione (ora escluso per FP).
- Non verificati: build MSVC/Windows; `babyc.exe` non ricompilato (rigenerare dai sorgenti).
- Benchmark non rieseguiti in modo esteso dopo i fix: ricontrollare con `bench/runners/run_all.py` e `src/tools/perf_gate.py` (le colonne "onesto" non dovrebbero cambiare, salvo i casi FP prima errati).

## 5. File modificati
`src/`: `ir.h`, `ir_runtime.h`, `ir_call.c`, `ir_compile.c`, `ir_eval.c`, `ir_exec.c`, `ir_fast.c`, `ir_lower.c`, `ir_stmt.c`, `ir_stubs.c`, `parse.c`, `tools/perf_gate.py`.
`tools/`: `run_suite.py`, `test_bytecode_vm.py`, `test_string_scan_fastpath.py`, `bench_isort_fastpath.py`, `bench_string_scan_fastpath.py`, `test_fp_modes.py` (nuovo), `gen_manifest.py` (nuovo).
`docs/`: `W29rc_HONEST_SHAPE_FP.md`, `REPORT_W37_FIXES.md`. Root: `MANIFEST_SHA256.txt`.
