# P0 — Unified storage descriptor (through P0.4)

## Goal

One structured view of local/param layout so CompLocal*, FnCodeSlot, and
IrCallFrame stamping share a single fill path.

## API

| Helper | Role |
|--------|------|
| `FromDecl` / `FromType` | fill descriptor |
| `StampCompLocal` / `StampFnSlot` | compile-time sinks |
| `ApplyFrame` | runtime Local* fields (flag-gated) |
| `ApplyParamMeta` | ParamIsPtr / ParamTyp / ParamName |
| `ParamIsPtrType` | formal ptr/array/struct/union? |
| `BindParamArg` | ArgVals bind: meta + ParamVal |
| `ApplySlotKind` | Meta->SlotKind → LocalIsPtr |
| `StampDecl` / `StampDeclEs` / `StampDeclFull` | convenience |

### ApplyFrame flags

`PTR` `FPTR` `INIT` `SKIP_OWNED` `ELEMSIZE` `NAME` `TYPE`

## Migration status

| Site | Status |
|------|--------|
| Decl CompLocal / FnSlot / Frame (ptr,fpt,array,struct,enum,bare) | done |
| Assign `IR_TF_PTR` | done |
| `CompileStampPtrSlot` | wrapper |
| ParamArray bind meta | done |
| ArgVals leaf / kernel / body / goto paths | **BindParamArg** |
| Meta SlotKind template seed | **ApplySlotKind** |
| Pre-clear LocalIsPtr loops (reset) | still direct (intentional zero) |

## Policy

- Dual-write; hot paths keep direct reads.
- **P0.25:** dual-read CompLocal+Frame → `IrCompLocal*OrFrame` accessors.

- Nested compile: never set TYPE on live frame.
- Param value packing stays at call site except ArgVals (bits already computed).

## Verify

Suite: 959 pass / 3 fail (097, 699, bytecode 820/825) — baseline pre-existing.
