# P0 — Helper inventory (updated 2026-10-17 P0.24)

## Filosofia
Un significato runtime → un helper. Hot = `static inline` (costo zero);
cold Decl/Call = funzione condivisa + `GUARD`. Niente duplicazione tree↔linear.

## A. Zero-cost / static-inline

| ID | Helper | Status |
|----|--------|--------|
| H1 | `IrWriteByElemSize` | **DONE** |
| H2 | `IrWriteScalarBase` / `IrWriteMemberScalar` | **DONE** |
| H3 | `IrTypePeelArray` | **DONE** |
| H4 | `IrResolveDeclElemSize` | **DONE** |
| H5 | `IrMarkInitMapRange` | **DONE** |
| H6 | `IrLoadByElemSize` | **DONE** |
| H7 | `IrBaseFromElemSize` | **DONE** (es→TypePointer store left intentional) |
| H8 | `IrArrayIndexInBounds` | **DONE** |
| H9 | `IrInitMapBytesReady` | **DONE** |
| H10 | multidim Index flatten | **DONE** |
| H11 | `IrResolveMemberLayout` | **DONE** — core; `IrLowerMemberOffEs` + `IrStructMemberOffset` wrappers |

## B. Cold shared

| ID | Helper | Status |
|----|--------|--------|
| C1–C2 | `IrPackStructBraceInit` | **DONE** |
| C3 | `IrCopyArrayIdentInit` | **DONE** |
| C4 | `SeedFrameFromFnSlot` | **DONE** |
| C5 | `IrStorageDesc*` | **DONE** |
| C6 | `IrCollectSortedMembers` | **DONE** |
| C7 | `IrCopyStructBlob` | **DONE** |
| C8 | `IrAllocOwnedBlob` | **DONE** — DECL_STRUCT + DECL_ARRAY lin |
| C9 | `BindParamArg` | **DONE** |

## Residuals intentional
- `es==8 → TypePointer` on `*p=v` store paths (pointer-width store, not LongLong)
- host/printf pack, WideImm, Light window (deferred by policy)

## Score
**Helper P0 CLOSED** for listed H1–H11 / C1–C9.
P0.25 dual-read accessors DONE. Next: more emit sites + optional Frame-first runtime-only helpers.
