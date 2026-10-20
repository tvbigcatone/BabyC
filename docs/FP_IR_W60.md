# BabyC — W60: isolamento del difetto di store tramite puntatore

## Baseline

Il delta è cumulativo e si applica direttamente a `babyc(20261015-071830).zip`.

## Nuova prova

Aggiunto `src/tests/open_fp_alias_minimal.c`, che elimina le chiamate a funzioni intermedie: esegue soltanto `float x`, `float *p = &x`, due store `*p = ...` e la lettura di `x`. Anche questa forma minima riproduce il problema in Tree/default e VM; la modalità token restituisce `2.25` e `3.75`.

La strumentazione temporanea mostra che `&x` viene calcolato come `&Fr->LocalVal[0]`, mentre `p` è nel successivo slot locale e inizialmente contiene l'indirizzo di `x`. Questo esclude l'ipotesi che la differenza dipenda esclusivamente dalle chiamate `bump()`/`widen()`. Il difetto è nel percorso di store/lettura o nella coerenza fra `LocalVal` e il valore letto dall'identificatore; non è stata ancora provata la singola istruzione responsabile. Non è stata applicata una correzione speculativa.

## Verifiche

- Build GCC pulita: verificata in questo turno.
- `test_fp_modes.py`: esito registrato nel log incluso.
- Audit architetturale FP: esito registrato nel log incluso; le differenze di `sizeof`, binary32 e layout struct restano aperte.
- Suite completa e compilazione MSVC: non verificate in questo turno.

## Prossimo passo

Tracciare il valore effettivo caricato per `x` immediatamente dopo `IrStorePtrElem(TypeFP, &LocalVal[0], ...)` e distinguere memoria fisica, `Fr->LocalVal[]` e qualsiasi array locale/copia usata dal percorso Tree. Confrontare Tree e VM nello stesso punto; intervenire solo dopo aver identificato quale rappresentazione resta stale.
