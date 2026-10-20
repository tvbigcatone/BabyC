# W32 — audit of direct indexed/member access paths (2026-10-16)

## Scope

Audit the linear IR paths that read or write through a computed address and could bypass the normal initialization-map checks. The fixed reference remains `babyc(20261016-120858).zip`; this report is part of the cumulative delta.

## Findings and changes

- **Nested local array-of-struct member read (`v[i].in.a`) — `IrLinLoadIndex Tok=7`.** The direct lowering retains the original member AST in `IdxTab`; `Step()` uses its source position and checks the effective address against the owning frame's byte map before reading. If metadata capacity is exhausted, lowering stays on the safe path.
- **General direct pointer loads (`Tok=2` and `Tok=7`).** The `Step()` address scan previously handled struct/union byte maps but did not account for frame-owned primitive arrays. It now computes the owned range for those arrays and checks the element-indexed initialization map as well. Struct/union byte-map behavior remains byte-granular.
- **Parameter-index fast path (`LinTryParamIndexedAccess`) — additional gap found.** This path performs direct pointer reads/writes without calling `Step()`, and the fused indexed-copy peephole delegates to the same helper. Added `IrLinCheckOwnedPointerAccess()` so the fast path checks reads against active frame-owned initialization maps and marks the corresponding map after stores. Unowned/external pointers retain the direct path and are not treated as local objects.
- **Struct-member array load (`Tok=6`, `s.arr[i]`).** This already has its own effective-address initialization check against the owning struct byte map; no second check was added there.
- **Remaining boundary.** The tree interpreter and default non-lowered execution do not currently diagnose every pointer-alias read from an uninitialized primitive array. `BABYC_EXEC=vm BABYC_LOWER=1` and strict VM can diagnose the pointer-parameter repro, while the tree path returns its prior value. This pre-existing semantic discrepancy is explicitly not claimed resolved by this direct-path audit.

## Validation

- Clean GCC `-O2` rebuild: PASS, no compiler warnings/errors in the build log.
- Existing suite: 950 tests were executed in 10 batches, each batch in both IR and `BABYC_NO_IR=1` modes; all 1,900 test executions passed. All batches were run against the final source revision, with batches of 100 tests except the final batch of 50.
- New test `951_direct_pointer_param_store_marks_init.c`: prints `5` in Tree, Linear VM, Bytecode-requested VM, and `BABYC_EXEC=vm BABYC_LOWER=1`. It covers the direct parameter-store path marking the caller's local-array initialization map.
- Manual pointer-parameter read repro: `int get(int *p){ return p[0]; } int main(void){ int a[2]; return get(a); }`. With `BABYC_EXEC=vm BABYC_LOWER=1`, the VM emits `uninitialized variable`; the default tree path still returns its prior result, as noted above.
- MSVC and sanitizers: not run in this turn.
- Performance: the micro benchmark set (11 cases, 3 runs) exceeded the 300-second execution limit at case 7/11; no complete benchmark result was produced. No performance claim is made for this additional audit fix.

## Result

The direct-load audit found and patched an additional path that bypassed `Step()`: parameter-indexed fast access (also used by fused indexed-copy). The new regression verifies that writes through the fast path mark the caller's array map. The tree/VM pointer-alias diagnostic discrepancy remains an explicit follow-up rather than being hidden.
