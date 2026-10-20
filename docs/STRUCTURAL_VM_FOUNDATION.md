# Foundation strutturale EXEC=vm (2026-10-05)

Interventi **solo generali/strutturali** — nessun tuning di bench.

## Cosa

1. **`IrLinCodeHasMemOps`** — helper unico (expr + stmt) per la policy integer-safe.
2. **`IrVmProdNotice`** — messaggio one-shot se `BABYC_EXEC=vm` (path sperimentale documentato).
3. **Soft-match** — sotto `EXEC=vm` in fallback tree non si confronta lin vs tree (niente rumore diagnostico).
4. **`IrFnBodyIntegerEligible` / `IrStmtIntegerOk`** — walker strutturale del body IR:
   - rifiuta index/member/ptr/array/struct/union/fptr/printf-family/malloc/free;
   - **non collegato al path default** (fondazione per futura fetta per-funzione).

## Gate

```
A-IR / B-NO_IR: 756/0  (suite base; test 770+ non in questo tree)
EXEC=vm array_sum/isort/fib: rc=0
default: invariato
```

## Non fatto
- FnCode / bytecode per-funzione
- Maxing fib o altri microbench
- Merge F6/F7 nel prodotto
