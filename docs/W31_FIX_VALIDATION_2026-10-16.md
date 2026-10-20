# W31 — light-call frame isolation (2026-10-16)

## Changes retained

1. `LightRunBody()` updates only its own `LLoc` and `LightInitMask`; it does not write `LocalVal` or `LocalInit` through `IrCallStack`, whose top frame can belong to the caller.
2. Added `src/tests/949_light_call_does_not_mask_uninit.c` and its expected diagnostic to guard against a light callee masking a caller's uninitialized scalar.

W31 temporarily disabled the direct member-chain load while its initialization behavior was investigated. W32 supersedes that temporary restriction: see `W32_INIT_AWARE_DIRECT_MEMBER_LOAD_2026-10-16.md` for the restored initialization-aware direct path and current validation status.
