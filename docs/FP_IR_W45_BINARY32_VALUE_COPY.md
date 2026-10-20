# W45 — confine di conversione binary32 nei valori temporanei

## Modifiche

- `ExpressionAssignFP()` arrotonda a binary32 quando il tipo destinazione dichiara `FPStorageBinary32`, anche quando il valore è uno slot temporaneo e non ha memoria oggetto associata. Il commit della memoria non è sufficiente a garantire l'arrotondamento dei valori di ritorno.
- `VariableAllocValueAndCopy()` copia semanticamente un valore binary32 tramite il suo slot FP (`double`), invece di copiare quattro byte grezzi da uno slot di esecuzione. Mantiene i flag di inizializzazione/qualificatori e usa l'allocatore tipizzato per ottenere lo spazio adatto.

## Limiti noti

Il frontend continua intenzionalmente a mappare `float` e `double` sul tipo storico `FPType`. Quindi questi rami sono preparatori e non risolvono ancora il probe architetturale. Un tentativo locale di attivare `FloatType` ha prodotto `881_fp_user_func_return.c` fallito nel percorso token e non è incluso in questo delta.

## Verifiche

- Build pulita GCC: PASS; zero warning/errori.
- Gate `tools/test_fp_modes.py --babyc src/babyc`: 28/28 PASS.
- Probe `tools/audit_fp_ir_arch.py --babyc src/babyc`: 0/4 modalità conformi, atteso come limite noto.
- Nessun file `.expect` modificato.
