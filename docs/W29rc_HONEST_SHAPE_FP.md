# Shape FP — policy (corretta in W37)

> **Questo documento sostituisce la versione W29rc**, che dichiarava "default OFF,
> opt-in con `BABYC_SHAPE_FP=1`". Il codice (`IrShapeFpEnabled` in `ir_fast.c`) è
> **default ON**; l'unico interruttore è `BABYC_NO_SHAPE_FP=1`. La documentazione
> contraddiceva il comportamento reale: ora coincidono.

## Policy

| Variabile | Effetto |
|-----------|---------|
| (nessuna) | shape matcher **attivi** (prodotto) |
| `BABYC_NO_SHAPE_FP=1` | shape matcher spenti |
| `BABYC_NO_KERNEL=1` | kernel spenti (`IrKernelFpEnabled`) |

I benchmark pubblicati come "onesti" **devono** usare
`BABYC_NO_KERNEL=1 BABYC_NO_SHAPE_FP=1` (colonne `*_no_accel` di `bench/runners/run_all.py`).
I numeri con acceleratori attivi vanno sempre riportati a parte e dichiarati come tali.

## Matcher interessati

- `IrTryFastInsertShift` (forma insertion-sort)
- `IrTryFastPtrScanPred` (forma scan su stringhe)

Entrambi sono verificati dal test `tools/test_string_scan_fastpath.py`
(diff nativo, contatore di hit, controllo con `NO_SHAPE`).

## Correttezza (W37)

I kernel trattano gli elementi come interi: sono ora ammessi **solo** su statement
che non toccano dati FP (`IrKernelFpEnabledFor(S)` / `IrStmtTouchesFP`). Le funzioni
con ritorno o parametri `double` non usano più mono-rewrite né memo (`IrFuncUsesFP`).
Regressione: `src/tests/950..956_fp_*` + `tools/test_fp_modes.py`.
