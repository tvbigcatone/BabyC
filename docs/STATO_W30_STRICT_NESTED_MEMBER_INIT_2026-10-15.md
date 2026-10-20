# W30 nested Member InitMap (2026-10-15)

## Fruit
o.in.x = 10 then read/+= failed STRICT "uninitialized variable"

## Cause
StoreIndex Tok=1 marked InitMap only when IX->Left was Ident or Index-of-Ident.
For nested Member (o.in.x) Left is Member — root Ident never found → no mark.

## Fix
Walk Member/Index chain to root Ident before IrFrameMarkBytes.

## PASS
561, 291, 271, 1012 antireg
Retained: 625, 178 cluster, FP, unsigned
