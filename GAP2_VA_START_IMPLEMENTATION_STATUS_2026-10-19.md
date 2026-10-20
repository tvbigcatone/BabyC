# Gap 2 — variadic cursor implementation status (2026-10-19)

This status supersedes the earlier failed `va_start` attempt notes: the macro/evaluation issue and the missing variadic ABI payloads have now been fixed. See `GAP2_VARIADIC_IMPLEMENTATION_STATUS_2026-10-19.md` for the final architecture and validation matrix.

Verified outcomes:
- 1035: `24 3.8`, expected output, Token/Tree/Linear STRICT pass.
- 1037: `20000100032`, expected output, Token/Tree/Linear STRICT pass.
- 1042–1044: expected outputs pass, including promotions, repeated `va_copy`, and forwarding.
- Linear STRICT 1035/1037/1042–1044: `tree_body=0`; the emitted Linear body owns execution.

The cursor refers to either the shared typed `CallAbiFrame` or the active raw-payload `IrCallFrame`. A dedicated `IrLinVaArg` instruction consumes the cursor in Linear; no benchmark-specific shape recognition or Tree fallback is used.
