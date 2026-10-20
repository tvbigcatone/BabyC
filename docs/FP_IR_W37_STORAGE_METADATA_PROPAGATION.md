# FP IR W37 — propagatione della metadata di storage

## Scopo

W37 prosegue il lavoro strutturale W36. La metadata `ValueType.FPFormat` viene ora consultata nei punti in cui il tipo concreto è disponibile, invece di ricostruire sempre la dimensione/allineamento FP da `sizeof(double)` o dai campi legacy.

## Modifiche

- `src/type.c`: la creazione di tipi array deriva dimensione e allineamento dell'elemento FP da `TypeFPStorageSize()` e `TypeFPStorageAlign()`; il layout di struct/union usa l'allineamento FP dichiarato per membri scalari FP.
- `src/ir_mem.c`: lo stride di indicizzazione per un tipo FP noto usa `TypeFPStorageSize()`.
- `src/ir_lower.c`: il calcolo del layout aggregato legge dimensione e allineamento FP dai helper centralizzati.

## Limiti espliciti

Questa non è ancora l'implementazione completa di `float` binario32. `TypeParseFront()` continua a ricondurre `float` e `double` al tipo legacy `FPType` binario64; l'IR non trasporta ancora la precisione FP su tutti gli operandi/risultati e i confini di conversione non applicano il rounding binario32. I percorsi che ricevono solo `ElemBase == TypeFP` senza un `ValueType` concreto continuano necessariamente a usare la dimensione legacy. Il probe architetturale deve quindi restare rosso: non è stato indebolito né aggiornato per nascondere la discrepanza.

## Verifica effettuata

- Build `make clean && make -j2`: PASS, nessun `warning:` nel log.
- `tools/test_fp_modes.py --babyc src/babyc`: PASS, 28/28.
- Regressioni FP esistenti `023_floating_point`, `063_float`, `666_fp_member_index`: output confrontato dopo normalizzazione CRLF, PASS in default, VM e token.
- `tools/audit_fp_ir_arch.py --babyc src/babyc`: FAIL atteso, 0/4 modalità allineate a GCC; le discrepanze `sizeof(float)`, array di `float`, rounding binary32 e layout aggregato sono ancora aperte.
- La suite completa non è stata usata come gate W37; il precedente tentativo su questa baseline aveva superato il limite di tempo dell'ambiente.

## Passo successivo

Introdurre una distinzione reale tra `float` e `double` nel frontend/type system, propagare il formato nell'IR e definire i punti di rounding/conversione prima di modificare ulteriori accessi memoria. Quindi verificare array, struct, pointer arithmetic, chiamate/return e tutti i backend (token, Tree/default, VM lineare, VM compatta).
