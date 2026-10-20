# BabyC W57 — tracciamento indirizzo/slot per store FP

## Baseline e metodo

Baseline fissa: `babyc(20261015-071830).zip`. Il delta di questo turno è cumulativo rispetto a quel punto zero.

## Riproduzione

`src/tests/open_fp_alias_copy.c` riproduce una divergenza tra Tree/IR e token:

- default e `BABYC_EXEC=vm`: dopo `*p = 2.25f`, la stampa osserva `x=1.00`; la seconda scrittura resta invisibile;
- `BABYC_NO_IR=1`: le scritture sono visibili (`2.25`, poi `3.75`).

## Nuovo dato diagnostico

La strumentazione temporanea di `IrStorePtrElem()` mostra che il ramo di store viene raggiunto con `ElemBase == TypeFP` e con un indirizzo locale non nullo, ma il successivo accesso all'identificatore `x` continua a osservare il valore precedente. La strumentazione è stata rimossa e non è inclusa nel sorgente consegnato.

Questo restringe il problema: non basta correggere la larghezza di scrittura o aggiungere una conversione binary32. Va seguita la relazione tra `LocalVal[]`, l'indirizzo preso da `&x`, l'espressione IR che legge `x` e l'eventuale stato shadow/cache del frame. Una modifica al solo store non è giustificata dai dati attuali.

## Modifiche e stato

- Conservata la correzione W51 in `VariableAllocValueAndCopy()` (refresh del proxy binary32 prima della copia).
- Conservato il riproduttore in `src/tests/open_fp_alias_copy.c`, fuori dai test con `.expect`: è un caso aperto, non un test superato.
- Nessuna modifica sperimentale a `ir_mem.c` inclusa.

## Verifiche

- Build GCC pulita: PASS.
- `tools/test_fp_modes.py`: 28/28 PASS.
- `tools/audit_fp_ir_arch.py`: resta 0/4 modalità conformi.
- Riproduttore: divergenza confermata, quindi il bug resta aperto.
- Suite completa e MSVC: non eseguite/non verificate.

## Prossimo passo

Tracciare il valore dello slot prima e dopo lo store e al momento della lettura dell'identificatore, distinguendo `LocalVal[]` da eventuali valori shadow o cache. Accettare una correzione solo se il riproduttore converge tra default, VM e token senza regressioni FP.
