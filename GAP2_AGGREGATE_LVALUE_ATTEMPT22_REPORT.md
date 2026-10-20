# Attempt22 — aggregate lvalue copy (partial, 2026-10-20)

## Integrated patch
Applied `exp28_patch_attempt21.diff.txt` unchanged on top of the reconstructed baseline + attempt21 overlay. It adds the canonical aggregate type, literal lexer base preservation, exact aggregate metadata for variadic calls, the Linear va_arg type pointer, fast/lower variadic admission guards, the single-evaluation struct initializer fix, and tests 1078–1080.

## New work attempted
Added a type-driven aggregate-lvalue address path in `src/ir_eval.c` and tests 1081/1082 based on the attached `n1` repro plus global/index/pointer-index copies.

## Validation actually performed
- GCC build at `-O0` completed. The successful build log contained no `warning:` or `error:` diagnostics.
- `1082_struct_copy_global_index.c` ran in the local binary and printed the expected `5 6`, `2 1`, `2 1 21`.
- `1081_struct_copy_member_deref.c` did not produce the expected output in the local binary.
- Attempted to run 1075–1080; the local binary reported `va_list is not active` for several tests, so these runs are not passing validation and were not counted as passes.
- Repros `a1`, `c2`, `c3`, `c4`, `c7`, `d2`, `n1` were sampled, not a complete 1032–1082 smoke. Failures remain (including struct copy and variadic activation).
- Full smoke 1032–1082 in blocks, six-configuration matrix, canonical gate, `tree_body`, and A/B were not run.

## Open / known incomplete
The aggregate address attempt is incomplete and does not close the requested root cause: member/dereference/global/array lvalues still do not all consistently yield aggregate storage addresses across Tree and Linear. In particular, repro `n1` remains failing. The patch's other changes are integrated as requested, but attempt22 must be treated as a partial handoff, not a verified fix. No parser work for variadic function-pointer declarators and no c2 work were done.
