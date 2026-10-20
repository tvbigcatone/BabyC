# W39 — usare i metadati FP nel layout dei tipi derivati

## Scopo

W39 estende l'uso degli helper `TypeFPStorageSize()` e `TypeFPStorageAlign()` ai punti del frontend che calcolano layout di array e aggregati. Non introduce ancora un tipo semantico distinto per `float`: il frontend continua a mappare `float` e `double` sul tipo FP legacy binary64.

## Modifiche

- `TypeGetMatching()`: il size iniziale di un array di elementi FP usa la dimensione di storage del tipo elemento; l'allineamento usa il relativo helper.
- `TypeInitTrackable()`: il conteggio dei byte di un array FP usa lo storage size dell'elemento, così il bitmap d'inizializzazione non assume implicitamente `ValueType.Sizeof` per FP.
- Layout di struct/union: il calcolo dell'allineamento dei membri FP usa `TypeFPStorageAlign()` sia per il padding dei membri normali sia per i confini dei bitfield (la logica bitfield resta invariata, e gli FP non sono tipi bitfield validi).

## Limiti deliberati

Questa è una migrazione dei punti di consumo della metadata, non l'implementazione completa di binary32. `TypeParseFront()` continua a collassare `TokenFloatType` e `TokenDoubleType`, l'IR non porta ancora la precisione su tutti gli operandi e l'esecuzione non applica arrotondamento binary32 ai confini semantici.

Il probe architetturale resta rosso ed è un criterio di accettazione: non modificarne il riferimento GCC né sopprimere le divergenze.
