# W30 — inventario gap VM (post-W29rc)

**Tree:** baseline `babyc.zip` + `babyc_w29rc_full_delta.zip`  
**Metodo:** ogni `src/tests/*.c` con `.expect` → tree vs `BABYC_EXEC=vm`, stdout norm come suite.

## Numeri

| Classe | N |
|--------|--:|
| Totale con expect | 851 |
| **both_ok** (tree=vm=expect) | **828** (~97.3%) |
| **vm_mismatch** | **23** |
| vm_timeout | 0 |
| tree fail / vm ok | 0 |

`tools/test_bytecode_vm.py`: PASS (campione dedicato).

## I 23 mismatch (cluster)

### A — Ptr walk / confronto (risultato sbagliato, rc=0) — **priorità #1**
| Test | tree | vm |
|------|------|-----|
| 183_ptr_walk_sum | 10 | 0 |
| 193_while_ptr_cond | 15 | 0 |
| 355_int_ptr_walk | 10 | 0 |
| 361_int_ptr_scan_bound | 10 | 0 |
| 656_fp_miss_ptr_walk | 15 | 0 |
| 188_ptr_cmp_array | 1 1 1 1 | 1 0 1 0 |

**Repro minimo:** `p = a; (p < a+4)` → tree 1, **vm 0**; dopo `p=p+1` il confronto può tornare 1.  
`p == a` ok, `e-p` con `e=a+4` → 4 ok, ma **relazionale** `p < e` → 0.  
Ipotesi: path relazionale ptr in lower/VM usa compare signed/errato o forma `ident < arr+const` mal abbassata.

### B — Ptr arith / deref → SIGSEGV (rc=-11)
163, 166, 177, 238, 347, 390, 399, 409, 414, 698, 719, 724, 749, 751  
Spesso store/load tramite `p+i` o walk; da affrontare dopo stabilizzare confronti (A).

### C — Uninit / diagnostica
| Test | Note |
|------|------|
| 255_multidim_cell_uninit | caret/posizione diversa, entrambi rc≠0 |
| 257_member_array_uninit | tree diagnostica, vm stampa `0` (non trap) |

### D — Storm parziale
| 614_storm_nested… | tree `f 21\nw 50`, vm si ferma a `f 21` poi SEGV |

## STRICT
Sui 23 mismatch, `vm_strict` non “salva”: stessi rc; **nessun tree-fallback** segnalato — falliscono in bytecode puro.

## Piano chiusura (ordine)

1. **A — relational ptr** (`p < a+N` / `p < e`): fix + test 178/183/188/345/351/666  
2. **B — ptr + offset load/store**: dopo A, ridurre SEGV  
3. **C — uninit member array**: allineare trap a tree  
4. **D — 614**: se ancora aperto dopo A/B  

Ogni fix: output tree≡vm≡expect; misurare opzionale `vm_noa` se hot path.

## File dati
`vm_gap_inventory.json` (lista nomi mismatch).
