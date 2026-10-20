# W30 struct array-member brace (2026-10-15)

## Fruit at dawn
357: `struct S { int arr[3]; int z; }; struct S g = {{1,2,3}};`
STRICT was 1 0 0 — only first init val applied as one int.

## Cause
Brace pack expanded nested struct/union but not TypeArray members.
`arr[3]` consumed 1 DeclInitVal instead of 3.

## Fix
Write-phase cursor: TypeArray member expands to N elements of FromType size.

## PASS
357, 374, 563, 566 + 1015 antireg
Retained: 543, 557, nested member, ptr-arith, FP
