# P0.13 — STRICT 596 CLOSED (`*out = z` / `&char*`)

## Root cause
`&z` for `char *z` was lowered as **LoadLocal** (blob) because
`CompLocalLeafEs[slot] > 0` (pointer stamp sets ElemSize=`sizeof(void*)`)
was treated as array/struct blob. That passed the **value** of `z` (0)
instead of the **address of the pointer cell**.

Callee then did `*out = p` through a null pointer → caller `z` stayed null.

## Fix (`ir_lin_emit.c` Ampersand)
- Blob only if `CompLocalNDims > 0` or type is struct/union/array
- Pointer cells (`SlotIsPtr` / `FromType==Pointer`) force **not** blob → Tok=4
- Also: pointee size for `*out` from formal `char**` type (word store)

## Verify
- Minimal `setp(&z)` STRICT → `4660` (0x1234)
- `594_memcpy_body_ir` STRICT → `0 ab xxx`
- Suite **960/2**
