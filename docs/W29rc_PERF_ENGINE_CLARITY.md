# Chiarezza motori: AST vs VM vs kernel condivisi

## Fatto di codice

Sotto `BABYC_EXEC=vm`, `IrLinKernel` / `IR_K_INSERT_SHIFT` (e altri) **chiamano
`IrTryFastInsertShift` e gli altri `IrTryFast*`** — le stesse funzioni dell’AST
accelerato. Quindi:

| Colonna | Cosa misura davvero |
|---------|---------------------|
| **ast_acc** | AST + fast path C |
| **vm_acc** | VM dispatcher + **stessi** fast path C |
| **ast_noa** | solo motore AST (NO_SHAPE + NO_KERNEL) |
| **vm_noa** | solo motore VM (EXEC=vm + NO_SHAPE + NO_KERNEL) |
| **python** | riferimento esterno |

**Confrontare i motori:** solo `ast_noa` vs `vm_noa`.  
**ast_acc ≈ vm_acc** su isort/string_proc **non** prova due motori uguali: prova
che entrambi passano dal kernel C condiviso.

## Misura (mediana 3 run)

| prog | ast_acc | ast_noa | vm_acc | vm_noa | python |
|------|--------:|--------:|-------:|-------:|-------:|
| fibc | 0.079 | 0.075 | 0.013 | **0.013** | 0.030 |
| isort | 0.098 | 0.099 | 0.046 | **0.052** | 0.061 |
| structs | 0.124 | 0.145 | 0.070 | **0.069** | 0.052 |
| sieve | 0.178 | 0.240 | 0.033 | **0.126** | 0.135 |
| collatz | 0.114 | 0.107 | 0.061 | **0.066** | 0.098 |
| tri | 0.085 | 0.080 | 0.020 | **0.017** | 0.032 |
| matmul | 0.036 | 0.083 | 0.053 | **0.051** | 0.052 |
| isort_micro | 0.007 | 0.304 | 0.011 | **0.331** | 0.471 |
| isort_v2 | 0.349 | 0.341 | 0.348 | **0.339** | — |
| string_proc | 0.031 | 0.531 | 0.029 | **0.623** | 0.256 |
| string_proc_v2 | 0.024 | 0.599 | 0.025 | **0.602** | — |

### Ratio utili

| prog | ast_acc/noa | vm_acc/noa | **vm_noa / ast_noa** |
|------|------------:|-----------:|--------------------:|
| fibc | 0.9× | 1.0× | **0.17×** (VM ~6× più veloce) |
| sieve | 1.4× | 3.9× | **0.52×** |
| tri | 0.9× | 0.9× | **0.22×** |
| isort_micro | **42×** | **31×** | **1.09×** (motori pari; il 42× è solo kernel) |
| string_proc | **17×** | **21×** | **1.17×** (idem) |
| isort_v2 | 1.0× | 1.0× | **0.99×** (nessun kernel shape) |

## Lettura in una frase

- Su **isort_micro / string_proc**: quasi tutto lo speedup è il **kernel C condiviso**, non la VM.
- Su **fibc / tri / sieve**: la **VM senza kernel** batte ancora l’AST senza kernel → vantaggio di motore reale.
- **Python** resta un riferimento esterno (a volte più veloce di ast_noa, a volte no).

## Comando

```bash
BABYC=./src/babyc python3 src/tools/perf_gate.py --runs 3
```
