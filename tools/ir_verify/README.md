# F2 — IrVerify (skeleton)

Goal (PIANO_DEFINITIVO F2): after `IrCompileBody`, walk every `IrExpr` and assert:
- TypeFlags has IR_TF_RESOLVED when node is evaluated on hot path
- ElemSize set for Index / ptr-deref
- ValWidth consistent for integer narrow
- No runtime type decision required (`IrExprIsFP` sites should become dead)

Status: **skeleton only** — full hook in `ir_compile.c` deferred until F0 doc closed and
signedness regress (718, 720–723, 17 repro) confirmed green on CI host.

## Manual gate today

```bash
# signedness / narrow regress
for t in 718_uchar_wrap_loop 720_global_store_in_for 723_uchar_array_sum_signext \
         725_ull_cast_shift_arith 727_narrow_incr_loop; do
  ./babyc tests/${t}.c
done
```

## Next implementation step

1. `IrVerifyExpr(IrExpr*)` / `IrVerifyStmt(IrStmt*)` in `ir_compile.c` or `ir_verify.c`
2. Call at end of successful body compile when `getenv("BABYC_IR_VERIFY")`
3. `ProgramFail` on violation in test builds only
