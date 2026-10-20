# Second evaluator audit — current-source verification

Date: 2026-10-17
Baseline used for performance comparison: `babyc(20261017-074837).zip` rebuilt from source; current tree rebuilt from clean with GCC `-O2`.

## Findings by evaluator claim

| Claim | Current status | Evidence / disposition |
|---|---|---|
| Linear VM is ~30% slower due to `IrStorageDescSeedFrameFromFnSlot`, `FnCodeSnapshot`, `LinTabsRestore` on every recursive call | **Not valid as a description of the current source; small benchmark-specific differences remain** | Current `LinFramePushFor()` skips `FnCodeSnapshot()` and `LinTabsRestore()` for same-function recursion. Same runner, seven runs, current vs rebuilt baseline: `call_loop` VM_no 0.902 → 0.737 s (-18.3%), VM_acc 0.878 → 0.750 s (-14.6%). `micro.isort` VM_no 0.315 → 0.327 s (+3.8%, slower); `classic.isort` VM_no 0.040 → 0.037 s (-7.5%). This does not prove absence of every regression; it does not reproduce the claimed general 30% slowdown. |
| Test 097 reports generic message / wrong column | **Not valid on current tree** | Both `.expect` and `.expect.noir` now specify `can't assign int[] from char[3]`, line 31 column 27. Actual output matched in Tree, Linear strict, token-only, and VM-selected modes. |
| Tree misses uninitialized pointer reads in 1038/1039 | **Fixed in current tree** | Both main expectations are now non-empty and specify the uninitialized diagnostic at columns 36/48. Actual output matched in Tree, Linear strict, token-only, and VM-selected modes. |
| Test 083 shows a `memcpy` overlap defect | **Confirmed and fixed as a didactic runtime diagnostic** | The C standard does not define the result of overlapping `memcpy`, but BabyC's educational contract now detects overlap in both standard-library registration paths and stops before calling host `memcpy`, with `memcpy source and destination regions overlap (undefined behavior in C)`. The same fixture checks that `memmove` still supports overlap and produces `memmove=ababcd`. `bcopy` now delegates to `memmove`, matching its overlap-tolerant contract. |
| Signed host overflow around test 819 remains, including after executor split | **Confirmed, fixed in this turn** | UBSan reproduced overflow in `IrFuseIndexVal`, `IrLinAddI`, token arithmetic and signed `LLONG_MIN / -1`. Arithmetic paths now use unsigned bit-pattern operations for wrapping add/sub/mul, avoid signed overflow in fused/immediate addition, and handle signed min divided/modulo by -1 explicitly. Test 819 now also exercises the min/-1 division/modulo case. ASan/UBSan passed test 819 in Tree, Linear strict, token-only and VM-selected modes with `halt_on_error=1`; compact bytecode test passed with `attempts=1, runs=1`. |
| ZIP still contains obsolete `.o` files | **Not valid for the supplied complete ZIP** | Inspected `BABYC_BASECODE_CURRENT_2026-10-17.zip`: zero `.o`/`.obj` entries. The working tree does contain objects after the clean build, but they are not packaged in that ZIP. |
| `vm`/bytecode gate is absent from CI | **Still valid** | No `.github/workflows`, `.gitlab-ci.yml`, or `Jenkinsfile` exists in the current tree. The bytecode aggregate gate remains a separate open item; the targeted 819 group passed. |
| HANDOFF says “zero cost” for hot paths | **Wording corrected** | `docs/HANDOFF.md` now distinguishes the design goal of avoiding call overhead with inlining from a measured guarantee, and records current suite/gate status without claiming blanket zero cost. |

## Validation run in this turn

- Clean GCC `-O2` build: PASS, no warnings reported.
- Dual suite: A-IR 960/960; B-NO_IR 960/960; C-IR-SHADOW0 960/960.
- FP modes: 32/32.
- Test 819 under ASan/UBSan: Tree, Linear strict, token-only, VM-selected all exit 0 with no sanitizer report.
- Compact bytecode test 819: `attempts=1, runs=1`, output matches.
- Focused output/expectation comparison for 097, 1038, 1039, 083 and 819: expected output matches in the applicable modes; 083 remains semantically unsuitable as a portable `memcpy` overlap test.
- Performance comparison: 7-run medians, same runner, same host, GCC `-O2`, rebuilt baseline and current tree. These are local measurements, not a global performance certification.

## Remaining open items

1. Add a CI workflow that explicitly runs the strict Linear and bytecode gates, with a timeout budget suitable for the complete bytecode corpus.
2. Replace the undefined-behavior `memcpy` overlap fixture with a defined `memmove` test, or retire it from correctness acceptance.
3. Run the full bytecode aggregate gate and complete sanitizer corpus; neither is claimed complete here.
4. Continue watching the small `micro.isort` slowdown with a longer paired sample before attributing it to a source change.


## Follow-up — undefined behavior diagnostic for overlapping `memcpy` (2026-10-17)

- Added canonical diagnostic `DiagnosticMsgMemcpyOverlap` and common `PicocMemoryRangesOverlap()` helper. The helper uses address distance rather than adding byte lengths to addresses, avoiding wraparound in the overlap check; zero-byte copies are not flagged.
- Added the guard to both `LibMemcpy` (`clibrary.c`) and `StringMemcpy` (`cstdlib/string.c`) so behavior does not depend on which standard-library registration is active. The normal successful-copy operation still delegates to host `memcpy`; only the wrapper now performs the constant-time check.
- `src/tests/084_memcpy_overlap.c` is now an intentional runtime-failure test: `memmove` first demonstrates the defined overlap-safe behavior, then overlapping `memcpy` must stop with the dedicated diagnostic.
- `VariousBcopy` now calls `memmove`, because `bcopy` permits overlapping regions.
- Validation: clean GCC `-O2` rebuild; dual suite A-IR/B-NO_IR/C-IR-SHADOW0 each 960/960; test 083 exits non-zero with the expected diagnostic in Tree, Linear strict, token-only, and VM-selected modes. No MSVC/Windows result is claimed.
