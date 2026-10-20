# W29rc — Performance gate

**Data:** 2026-10-12  
**Binary:** W29rc consolidation (`make -O2`)  
**Tool:** `src/tools/perf_gate.py`  
**Baseline file:** `docs/perf_baseline_W29rc.json`

## Cosa misura

- Programmi **serie1**: `fibc isort structs sieve collatz tri matmul`
- Mediana wall-clock su N run (default 7 in scrittura, 5 in check)
- Env fisso: `BABYC_NO_MEMO=1` (niente memo che maschera il costo)
- Tolleranza default: **+5%** (`BABYC_PERF_TOL`)
- `--strict`: exit 1 se regressione oltre tol (con eccezione soft per bench &lt;150ms fino a 2×tol)

**Importante:** i numeri sono **machine-local**. Su un altro host:  
`python3 src/tools/perf_gate.py --write-baseline docs/perf_baseline_W29rc.json`  
poi usa quel file per i check.

## Baseline W29rc (questa macchina, 7 run)

| Prog | median (s) |
|------|----------:|
| fibc | 0.079 |
| isort | 0.097 |
| structs | 0.125 |
| sieve | 0.182 |
| collatz | 0.108 |
| tri | 0.086 |
| matmul | 0.037 |

Self-check `--strict` vs questa baseline: **PASS**.

Vs `perf_baseline_babyc8.json` (storico, host diverso): ordine di grandezza allineato; non usarlo come soglia CI su host nuovi.

## Onestà shape / kernel (5 run, output identico tra modi)

| Prog | default | NO_SHAPE | NO_KERNEL |
|------|--------:|---------:|----------:|
| fibc | 87 ms | 75 ms | 75 ms |
| isort (serie1) | 102 ms | 105 ms | 97 ms |
| structs | 117 ms | 129 ms | 120 ms |
| sieve | 188 ms | 173 ms | 225 ms |
| collatz | 107 ms | 112 ms | 112 ms |
| tri | 88 ms | 84 ms | 84 ms |
| matmul | 40 ms | 36 ms | 86 ms |

Micro shape-sensitive (bench/ root, non serie1):

| Prog | default | NO_SHAPE | ratio |
|------|--------:|---------:|------:|
| isort.c | ~7 ms | ~291 ms | ~42× |
| isort_v2.c | ~339 ms | ~342 ms | ~1× (A1: decoy non accelera) |
| string_proc.c | ~30 ms | ~578 ms | ~20× |

Output default / NO_SHAPE / NO_KERNEL: **identico** sui serie1 misurati.

## Comandi

```bash
cd src && make -j
# nuova baseline su questa macchina
BABYC=./babyc python3 tools/perf_gate.py --runs 7 \
  --write-baseline ../docs/perf_baseline_W29rc.json

# check non-strict (default exit 0 anche con note)
BABYC=./babyc python3 tools/perf_gate.py --runs 5 \
  --baseline ../docs/perf_baseline_W29rc.json

# check strict (fallisce se regressione > tol)
BABYC=./babyc python3 tools/perf_gate.py --runs 5 \
  --baseline ../docs/perf_baseline_W29rc.json --strict
```

## Policy

1. Dopo ogni tocco hot-path (ir_fast, ir_ops, ir_eval, ir_vm, lower): rieseguire gate vs baseline della stessa macchina.
2. Non alzare la tol per far passare un regress; investigare.
3. Shape FP solo se pattern generale (policy W29rc A1/A2).
4. serie1 `isort` ≠ `bench/isort.c` (carichi diversi): non confondere i numeri.
