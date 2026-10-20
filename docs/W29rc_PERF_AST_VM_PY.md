# W29rc — AST / VM / Python (due accelerazioni)

Policy: **prod = accelerazioni ON**; nei test/bench si spegne al volo e si
riportano **più colonne**.

## Colonne

| Colonna | Env | Significato |
|---------|-----|-------------|
| **ast_acc** | default prodotto | AST/IR tree + shape/kernel ON |
| **ast_noa** | `BABYC_NO_SHAPE_FP=1` `BABYC_NO_KERNEL=1` | AST onesto senza scorciatoie |
| **vm** | `BABYC_EXEC=vm` | bytecode VM (accel prodotto ON) |
| **python** | `python3 bench.py` | riferimento |

## Misura (mediana 3 run, questa macchina)

| prog | ast_acc | ast_noa | vm | python | a/noa |
|------|--------:|--------:|---:|-------:|------:|
| fibc | 0.054 | 0.050 | **0.011** | 0.022 | 0.9× |
| isort | 0.077 | 0.063 | **0.034** | 0.041 | 0.8× |
| structs | 0.086 | 0.092 | 0.045 | **0.037** | 1.1× |
| sieve | 0.111 | 0.164 | **0.024** | 0.093 | 1.5× |
| collatz | 0.075 | 0.076 | **0.046** | 0.066 | 1.0× |
| tri | 0.060 | 0.061 | **0.013** | 0.020 | 1.0× |
| matmul | **0.024** | 0.055 | 0.038 | 0.039 | 2.3× |
| isort_micro | **0.005** | 0.197 | 0.005 | 0.296 | **39×** |
| isort_v2 | 0.248 | 0.256 | 0.262 | — | 1.0× |
| string_proc | **0.020** | 0.386 | 0.019 | 0.204 | **19×** |
| string_proc_v2 | **0.016** | 0.419 | 0.017 | — | **26×** |

## Lettura

- **VM** spesso più veloce dell’AST prodotto sui serie1 (fibc/sieve/tri).
- **ast_noa** è la colonna “onesta” per shape/kernel; su isort_micro/string_proc
  il gap accel è grande → va sempre mostrato accanto all’accel.
- **Python** è un riferimento esterno, non un oracolo di correttezza C
  (per quello si usa GCC / output AST).

## Comando

```bash
BABYC=./src/babyc python3 src/tools/perf_gate.py --runs 3 \
  --write-baseline docs/perf_baseline_W29rc.json
```
