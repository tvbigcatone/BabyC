# P0.5 — STRICT 954 int→FP return CLOSED (2026-10-17)

## Symptom
`double f(int n){ return n; }` under `BABYC_VM_STRICT=1 BABYC_EXEC=vm`
printed `0.000000` instead of `3.000000`. Soft VM OK (VmTreeSkip → tree).

## Root cause
1. Return wrapper in `ir_compile` builds `TokenCast` with `I32=TypeFP` but lower
   peeled the cast (no int→FP arm).
2. Annotate late-arm forced `IR_TF_INT` on every Cast, muddying FP flags.

## Fix
- `ir_lin_emit.c`: Cast lower emits `Imm=2` when target is FP (`IR_TF_FP` or
  `I32==TypeFP`) and source is not FP.
- `ir_compile.c`: set `TypeFlags=IR_TF_FP` on return wrapper; do not force INT
  on FP/pointer casts in the late annotate arm.

## Verify
tree / soft vm / STRICT: `3.000000 1.000000 2.500000 3.000000 8.000000`
Suite: 959 pass / 3 pre-existing fails.
