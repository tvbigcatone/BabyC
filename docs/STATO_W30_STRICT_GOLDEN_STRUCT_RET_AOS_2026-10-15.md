# GOLDEN FRUIT CLOSED: v[i] = mk() (549 / 249 / 1016)

## Cause
Pointer-array Index store gate used:
```
CompLocalElemIsPtr[slot] || CompLocalLeafEs[slot] == sizeof(void*)
```
On 64-bit, `struct S { int a; int b; }` has Sizeof=8 == sizeof(void*).
So `v[i] = mk(...)` took the **pointer-word store** path and wrote the
return blob *address* into the first int of the element → garbage.

## Fix
Require **only** `CompLocalElemIsPtr[slot]` for the pointer-array store path.
Struct arrays (LeafEs==8, not ElemIsPtr) fall through to Tok=3 memcpy
(struct-by-value / general Index Call→aos).

## Results
549 PASS, 249 PASS, 1016 antireg PASS; soft unchanged; retained fruits PASS.
