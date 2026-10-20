# W67 — audit della semantica float e decisione di non accettare una regressione

## Obiettivo

Continuare il lavoro FP partendo dal delta W66 e dall'unica baseline cumulativa `babyc(20261015-093958).zip`.

## Risultato concreto dell'indagine

L'audit `tools/audit_fp_ir_arch.py` conferma ancora un gap architetturale indipendente dai difetti di alias già corretti:

- `sizeof(float)` e le dimensioni degli array FP non coincidono con GCC nei percorsi IR;
- il rounding binary32 non è applicato coerentemente a tutte le operazioni;
- il layout di struct che contengono `float` non è ancora conforme.

È stata isolata una causa importante: `TypeParse()` mappa `TokenFloatType` e `TokenDoubleType` entrambi a `pc->FPType`, benché `pc->FloatType` esista con `FPStorageBinary32`.

## Esperimento e gate anti-regressione

Ho provato a distinguere i due tipi nel parser e a propagare la distinzione al compilatore IR locale. La build GCC riusciva, ma il gate differenziale scendeva da 32/32 a 22/32 (e il primo tentativo a 28/32). I fallimenti coinvolgevano ritorni FP nel percorso token e accessi/alias su array FP nei percorsi IR.

**La modifica sperimentale è stata quindi scartata e non è inclusa nel delta.** Non sarebbe corretto consegnare come fix una variazione che corregge un numero di `sizeof` ma rompe chiamate, alias e array. Il sorgente consegnato resta quello verificato in W66.

## Stato verificato della base consegnata

- Build GCC pulita: PASS.
- `tools/test_fp_modes.py`: 32/32 PASS sul sorgente W66.
- Audit architetturale FP: 0/4 modalità conformi; i mismatch elencati sopra restano aperti.
- Suite completa: tentativo avviato ma non completato entro il limite disponibile; non dichiarata PASS.
- MSVC: non verificato.

## Prossimo intervento tecnico

Separare esplicitamente tre concetti oggi accoppiati: dimensione dell'oggetto C, dimensione dello slot di esecuzione FP (double) e formato del valore (binary32/binary64). La distinzione deve arrivare ai frame locali, agli array/arena, ai caricamenti/store indiretti, ai parametri e ai valori di ritorno. Il gate minimo di accettazione resta `test_fp_modes.py` 32/32 più audit mirato; nessuna modifica verrà accettata se degrada il gate.
