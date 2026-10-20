# W29rc full delta vs baseline `attachments/babyc.zip`

## Sorgenti
| File | Motivo |
|------|--------|
| `src/ir_fast.c` | A1 InsertShift body_n==2; commenti policy shape ON + bench OFF |
| `src/ir_ops.c` | wrap int32 via unsigned (UBSan-clean) |
| `src/ir_compile.c` | free List Count==1; free fold-init expr; string-join Ident |
| `src/parse.c` | IrLoopRelease BodyIr/BodyTokens |
| `src/platform.c` | PicocWriteAll (warn write) |
| `src/third_party/sqlite/sqlite.c` | ternary ParamType → if/else |
| `src/tools/perf_gate.py` | colonne ast_acc/ast_noa/vm_acc/vm_noa/python |
| `tools/test_string_scan_fastpath.py` | hit prodotto + control NO_SHAPE |
| `tools/bench_isort_fastpath.py` | allineamento env |

## Doc / baseline
| File | Motivo |
|------|--------|
| `docs/CURRENT_STATUS.md` / `HANDOFF.md` | stato W29rc |
| `docs/W29rc_*.md` | policy, gate, chiarezza motori, next steps |
| `docs/perf_baseline_W29rc.json` | baseline dual/multi colonna |
| `docs/archive/` | W* storici spostati |

## Verifica su questo tree
- `make` warn **0**
- suite **853 / 0 / 23**
