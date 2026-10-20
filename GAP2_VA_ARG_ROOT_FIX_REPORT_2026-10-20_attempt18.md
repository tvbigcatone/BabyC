# Gap 2 — attempt18: pointer dereference typing and expression correctness

## Scope
Cumulative delta over attempt17 / fixed loop-promotion baseline. Keeps the attempt16 native-call argument packing that classifies arguments from static/canonical type metadata and evaluates each argument once. This turn integrates the root fixes supplied for attempt17's local pointer typing and Linear lowering, then adds coverage for unary dereference arguments, pointer arithmetic in both operand orders, multidimensional-array dereference, FP pointees, and struct-pointer member access.

## Source changes
- `src/ir_compile.c`: stamp complete declared pointer types for local pointer declarations (including pointer typedefs and double pointers); retain lowering metadata for pointee stride; use the full local array declaration type while annotating dereference and array indexing.
- `src/ir_lin_emit.c`: add declaration-derived pointee typing for local dereferences and pointer arithmetic; avoid taking scalar stride from an unrelated slot; route pointer-pointee increments through the typed lowering path.
- `src/ir_mem.c`: resolve pointer arithmetic using the pointer operand regardless of operand order; add type recovery for struct-pointer arithmetic in `->` and `(*p).member` paths; preserve multidimensional-array row decay semantics in pointer resolution.
- `src/ir_eval.c`: handle the nested dereference of a multidimensional array row as array decay to the first scalar, avoiding loading the first scalar as a pointer word.
- `src/expression.c`: support the standard commutative form `integer + pointer`; mark computed pointer arithmetic results initialized so member access through `(p + n)->field` and `(*(p + n)).field` is not incorrectly rejected.
- `src/tests/1068_args_unary_deref.c` and `.expect`: cover naked unary-`*` function arguments across scalar/pointer/FP/typedef/side-effect cases.
- `src/tests/1069_deref_composite_and_fp.c` and `.expect`: cover `*(1 + ip)`, `**m`, float/double pointees and stores, struct-pointer member access, struct dereference copy, and `*(fields + 1)` argument packing.

## Build and correctness validation
Clean GCC build: success, 0 warnings, 0 errors. Full numbered matrix includes 1053 cases and passed in all six configurations:

| Configuration | Result | Canonical gate | Tree body fallback |
|---|---:|---:|---:|
| Tree A0 | 1053/1053 | 1053/1053, invalid_nodes=0 | not applicable |
| Tree A1 | 1053/1053 | 1053/1053, invalid_nodes=0 | not applicable |
| Linear STRICT A0 | 1053/1053 | 1053/1053, invalid_nodes=0 | not applicable |
| Linear STRICT A1 | 1053/1053 | 1053/1053, invalid_nodes=0 | not applicable |
| Linear non-STRICT A0 + `--require-no-tree-fallback` | 1053/1053 | 1053/1053, invalid_nodes=0 | tree_body=0, violations=0 |
| Linear non-STRICT A1 + `--require-no-tree-fallback` | 1053/1053 | 1053/1053, invalid_nodes=0 | tree_body=0, violations=0 |

The six matrix stdout summaries and full per-test logs are included in `validation/attempt18_matrix/`. Tests 1064–1069 all pass. The new 1069 output was generated from `gcc -O0` and matches in Tree and Linear.

## Alternating A/B performance check
Seven alternating attempt15/attempt18 pairs were run for each of the four requested benchmarks at both accelerator settings. All outputs matched. Median time ratios are attempt18 / attempt15 (lower is faster):

| Benchmark | ACCEL 0 ratio | ACCEL 1 ratio |
|---|---:|---:|
| string_scan | 0.9380 | 1.0285 |
| string_scan_bounded | 0.9847 | 1.0980 |
| struct_score | 1.0227 | 1.0587 |
| dfs_rec | 1.0337 | 1.1312 |

Performance is mixed and does **not** meet a no-regression standard yet: ACCEL 1 medians show slowdowns on these samples, most notably `dfs_rec` (+13.1%) and `string_scan_bounded` (+9.8%). Host variance is visible in individual samples, so these measurements are not proof of a stable regression, but they are reported without masking. Raw samples are in `validation/attempt18_ab_results.json`.

## Platform verification and status
MSVC/LLP64 was not available in this run and remains unverified. The source uses portable C constructs and `sizeof(void *)` / existing type-size helpers, without new platform-specific `#ifdef` branches. The previously deferred struct brace initializer bug involving a struct member `char[N]` initialized from a string remains out of scope.

**Correctness and Linear coverage gates are green. Performance is mixed and needs follow-up before declaring the optimization/performance objective fully closed.**
