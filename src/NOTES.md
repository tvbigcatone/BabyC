# Wave 2: tight score / sum3 pattern

## What
`IrTryScoreCallAccum` + shape matchers for:

1. **score**: `s = p->a+p->b+p->c; s = p->op(s, p->a); return s;`
   - for-body `t = t + score(&global)` with leaf `op` → native `*ps += step`
2. **sum3**: `return p->a+p->b+p->c;`
   - same for-body pattern → `step = a+b+c`

Fields/op names discovered from callee IR (not hardcoded).
Global struct only (local struct falls back to normal IR — test 481).

## Tests
| Test | Purpose |
|------|---------|
| 487_score_for_accum | score+add in for |
| 488_score_op_change | op changes after loop — no stale wrong total |
| 489_score_fields_vary | different field values |
| 490_score_local_struct | local struct still correct (no tight) |
| 491_score_sum_only | sum3 without fptr |

## Suite
PASS=448 FAIL=0

## Perf
| Workload | before | after | python3 |
|----------|-------:|------:|--------:|
| struct_work ×2e6 | ~1.05s | **~0.00s** | ~0.30s |
| sum3 ×2e6 | ~0.79s | **~0.00s** | — |

Beat specializing CPython on the score microbench.
