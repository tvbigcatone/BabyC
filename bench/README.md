# BabyC unified bench suite

## Layout

```
bench/
├── MANIFEST.yml          # case registry (id, path, tags, gates)
├── cases/
│   ├── micro/            # short kernels + shape-sensitive (isort, string_scan)
│   ├── classic/          # ex serie1 — comparative / perf_gate
│   ├── variant/          # ex serie2 — algorithm variants
│   ├── real/             # i* + a* real-code slice
│   └── stress/           # anti-kernel + VM gate (ex noscorciabili + s3)
├── runners/
│   ├── run_all.py        # ★ full suite launcher
│   ├── run_bench.py      # filtered runs (--set / --gate / --only)
│   └── lib_bench.py      # engine (6 columns)
├── refs/                 # optional prebuilt gcc binaries
└── results/              # latest.json / latest.md
```

Legacy folders (`serie1/`, `serie2/`, `noscorciabili/`, `s3/`, root `*.c`) remain
for reference during transition; **new work uses `cases/` only**.

## Columns (uniform)

| Column | Meaning | Environment |
|--------|---------|-------------|
| **ATS_no** | Body-IR tree, no kernel/shape | `BABYC_NO_KERNEL=1 BABYC_NO_SHAPE_FP=1` |
| **ATS_acc** | Body-IR tree, accel ON | product default |
| **VM_no** | linear VM, no kernel/shape | `BABYC_EXEC=vm` + no kernel/shape |
| **VM_acc** | linear VM, accel ON | `BABYC_EXEC=vm` |
| **python** | CPython (if `.py` exists) | |
| **gcc** | native `-O2` (compile or `refs/`) | |

Always `BABYC_NO_MEMO=1`. Output must match gcc (or python) or the row is `DIFF`.

## Commands

```bash
# build interpreter
make -C src -j

# full suite (all series)
python3 bench/runners/run_all.py

# subsets
python3 bench/runners/run_bench.py --set micro
python3 bench/runners/run_bench.py --set classic --runs 3
python3 bench/runners/run_bench.py --gate shape
python3 bench/runners/run_bench.py --only isort,string_scan,fib_rec

# skip slow refs
python3 bench/runners/run_all.py --skip-gcc --runs 1
```

Results: `bench/results/latest.md` and `latest.json`.

## Naming notes

| Old | New |
|-----|-----|
| `bench/fib.c` (iterative) | `cases/micro/fib_iter.c` |
| `serie1/fib.c` (recursive) | `cases/classic/fib_rec.c` |
| `serie1/fibc.c` | `cases/classic/fib_rec_count.c` |
| `bench/isort_v2.c` (decoy) | `cases/micro/isort_decoy.c` |
| `bench/string_proc*.c` | `cases/micro/string_scan*.c` |
| `serie2/crc.c` | dropped (duplicate of classic) |
| `noscorciabili/*` + `s3/cat_*` | `cases/stress/` |
