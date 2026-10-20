# W34 — pointer-alias initialization diagnostics (2026-10-16)

## Root cause

The token/tree evaluator created `p[i]` as an lvalue whose initialization provenance followed the pointer parameter's own `Value`, not the automatic array that owned the pointed-to bytes. In addition, the evaluator's final uninitialized-value check was only run for syntactic arrays (`IndexingArray`), not pointer-index expressions. This made `int get(int *p) { return p[0]; }` silently read a caller's uninitialized `a[0]`, while the lowered VM correctly reported an uninitialized read.

## Fix

- Added `Value.InitAliasRoot`, separate from `LValueFrom`, to carry a stable tracked object root through pointer assignment/copies without conflating pointer storage with pointee storage.
- Propagated the alias root through pointer parameter/local value copies and pointer proxies.
- For pointer-index expressions whose target maps to a tracked automatic object, built the element lvalue against the owning root and checked the byte map even if the legacy escape policy had conservatively set `IsInitialized`.
- Kept the general `ExpressionLeafInit` / `ExpressionInitLeafAddr` escape behavior unchanged. The stricter check is limited to pointer-alias reads, avoiding the broad regressions seen in an initial over-generalized experiment.
- Added regressions for an uninitialized pointer-parameter read (`1038`) and for partial initialization through an alias (`1039`). The existing `1037` regression confirms that writes through a pointer parameter mark the owning array's map.
- Kept an address-based check in the IR tree evaluator for pointer parameters/local pointers as a complementary path.

## Validation

- Clean GCC `-O2` build: passed with no warnings/errors.
- Focused: `1037` writes and then reads initialized element (`5`); `1038` uninitialized read reports `uninitialized variable` in token/tree and both linear VM and strict bytecode VM; `1039` writes `p[0]` but still diagnoses uninitialized `p[1]`; previously passing alias tests `244`, `454`, `545`, `978` remain passing.
- Full expected-output suite, executed in 100-test tranches with a 15-second per-test limit:
  - IR mode: 952/952 pass, 0 timeout.
  - `BABYC_NO_IR=1` token/tree mode: 952/952 pass, 0 timeout.
- The IR-mode suite does not execute the token/tree expression path for this test, so `1038` and `1039` use empty `.expect` files for IR and `.expect.noir` diagnostics for token/tree. Both repros were separately checked in `BABYC_EXEC=vm BABYC_LOWER=1`; `1038` was also checked in `BABYC_EXEC=vm_strict BABYC_BYTECODE_VM=1`.
- MSVC and sanitizers not run. No performance benchmark claim is made; the new metadata field is only read/set at pointer assignment/copy and pointer-index expression formation, not on ordinary scalar reads.

## Status

The reported semantic mismatch is fixed for tracked automatic arrays and struct/union-backed tracked bytes reached through a pointer alias. Unowned/external pointers remain untracked, as before. Pointer arithmetic that loses all provenance, and aliases that escape through opaque native APIs, remain outside this specific fix.
