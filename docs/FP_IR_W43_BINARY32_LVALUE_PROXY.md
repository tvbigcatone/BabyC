# W43 — proxy distinto per lvalue binary32

## Obiettivo

Preparare il confine tra l'oggetto C `float` (4 byte) e lo slot scalare dell'interprete (almeno `sizeof(double)`) senza attivare ancora `TokenFloatType -> FloatType`.

## Modifica

`VariableAllocValueFromType()` ora predispone, per un lvalue di tipo FP binary32, una regione dati con due parti: lo storage oggetto di `sizeof(float)` byte e uno slot temporaneo allineato per `union AnyValue`. `ValRaw` identifica lo storage C; `Val` punta allo slot di esecuzione. Le funzioni esistenti `VariableValueRefresh()` e `VariableValueCommit()` effettuano la conversione nei percorsi che già gestiscono i proxy.

Non viene inizializzato artificialmente lo slot temporaneo: inizializzazione dell'oggetto e diagnostica degli accessi non inizializzati restano responsabilità dei percorsi esistenti.

## Limite importante

La modifica è preparatoria e resta inattiva con i tipi sorgente correnti, perché `float` continua a essere mappato al descrittore legacy binary64. L'esperimento temporaneo che attiva il mapping a `FloatType` continua a fallire `881_fp_user_func_return.c` in modalità token (`fl=1.00`, atteso `3.50`). Perciò W43 **non** attiva quel mapping e non dichiara completata l'architettura binary32. Prima di abilitarlo va corretta la propagazione di parametri/ritorni e verificato il round-trip di ogni lvalue.

## Verifiche

- Build pulita GCC: PASS, zero warning/error.
- Gate FP: 28/28 PASS con il mapping storico non modificato.
- Probe architetturale: 0/4 modalità corrispondenti a GCC (problema aperto).
- Test expected invariati.
