# BabyC POSIX/MSVC/guest-exit follow-up — 2026-10-20

## Applied
- Structural C4101 declaration scoping in five POSIX implementation files.
- Corrected native process-exit fallback in both Unix and Windows platform layers: `exit(0)` remains a successful zero exit status rather than being rewritten to status 1.
- Hardened the interpreted guest-thread trampoline's exit-point path: when guest `exit()` longjmps over ordinary return cleanup, unwind IR call frames created by the callback, remove stack-owned value proxies, and restore heap/stack-frame pointers captured before guest execution.
- Added `src/tests/posix/guest_exit_zero.c` as a focused regression case for the guest worker exit path.

## Validation
- Source-edit assertions: PASS; each targeted source edit matched exactly once.
- Archive contents and Git tree: verified after publication.
- Native compilation, Linear STRICT execution, POSIX suite, MSVC x86/x64: NOT RUN in this environment. No build/test pass is claimed.
- The Linear STRICT admission helpers `FnCodeVoidParamTypedAccessSafe` and `FnCodeIndexedPointerParamsSafe` already exist in the checked branch. Their runtime behavior cannot be certified without executing the gate; this follow-up does not broaden their admission rules without a failing trace.
