# P0.26 — Unified execution config

## Tre assi ortogonali

| Asse | Env | Valori | Default |
|------|-----|--------|---------|
| **PATH** | `BABYC_PATH` | `token` \| `tree` \| `linear` \| `bytecode` | `tree` |
| **ACCEL** | `BABYC_ACCEL` | `0` \| `1` | `1` |
| **STRICT** | `BABYC_STRICT` | `0` \| `1` | `0` |

- **PATH**: quale motore esegue il body  
- **ACCEL**: master switch shape FP + kernel (`0` spegne tutto; con `1` restano validi `BABYC_NO_SHAPE_FP` / `BABYC_NO_KERNEL` per feature singola)  
- **STRICT**: niente residual tree silenzioso su linear/bytecode  

## Alias legacy (ancora onorati)

| Legacy | Mapping |
|--------|---------|
| `BABYC_NO_IR=1` | PATH=token |
| `BABYC_EXEC=vm` | PATH=linear |
| `BABYC_EXEC=vm_strict` | PATH=linear + STRICT=1 |
| `BABYC_EXEC=tree` | PATH=tree |
| `BABYC_VM_STRICT=1` | STRICT=1 |
| `BABYC_BYTECODE_VM=1` | PATH=bytecode (con linear base) |
| `BABYC_NO_SHAPE_FP` / `NO_KERNEL` | per-feature quando ACCEL=1 |

## API C
`BabycPathGet()` · `BabycAccelOn()` · `BabycStrictOn()`  
Wrapper: `IrExecIsVmProd()` · `IrVmStrictOn()`

## Tools
`run_suite.py` docs aggiornati; `perf_gate.py` imposta PATH/ACCEL oltre agli alias.


## Tools migration (full)
- `tools/babyc_env.py` — `make_env` / `linear` / `bytecode` / `token` / `honest_*`
- All suite/bench/fuzz harnesses set **PATH/ACCEL/STRICT**; legacy keys scrubbed from parent env where modes switch.
- Differential shadow still may use legacy `BABYC_EXEC=both` (not a pure path).
