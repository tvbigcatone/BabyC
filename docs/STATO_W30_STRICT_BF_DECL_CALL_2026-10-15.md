# Fallen fruits: bitfield struct Decl = Call (514/647)

## Found
DECL_STRUCT lowered zero-init and **dropped** S->Expr Call init.
Soft+STRICT both wrong; tree OK.

## Fix
1. After DECL_STRUCT emit, if Expr is Call → LowerRec + Tok=3 memcpy (non-BF).
2. Bitfield members → residual (return 0): linear BitInsert on Member store
   not yet reliable; tree oracle keeps 10 3.

## Results
514/647 PASS (soft+STRICT via honest residual for BF).
549/249 golden still PASS. 535 PASS.
