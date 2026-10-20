# P0.23 — C7 IrCopyStructBlob + H3/H7/H9 wire

## C7
`void IrCopyStructBlob(dst, src, sz, imap, map_len)` in `ir_mem.c`
- memcpy blob
- optional full InitMap mark
- call sites: tree struct Decl (`ir_stmt`), lin Tok=3 (`IrCopyStructBlob(..., NULL, 0)` + InitMap custom below)

## H3
Pure `while (T->Base==TypeArray) T=T->FromType` → `IrTypePeelArray` in lin_exec + ir_mem.
Dim-collect loops kept local (need ArraySize side effects).

## H7
Signed/unsigned es→BaseType cascades → `IrBaseFromElemSize`.
Left intentional: `es==8 → TypePointer` stores (not LongLong).

## H9
`IrInitMapBytesReady(imap, off, es, map_len)` — byte-map uninit probe in load path.

## Verify
Suite 960/2 · STRICT 964/981/982/1042/699/596 PASS.
