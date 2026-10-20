# Bench migration — unified suite (2026-10-12)

## What changed

- All programs live under `bench/cases/{micro,classic,variant,real,stress}/`.
- Single launcher: `python3 bench/runners/run_all.py` (full suite).
- Filtered runs: `python3 bench/runners/run_bench.py --set … --gate … --only …`.
- **Uniform columns** on every report:
  `ATS_no | ATS_acc | VM_no | VM_acc | python | gcc`
- Registry: `bench/MANIFEST.yml`.

## Renames

| Old path | New path |
|----------|----------|
| `bench/fib.c` | `cases/micro/fib_iter.c` |
| `serie1/fib.c` | `cases/classic/fib_rec.c` |
| `serie1/fibc.c` | `cases/classic/fib_rec_count.c` |
| `bench/isort_v2.c` | `cases/micro/isort_decoy.c` |
| `bench/string_proc.c` | `cases/micro/string_scan.c` |
| `bench/string_proc_v2.c` | `cases/micro/string_scan_bounded.c` |
| `serie2/crc.c` | **dropped** (byte-identical to classic) |
| `serie2/*_v.c` | `cases/variant/<name>.c` |
| `noscorciabili/*` | `cases/stress/` |
| `s3/cat_*.c` | `cases/stress/` |

Legacy folders (`serie1/`, `serie2/`, `noscorciabili/`, `s3/`, root micros) are **kept** for path compatibility during transition; prefer `cases/`.

## Smoke (runs=1, this host)

| set | OK | notes |
|-----|---:|-------|
| micro | 10/11 | `list_walk` DIFF on **VM_no** only (prints 0; product residual, not migration) |
| classic | 14/14 | |
| variant | 12/12 | |
| real | 19/19 | |
| stress | 11/11 | |

Shape check: `micro.isort` ATS_acc ≪ ATS_no; `micro.isort_decoy` no speedup (A1).

## Commands

```bash
make -C src -j
python3 bench/runners/run_all.py --runs 3
python3 bench/runners/run_bench.py --gate shape --runs 3
```
