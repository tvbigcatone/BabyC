# Compact bytecode struct-array preflight — 2026-10-16

Cumulative fix retained from the preceding delta: in `src/ir_vm.c`, Tok=2 direct index load/store instructions are handled before consulting index metadata, because their `Imm` field represents access width, not an index metadata id. Array snapshot preparation likewise skips Tok=2 and Tok=3 forms that do not encode index metadata. No counters or fallback conditions are manipulated.

Validated gate: `546_ir_struct_array_assign`, `889_vm_struct_array_return_copy`, `892_vm_struct_array_copy_no_call`, `895_vm_dynamic_struct_array_index`, `897_vm_dynamic_struct_array_loop`, `899_vm_dynamic_struct_array_copy`, and `901_vm_dynamic_struct_array_copy_padding` all pass with compact bytecode `runs > 0`. Additional multidimensional/global struct-array tests and the supplemental gate passed in the previous tranche. The full aggregate suite remains unverified due to a 300-second timeout.
