# P1 — STRICT residual inventory (2026-10-17 kickoff)

Scan: `BABYC_PATH=linear BABYC_STRICT=1` vs `.expect` on 944 tests.

| Result | Count |
|--------|------:|
| ok | 884 |
| tree fallback | 47 |
| mismatch / other | 13 |

## Fallback clusters (priority)
1. **Heap / large auto** (280–284, 369, 942)
2. **stdio / scanf / printf varargs** (654–656, 690, 697, 644)
3. **qsort / real algorithms** (1033–1034, 897–898)
4. **bitfield nested / brace nonconst** (519, 568)
5. **goto / typedef / sibling** (593, 1031)
6. **Known soft residuals** (785 pointer fallback, 792 unsigned deref)

## Mismatch notes
Many are diagnostic/args/include tests (018, 031, 097-class) — not VM gaps.
Focus P1 on **fallback** list: pure linear coverage expansion.

## Next actions
1. Pick one cluster (stdio host-call or heap Decl) and close with regression tests
2. Re-scan STRICT cover % after each cluster
3. Keep ACCEL=0 engine benches separate from coverage work
