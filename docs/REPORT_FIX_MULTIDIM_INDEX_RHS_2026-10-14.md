# BabyC — fix accessi multidimensionali come RHS di assegnamento

Data: 2026-10-14

## Esito

Risolto il difetto del VM lineare nelle letture di elementi scalari di array globali multidimensionali quando l'espressione indicizzata è il lato destro di un'assegnazione a una variabile scalare. I casi 3D e 4D ora concordano con Tree in `vm`, `vm_strict` e nel percorso bytecode compatto.

## Causa radice verificata

Il lowerer contiene una specializzazione per assegnamenti di struct/union per valore (`IrExAssign`), che usa `IrExpr.ElemSize` come dimensione del valore copiato. Per un'espressione annidata come `cube[0][0][2]`, però, il nodo indice esterno conserva `ElemSize` della riga/sotto-array (6 nel fixture 3D), non la dimensione dell'elemento finale (1 byte). La specializzazione classificava quindi una lettura scalare come copia di aggregato e generava una lettura errata. La traccia AST aveva mostrato la catena di tre nodi `IrExIndex`, ciascuno con `ElemSize=6`; il ramo di copia veniva raggiunto per le assegnazioni a `a`, `b` e `c`.

## Correzione

- Il ramo di copia per valore conta ora la profondità della catena di indici.
- Usa `ElemSize` come dimensione dell'elemento solo per indicizzazioni singole, per le quali questa specializzazione è implementata.
- Per gli array globali, la dimensione dell'elemento viene riconosciuta come aggregato solo se il tipo risultante da un singolo indice è `struct`/`union`; non si scambia più la dimensione di un sotto-array per la dimensione di un oggetto da copiare.
- Non sono stati introdotti fallback generalizzati, restrizioni del linguaggio o fast path specifici per benchmark.

## Verifiche eseguite

1. `make -C src clean && make -C src -j4`: completato; nessun warning del compilatore GCC.
2. `python3 tools/test_bytecode_vm.py`: **31/31 gruppi passati**, inclusi i percorsi bytecode e le verifiche `vm_strict` già previste dal test harness.
3. Fixture multidimensionali eseguite in `tree`, `vm` e `vm_strict`:

| Fixture | Output atteso e osservato | Esito |
|---|---|---|
| `831_vm_multidim_array.c` | `65535 32768 -17 14` | 3 modalità OK |
| `832_vm_pointer_array.c` | `10 40 30` | 3 modalità OK |
| `833_vm_multidim_3d.c` | `255 128 254` | 3 modalità OK |
| `834_vm_multidim_4d.c` | `65535 65534` | 3 modalità OK |

4. Il test harness verifica inoltre che 833 e 834 eseguano il bytecode compatto (`attempts=1`, `runs=1`) e passino in `vm_strict` senza fallback ad albero.

## Ambito e limiti

Il fix corregge la classificazione erronea dell'espressione RHS annidata come copia di aggregato. Non dichiara aggiunto il supporto alla copia di `struct`/`union` attraverso catene multidimensionali: il percorso esistente di copia per valore è implementato per il caso a singolo indice e il fix non amplia tale ambito. Non sono stati eseguiti benchmark prestazionali perché la modifica corregge una classificazione semantica e non introduce un'ottimizzazione.

## File modificato

- `src/ir_lower.c`
