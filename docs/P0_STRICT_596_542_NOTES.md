# P0.7 — STRICT 596 / 542 investigation (2026-10-17)

## 596 `*out = z` (char **out)

**Symptom:** STRICT `0 (null) xxx`; tree/soft `0 ab xxx`.

**Changes attempted:**
1. Annotate `*Ident`: if pointee is pointer (`T**`), set `IR_TF_PTR` + `ElemSize=sizeof(void*)`.
2. Lower `*p=` store: force es=sizeof(void*) when LHS/RHS TF_PTR, or RHS SlotIsPtr / CompLocalFromType pointer.

**Still silent under STRICT.** Store size alone may not be the root: after *out=z, caller z stays 0 (not a truncated non-null). Suspect Light/Locals address contract on `&z` across nested Call, or ParamVal for `out` not holding parent cell address in the pure-STRICT path. Soft likely tree-skips `copy_s`.

**Next:** instrument Tok=4 / ParamVal / StoreIndex Tok=2 under STRICT-only on 596; compare Light vs Step Locals pointer identity.

## 542 token nested call

**Symptom:** STRICT garbage int; tree `10`. Comment: IrCallDepth=0 isolation / HasReturn across token mid. Separate from 596; do not mix fixes.

## Still closed this stack

- 954 int→FP return STRICT
- 1040/567 designated struct STRICT
- Suite 959/3 pre-existing
