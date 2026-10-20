# Unificazione del catalogo dei messaggi diagnostici — 2026-10-17

## Obiettivo

Rendere verificabile la regola richiesta: le stringhe radice dei messaggi semantici condivisi devono essere definite in `src/diagnostic.c`, non replicate nei motori. Il renderer comune non può uniformare da solo le stringhe che gli vengono passate; il catalogo centralizza il vocabolario e il controllo statico impedisce regressioni per le radici incluse.

## Modifica

Aggiunto in `src/diagnostic.c` il catalogo dei messaggi condivisi, dichiarato in `src/interpreter.h`, e sostituiti i letterali nei percorsi Tree, IR, Linear, bytecode e nelle librerie runtime con i simboli condivisi:

- `NULL pointer dereference`
- `uninitialized variable` e `uninitialized variable '%s'`
- `division by zero` e `modulo by zero`
- `array index out of bounds` e `array index %d out of bounds (size %d)`
- `out of memory`
- `null function pointer`
- `function is undefined`

Anche `DiagnosticResolvedColumn()` usa il simbolo canonico per riconoscere il caso di variabile non inizializzata, invece di contenere una seconda copia della radice.

Le chiamate mantengono gli stessi formati e argomenti: non è stato introdotto lavoro sul percorso di successo; il cambiamento riguarda puntatori a stringhe statiche usati sui percorsi d'errore. Le forme contestuali diverse, ad esempio `out of memory (IR call frame)`, rimangono intenzionalmente distinte e non sono state forzate in un messaggio generico.

## Guardia anti-regressione

Aggiunto `tools/check_diagnostic_catalog.py`. Il controllo fallisce se uno dei dieci letterali canonici ricompare nei sorgenti runtime fuori da `src/diagnostic.c` (esclude test, codice vendorizzato e intestazioni di terze parti). Esito corrente:

`Diagnostic catalog OK: 10 canonical literals are confined to src/diagnostic.c`

Una ricerca supplementare delle radici `out of bounds`, `uninitialized variable`, `NULL pointer dereference`, `division by zero`, `modulo by zero`, `null function pointer` e `function is undefined` non trova letterali diagnostici duplicati nei sorgenti runtime escluso `diagnostic.c`.

## Verifiche

- Build GCC: riuscita.
- Corpus Tree/IR: 959/959, nessun timeout.
- Corpus token-only: 959/959, nessun timeout.
- Corpus IR shadow disattivato: 959/959, nessun timeout.
- Linear strict (`BABYC_PATH=linear BABYC_STRICT=1`): 906/959; 53 discrepanze/limiti di copertura già presenti nell'asse Linear strict. Questo risultato è riportato separatamente e non viene presentato come suite superata; il catalogo testuale non rende automaticamente equivalenti le semantiche dei motori.
- **Limite importante del confronto corpus:** il runner a tranche normalizza le righe del caret, quindi 959/959 non certifica l'uguaglianza esatta delle colonne. Il confronto puntuale ha rilevato che Linear strict stampa ancora il caret su `p` (colonna 21) nei test `1038` e `1039`, mentre Tree/token puntano alla lettura `p[0]`/`p[1]` (colonne 36/48). Il testo e il codice d'uscita coincidono, ma l'ancora di Linear resta da correggere alla sorgente. Non è stata mascherata nel renderer.

## Limiti espliciti

I fault di sistema dentro intrinsic restano un percorso distinto: il signal handler Unix deve usare solo operazioni async-signal-safe e terminare con `_exit`, quindi non può invocare il renderer generale. I gestori SEH/Windows hanno una limitazione analoga sul percorso eccezionale. Le tracce di debug/profilazione non sono diagnostiche semantiche del linguaggio e non sono state convertite.

Questo è il primo nucleo del catalogo, non la pretesa che ogni messaggio di BabyC sia già stato migrato. Il passo successivo è estendere lo stesso controllo a tutte le radici duplicate trovate dal grep, classificando prima le varianti semantiche e poi uniformando quelle equivalenti senza cancellare informazioni contestuali utili.
