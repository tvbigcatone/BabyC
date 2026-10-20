# Surprise fruit: v[i] = mk() — struct return into array element (549/249)

## Symptom
STRICT prints garbage (stack addresses as ints); soft/tree OK.
`v[2]=v[0]` works; only **Call RHS** fails.

## Root causes found
1. **nbytes at compile**: Fr->LocalElemSize unset during FnCodeCompile.
   Fix: CompLocalLeafEs / CompLocalFromType Sizeof for aos dest + Call ReturnType.
2. **RetBuf match**: IrLinRet only scanned `Fr->NumLocals` and required LocalFromType.
   Fix: scan all owned slots; accept LocalTyp / elem size.
3. **Still open**: even with RetBuf path, result remains garbage — next dig:
   - Does `return r` lower to blob address or scalar LocalVal?
   - Does Tok=3 run after Call with R[src]=RetBuf?
   - DeclFromType NULL on StarPtr-style struct array DeclIsPtr=1 confusion?

## Tests
549, 249, 1016 (antireg expected 1 2 3 4 — still FAIL under STRICT)
