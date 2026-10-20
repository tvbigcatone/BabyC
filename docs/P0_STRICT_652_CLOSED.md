# P0.14 — STRICT 652 CLOSED (multi-dim array of pointers)

## Roots
1. **Meta STX `LocalPointeeSize` override** on owned array-of-pointer:
   treated element width as pointee (e.g. 4) → truncated stores. Fixed:
   only scalar pointer cells (`!Owned && ArraySize<=0`) use PointeeSize.
2. **`IrLowerIndexRowIsPointer`**: multi-dim `T *a[N][M]` must not be aop;
   `NDims>=2` or `Array→Array→Ptr` → flatten path.

## Verify
- 652 STRICT → `1 1 1`
- 596 STRICT still `0 ab xxx`
- Suite 960/2
