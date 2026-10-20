# BabyC — verifica modifica SCAN13 (SCAN14)

Data: 2026-10-17

## Oggetto

Verifica mirata della sola modifica introdotta in SCAN13 in `src/ir_lin_emit.c`, nel ramo di lowering per accesso a membri tramite array di puntatori:

- prima: lettura diretta `CompLocalFromType[slot]`;
- dopo: lookup tramite `IrLinEmitLocalType(slot)`.

La funzione helper controlla il range dello slot e, durante la compilazione Linear attiva, dà precedenza a `CompileSlot->SlotFromType[slot]`; in assenza di quel metadato usa `IrCompLocalFromTypeOrFrame(slot)`. La modifica è limitata alla fase di lowering e non introduce istruzioni VM o controlli per elemento a runtime.

## Verifiche eseguite

Binario verificato: `src/babyc`, compilato con GCC `-O2` dal tree SCAN13.

| Test | IR/default | Token-only (`BABYC_NO_IR=1`) | Linear VM strict (`BABYC_EXEC=vm`, `BABYC_VM_STRICT=1`) |
|---|---:|---:|---:|
| `442_array_of_fptr` | PASS | PASS | PASS |
| `443_array_fptr_dispatch` | PASS | PASS | PASS |
| `454_complex_dispatch` | PASS | PASS | FAIL preesistente/limite noto |
| `520_bitfield_struct_array_loop` | PASS | PASS | PASS |
| `624_nested_fptr` | PASS | PASS | PASS |
| `961_pointee_stride_large_struct` | PASS | PASS | PASS |

I confronti sono stati eseguiti contro i rispettivi `.expect` (e `.expect.noir` quando presente), normalizzando il banner diagnostico e le differenze di caret come fa il runner del progetto.

### Dettaglio del fallimento strict

`454_complex_dispatch` in Linear strict termina con codice 1 e diagnostica:

`454_complex_dispatch.c:1: strict: tree fallback (body)`

Il test include `<stdio.h>`; è il gap di copertura strict già noto, non una divergenza dell'output funzionale causata da questa modifica. Il test passa sia nel percorso IR/default sia in token-only. Non è stato introdotto né mascherato alcun fallback dalla patch SCAN13.

## Valutazione

- Nessuna regressione rilevata nei sei test mirati per i tre percorsi provati, salvo il limite strict noto di `454_complex_dispatch`.
- Il test mirato non dimostra da solo l'assenza di regressioni nell'intera suite.
- Build GCC: già verificata dopo SCAN13, riuscita senza warning riportati.
- Suite completa e benchmark: non eseguiti in questa verifica; non si dichiara quindi una misura quantitativa dell'impatto prestazionale.
- MSVC/Windows e build 32-bit: non verificati in questa sessione.

## Esito

La modifica SCAN13 è confermata dai test mirati disponibili e non richiede correzioni sulla base di questa verifica. Si sospende qui la scansione degli helper, in attesa dell'analisi del valutatore richiesta dall'utente.
