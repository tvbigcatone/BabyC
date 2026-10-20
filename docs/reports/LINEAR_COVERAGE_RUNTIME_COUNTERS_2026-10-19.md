# Linear — contatori runtime di copertura

**Snapshot:** baseline loop_promotion + delta cumulativo 126 + `LINEAR_COVERAGE_MAP.md`.
**Configurazione:** `BABYC_PATH=linear`, `BABYC_STRICT=0`, `BABYC_ACCEL=1`, `BABYC_COV=1`, `BABYC_VM_PROFILE=1`.
**Corpus:** 1008 test numerati con `.expect`; durata 10.24s.

I contatori sono quelli già presenti nel codice: nessun contatore o ramo di strumentazione è stato aggiunto per questa fase. Sono emessi a fine processo solo quando `BABYC_COV` è attivo; `BABYC_VM_PROFILE` abilita il dettaglio. La normalizzazione del confronto rimuove i tag di profilazione, incluso `[babyc-bytecode-vm-prod]`; nessun output di programma viene filtrato. Questa è copertura dinamica dei corpi/funzioni effettivamente eseguiti, non copertura statica di ogni nodo AST né dell’intero standard C.

## Somma dei contatori

| Contatore | Totale |
|---|---:|
| `fncode_body` | 9669 |
| `lin_inst` | 324567469 |
| `profile_attempts` | 10735 |
| `profile_body_run_fail` | 1 |
| `profile_body_runs` | 9669 |
| `profile_compile_fail` | 38 |
| `profile_index_copy_fuse` | 5 |
| `profile_ineligible` | 26 |
| `profile_local_add_fuse` | 0 |
| `profile_ptr_index_admit` | 256 |
| `profile_reject_depth` | 0 |
| `profile_reject_ptr` | 939 |
| `profile_reject_small` | 0 |
| `profile_run_nested` | 183 |
| `profile_run_top` | 9486 |
| `profile_tiny_tree` | 0 |
| `tree_body` | 36 |
| `tree_expr` | 0 |
| `tree_stmt` | 0 |

## Fallback Tree osservati: 34 test hanno `tree_body > 0`

| Test | Corpi Tree eseguiti | Corpi Linear eseguiti |
|---|---:|---:|
| `024_type_coercion.c` | 3 | 7 |
| `062_initializers.c` | 1 | 1 |
| `249_array_alias_init.c` | 1 | 1 |
| `258_member_array_init_decay.c` | 1 | 1 |
| `261_row_decay_address.c` | 1 | 2 |
| `345_member_assign.c` | 1 | 1 |
| `363_bodyir_member_index.c` | 1 | 6 |
| `395_array_fn_loop_local_global.c` | 1 | 5 |
| `410_member_cache_call.c` | 1 | 100 |
| `414_ptr_member_arith.c` | 1 | 1 |
| `416_fptr_kitchen_sink.c` | 1 | 2 |
| `446_ir_loop_body_pure.c` | 1 | 1 |
| `462_array_decay_ret.c` | 1 | 1 |
| `463_struct_ptr_ret_chain.c` | 1 | 1 |
| `464_outparam_array.c` | 1 | 1 |
| `501_bitfield_fn_arg.c` | 1 | 1 |
| `510_bitfield_call_index.c` | 1 | 3 |
| `516_bitfield_ir_ptr.c` | 1 | 1 |
| `524_bitfield_multi_local_call.c` | 1 | 2 |
| `534_storm_bf_multi_struct.c` | 1 | 1 |
| `535_main_continue_after_extract.c` | 1 | 1 |
| `614_pseudo_dispatch_vm.c` | 1 | 5 |
| `615_pseudo_graph_dfs.c` | 1 | 1 |
| `666_fp_member_index.c` | 1 | 50 |
| `669_fp_thin_outparam.c` | 1 | 5 |
| `671_fp_struct_inline_for.c` | 1 | 5 |
| `673_fp_nested_thin_for.c` | 1 | 39 |
| `771_flag_prop_call_scale.c` | 1 | 6 |
| `841_real_a08_text.c` | 1 | 6000 |
| `846_real_a11_bfs.c` | 1 | 2500 |
| `912_call_arg_addr_of_index.c` | 1 | 1 |
| `945_sibling_for_same_name.c` | 1 | 0 |
| `981_printf_ptr_member_ternary.c` | 1 | 1 |
| `985_many_params_mixed.c` | 1 | 1 |

## Raggruppamento dei rifiuti osservati

### `vm-rej`


### `compile_reject`

- `compile_reject=lower`: 241

### `lower_reject` raggruppati per `stmt_kind`

- `stmt_kind=1`: 117
- `stmt_kind=10`: 48
- `stmt_kind=4`: 7
- `stmt_kind=3`: 5
- `stmt_kind=8`: 1
- `stmt_kind=7`: 1

## Confronto output non-STRICT contro `.expect`

- Differenze rilevate dal runner di profilazione: **0**.
- La suite STRICT separata resta il gate di correttezza; il fallback Tree è contato come fatto dinamico soltanto quando `tree_body` è incrementato.

