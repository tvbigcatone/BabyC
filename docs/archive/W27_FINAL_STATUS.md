# W27 — stato finale

## Suite

| modalità | risultato |
|---|---|
| vm_strict | 483 / 346 |
| vm | 828 / 1 (138 colonna off-by-1) |
| default | 829 / 0 |

## Fix chiusi

| fix | note |
|---|---|
| CallN → 255 | fan-out non è il limite |
| `for(;;)` | loop infinito con break |
| malloc/free printf gate | fast-path gated sul nome |
| `sizeof(typedef var)` | `IrCompileLocalElemSize` per scalare |
| deref args + NULL check | `printf("%d", *p)` + `ProgramFail` |
| struct element assign | rifiuto compile-time `ElemSize > 4` |
| ternario array decay | `IR_TF_INT` su braccia const intere |
| const assign | `Tok=1` init + check runtime |

## Residui (346)

| categoria | n | causa |
|---|---|---|
| compile_fail | ~200 | multi-dim, fptr, assert, ternario complesso |
| no_prof | ~144 | char**, static, bitfield |

## Prossimi target

- Multi-dim arrays: dichiarazione e accesso `m[i][j]`
- assert: riconoscimento `__assert_fail`
- char** usage-aware gate
