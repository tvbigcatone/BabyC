# P0.24 — H11 + C8 CLOSED

## H11
`IrResolveMemberLayout(ST, Name, Off, Es, Eb, IsPtr)` in `ir_lin_ptr.c`
- `IrLowerMemberOffEs` → thin wrapper
- `IrStructMemberOffset` → thin wrapper (fast paths)

## C8
`IrAllocOwnedBlob(Fr, slot, nbytes)` in `ir_storage.c`
- free previous owned + calloc + LocalVal/LocalOwned
- wired: lin `IR_K_DECL_STRUCT`, lin `DECL_ARRAY` owned

## Residuals
es→TypePointer store maps kept (semantic, not LongLong).

Suite 960/2.
