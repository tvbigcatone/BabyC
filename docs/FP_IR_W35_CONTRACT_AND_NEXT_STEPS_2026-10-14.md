# BabyC — FP IR W35: contratto e piano d'implementazione

Data: 2026-10-14

## Stato

W35 non modifica il runtime. La baseline W34 è stata estratta, ricompilata da zero e usata per rieseguire i gate prima di preparare questo delta. La distinzione `float`/`double` rimane un difetto aperto; questo pacchetto non va interpretato come fix FP.

## Evidenze verificate

- `struct ValueType` espone `Base`, `Sizeof` e `AlignBytes`, ma non codifica la precisione FP.
- `TypeParseFront` associa `TokenFloatType` e `TokenDoubleType` allo stesso `pc->FPType`.
- `pc->FPType` è inizializzato con `TypeFP` e `sizeof(double)`.
- La ricerca sul sorgente individua 34 occorrenze letterali di `sizeof(double)` nei file C della directory `src` (conteggio da riprodurre con `grep -R`). Non tutte sono automaticamente bug: alcune sono necessarie per il tipo double, altre richiedono audit del contesto.
- Il probe ora stampa le differenze per sezione: size/layout, arrotondamento binary32, chiamate/ritorni, valori degli array e layout struct. Questo rende la diagnosi più leggibile senza trasformare i mismatch in successi.

## Contratto FP richiesto

Un tipo FP deve determinare separatamente:

1. **Formato semantico**: binary32 oppure binary64.
2. **Storage**: dimensione e allineamento dell'oggetto.
3. **Valore di esecuzione**: rappresentazione numerica temporanea; non deve essere confusa con lo storage dell'oggetto.
4. **Conversioni ai confini**: conversione/arrotondamento su store, cast, assegnazione, ritorno e promozione variadica.
5. **Operazioni IR**: formato dell'operando e del risultato, incluso il comportamento delle conversioni esplicite.

`Sizeof` da solo non basta a determinare il formato semantico, e `TypeFP` da solo non basta a determinare lo storage.

## Sequenza di lavoro

1. Introdurre descrittori/helper centrali per formato e storage senza cambiare ancora il parser.
2. Inventariare e sostituire solo le assunzioni di storage nei percorsi load/store, allocazione locale, array e layout aggregati, distinguendo i casi double legittimi.
3. Aggiungere il tipo binary32 nel modello `ValueType` e instradare i token float/double in modo distinto.
4. Propagare il formato attraverso IR, conversioni, chiamate e ritorni.
5. Portare la semantica nel percorso token, Tree, VM lineare e VM compatta.
6. Promuovere il probe per sezioni a gate di accettazione solo quando tutti i percorsi lo superano; fino ad allora resta un audit volutamente rosso.

## Criteri di accettazione

- `sizeof(float)==4`, `sizeof(double)==8`, array e struct con layout conforme all'ABI host.
- Arrotondamento binary32 corretto anche attraverso assegnazioni e chiamate.
- Coerenza dei risultati fra token, Tree, VM lineare e compatta.
- Gate FP esistente 28/28 o superiore e suite standard senza nuove regressioni.
- Test compatti devono dimostrare esecuzione effettiva della VM compatta, non solo abilitazione del flag.

## Risultati W35

La build GCC pulita della baseline ha esito positivo. Il gate FP esistente e il probe architetturale sono registrati nei log allegati. Non sono stati modificati i sorgenti del runtime e non viene rivendicata alcuna correzione semantica in questo delta.
