# REPORT W29rc — consolidation

**Data:** 2026-10-12  
**Baseline:** `attachments/babyc.zip`  
**Criterio A1/A2:** tenere fast-path solo se pattern generale; niente gaming dei bench.

## Modifiche codice

### `src/ir_fast.c`

1. **A1 — `IrTryFastInsertShift`**
   - Rimosso il ramo che accettava self-assign inerti `key = key`.
   - `body_n` richiesto **== 2** (solo shift + decremento).
   - Commenti aggiornati: decoy non accettati.
2. **A2 — `IrTryFastPtrScanPred`**
   - Nessun cambio logico: pattern scan-count su `char*` ritenuto generale.
   - Header file aggiornato per descrivere entrambi i kernel in questi termini.

### Documentazione

- `docs/CURRENT_STATUS.md`, `docs/HANDOFF.md` riscritti (W29rc).
- W27–W35, raw JSON/TXT e piani collaterali → `docs/archive/`.
- `W36_ARCHITECTURE_AND_FASTPATH_AUDIT.md` lasciato come storico; decisioni vive in CURRENT.

## Verifiche

| Gate | Risultato |
|------|-----------|
| `make clean && make -j` | OK |
| `run_suite.py` | **853 / 0 / 23** |
| `test_bytecode_vm.py` | **56 PASS / 0 FAIL** |
| `test_string_scan_fastpath.py` | PASS |
| isort / isort_v2 vs GCC | output `29665` identico |
| ASan+UBSan+LSan smoke (944, 132, 463, 694, 004_struct) | nessun report |

## Tempi shape ON vs OFF (mediana ~5 run, indicativi)

| Bench | shape ON | shape OFF |
|-------|---------:|----------:|
| isort.c | ~5.3 ms | ~220 ms |
| isort_v2.c | ~300 ms | ~299 ms |
| string_proc.c | ~19 ms | ~502 ms |
| string_proc_v2.c | ~17 ms | ~478 ms |

Interpretazione: il kernel insertion-sort resta efficace sul pattern canonico; `isort_v2` non è più accelerato dal decoy (comportamento voluto). String scan resta efficace sul pattern generale.

## Non fatto (fuori scope W29rc)

- Fix warning GCC platform/sqlite
- Sanitizer full suite
- Gate perf globale / Callgrind
- Nuove ammissioni VM / feature
