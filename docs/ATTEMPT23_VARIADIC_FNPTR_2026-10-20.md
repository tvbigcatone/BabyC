# BabyC attempt23 — aggregate fix + variadic function-pointer declarators

## Baseline and scope

Fixed point-zero baseline: `babyc(20261020-064516).zip`.
This cumulative delta contains the previously requested libWINAPI filename/FileTime integration, the supplied `exp29_patch_attempt22.diff.txt` applied unchanged, and one parser correction for variadic function-pointer declarators. It is intended to be applied to that point-zero baseline.

## Patch integration and conflict handling

- `exp29_patch_attempt22.diff.txt` passed `patch --dry-run -p1` against the baseline and was applied without rejected hunks.
- The prior libWINAPI integrated delta was overlaid as a separate, non-overlapping change set. The aggregate/IR patch does not touch its files; no content conflict was found between those changes.
- The supplied exp29 patch's implementation and tests 1083–1090 were preserved; parser work is confined to `src/type.c`, with tests 1091 and 1092.

## Parser fix

`TypeParseIdentPart()` now recognizes a terminal `...` in a function-pointer parameter list as declarator syntax, after at least one fixed parameter, and requires the ellipsis to be followed by `)`. This accepts both:

- `long long (*fp)(int, ...) = f;`
- `typedef long long (*VarFn)(int, ...);`

The change is grammar-level rather than a special case for a variable or function name. Existing generic function-type representation is unchanged; invocation tests exercise the declared function pointer in both Tree and Linear STRICT execution.

## Validation performed in this turn

- GCC/Linux build at `-O0`: success, zero compiler warnings and errors.
- Tests 1081–1084, 1085–1088, and 1089–1092 were run in three small blocks under Tree and Linear STRICT (`BABYC_PATH=linear`, `BABYC_STRICT=1`, acceleration disabled). Result: 4/4 PASS in each block for each path (24/24 executions).
- Tests 1091 and 1092 expectations were generated with `gcc -O0`; both print `1099511627774` and pass in Tree and Linear STRICT.
- No full six-configuration matrix, canonical gate, no-tree-fallback coverage gate, A/B benchmark, MSVC build, or libWINAPI CTest rerun was performed in this turn; those remain unclaimed.
- Build products were removed before packaging. The ZIP contains source, tests, documentation and manifests only.

## Remaining scope / caveats

- Variadic function-pointer parsing is covered for named declarators and typedef aliases with one fixed `int` parameter and terminal ellipsis. Broader combinations of nested/abstract declarators and malformed forms are not exhaustively tested here.
- This turn does not independently revalidate the complete BabyC matrix or performance; the matrix, canonical gate and A/B results reported for exp29 are the user's prior validation, not results reproduced here.
- The earlier known open items outside this task are not claimed closed by this delta.
