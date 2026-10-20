# W30 nested/aos brace pack (2026-10-15)

## Hidden fruit in the small bush
543: `struct S1 s = {{1U,2U,3},4}` inside while → STRICT 6 not 20

## Cause
DECL_STRUCT brace packed as flat `int[]` into the blob:
- sizeof(S1)=8 → only 2 ints written → fields `1 0 0 2`
- Nested S0 (uchar,uchar,short) never expanded
- Fallback overwrote correct field packing when `!used_bf`

Array-of-bitfield-struct (557) same class via DECL_ARRAY int-flat path.

## Fix
1. Expand nested struct/union members into flat field list by offset
2. Pack by actual type width (char/short/int/ll + BitInsert)
3. Skip int-flat overwrite when member packing ran
4. DECL_ARRAY aos: per-element member-aware pack

## PASS
543, 557, 1013, 1014
