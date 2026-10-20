# P0 — STRICT inventory (honest residual vs silent)

**Source:** W30 gap inventory, STRICT fruits 2026-10-15, FP return gap 954,
suite status post-P0.3 (soft path).

## Soft EXEC=vm (product path)

| Metric | Value |
|--------|------:|
| Suite pass | 959 |
| Pre-existing fails | 3 (097 caret, 699 designated union, bytecode 820/825) |
| Soft residual on core corpus | ~0 for pointer scale / uninit clusters closed in W30 |

## STRICT coverage (historical snapshot ~2026-10-15)

| Metric | Value |
|--------|------:|
| ok | ~832–844 / ~930 (~89–90%) |
| honest fallback | ~68 |
| silent wrong | ~25–30 |

## Known silent / priority residual (do not expand without root cause)

| ID / cluster | Class | Notes |
|--------------|-------|-------|
| 954 FP return int-as-double | **CLOSED** | P0.5 Cast Imm=2 + annotate |
| nested member assign | residual/silent | InitMap root walk edge cases |
| bitfield Decl from Call | residual | 514/647 style |
| bytecode 820/825 | residual | compact path, not soft |
| 097 caret column | diag | pre-existing expect drift |
| 699 designated union | semantic | pre-existing |

## Closed clusters (do not re-open without regression)

- Ptr relational / scale (178, 625, 751, …)
- FP promote / Cast Imm=2 / BinOp RHS
- Unsigned deref shift 792
- Multi-level pointer root (431)
- Uninit member-array trap alignment (soft)

## Policy for P0 continuation

1. Work only on **silent wrong** under STRICT; leave honest fallback as-is.
2. Prefer storage-descriptor fixes when root cause is mis-classification.
3. Every STRICT fix needs antireg test + dual tree/soft/strict gate.
4. Do not count `runs=0` fallback as coverage.

## Next measurement

Re-run STRICT cover script when available (`tools` inventory or
`BABYC_VM_STRICT=1` corpus scan) and refresh numbers in this file.
