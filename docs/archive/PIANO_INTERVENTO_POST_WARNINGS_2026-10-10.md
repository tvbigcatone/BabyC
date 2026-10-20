# Piano d’intervento BabyC — aggiornamento post warning cleanup

**Data:** 2026-10-10  
**Baseline:** `babyc(2).zip` fornito per questa revisione  
**Stato:** build e suite standard/duale verdi su Linux/GCC; validazione fuzz/performance completa ancora da eseguire.

## Obiettivo

Ridurre il rischio tecnico della baseline senza introdurre nuove funzionalità prima di aver consolidato correttezza, riproducibilità e prestazioni. Questo piano sostituisce le priorità operative precedenti per il prossimo ciclo, lasciando invariati gli obiettivi architetturali a lungo termine.

## Delta completato in questa revisione

1. Eliminati i warning GCC rilevati con la configurazione esistente del Makefile.
2. Corretti percorsi di gestione memoria in cui l’uso di `realloc` poteva invalidare puntatori prima della gestione dell’errore.
3. Corretto il blocco di caching in `ir_eval.c`, includendo nel controllo la memorizzazione dei metadati del nome funzione.
4. Inizializzati i registri temporanei di lowering e separati controlli di limite da `realloc`.
5. Aggiunta dichiarazione anticipata di `struct Value` e supporto a `TypeLongLong` nella stampa dei tipi.
6. Normalizzato a LF `src/tools/run_suite_dual.sh`, che prima falliva con `set: pipefail\r: invalid option name`.
7. Aggiornati `docs/HANDOFF.md` e `docs/CURRENT_STATUS.md`.

## Piano operativo aggiornato

### P0 — Baseline riproducibile (completato per Linux/GCC)

- [x] `make clean && make -j4`
- [x] Log GCC senza warning né errori con i flag del Makefile
- [x] Suite standard: 816 passati, 0 falliti, 21 saltati
- [x] Suite duale: A-IR, B-NO_IR, C-IR-SHADOW0 ciascuna 816/0
- [ ] Registrare versione esatta di GCC, OS e commit/hash sorgenti nei prossimi report
- [ ] Verificare build con `-Werror` e configurazione sanitizer

**Criterio d’uscita:** build riproducibile, nessun warning, test standard e duali verdi su ambiente dichiarato.

### P1 — Verifica della memoria e dei percorsi di errore

- [ ] Aggiungere o eseguire test per `realloc(NULL, n)`, `realloc(p, n)` con blocco tracciato, fallimento di allocazione e `realloc(p, 0)`.
- [ ] Eseguire ASan/UBSan sui test di regressione e sulla suite compatibile.
- [ ] Verificare che il tracciamento heap resti coerente dopo realloc riuscita, fallita e con spostamento del blocco.
- [ ] Riesaminare i percorsi di allocazione che possono restituire `NULL`, inclusi i chiamanti di `VariableAllocValueFromExistingData`.

**Criterio d’uscita:** nessun errore sanitizer nei casi supportati e semantica del tracciamento documentata, inclusi i limiti specifici di `realloc(p, 0)`.

### P2 — Correttezza differenziale IR

- [ ] Rieseguire il corpus di fuzz differenziale sulla baseline aggiornata con seed conservati.
- [ ] Confrontare tree/token, IR e GCC nei casi supportati.
- [ ] Convertire ogni discrepanza confermata in test di regressione.
- [ ] Pubblicare numero totale di casi, hard fail, mismatch soft e crash; non limitarsi al solo exit code della suite.

**Criterio d’uscita:** zero hard fail nel corpus di accettazione; ogni eccezione residua è riproducibile, documentata e approvata.

### P3 — Gate prestazionale

- [ ] Rieseguire il gate Callgrind/performance completo sulla stessa macchina e con ambiente pulito.
- [ ] Separare i risultati tree executor, IR lineare e VM.
- [ ] Indagare prima i benchmark storicamente fuori soglia (`structs`, `collatz`, `crc`) solo se risultano ancora fuori soglia nella nuova misurazione.
- [ ] Non modificare hot path senza baseline prima/dopo e test di correttezza.

**Criterio d’uscita:** tutti i benchmark entro soglia concordata oppure eccezioni esplicite, motivate e approvate.

### P4 — VM bytecode

- [ ] Mantenere il percorso corrente e l’esecutore ad albero come riferimento finché la VM non è completa.
- [ ] Specificare formato istruzioni, validazione, gestione errori, frame e convenzioni di chiamata.
- [ ] Implementare un sottoinsieme incrementale con fallback esplicito.
- [ ] Aggiungere confronti differenziali per ogni opcode e per chiamate annidate.
- [ ] Promuovere la VM solo dopo gate di correttezza, sanitizer e prestazioni verdi.

**Criterio d’uscita:** esecutore bytecode di produzione completo per il sottoinsieme dichiarato, con copertura test e fallback documentati.

### P5 — Documentazione e release

- [ ] Aggiornare HANDOFF e CURRENT_STATUS a ogni baseline verificata.
- [ ] Allegare i comandi esatti, l’ambiente e i conteggi test ai report.
- [ ] Distinguere chiaramente “test eseguiti” da “test riportati da documenti precedenti”.
- [ ] Preparare una checklist di release con build pulita, suite, fuzz, performance e note di compatibilità.

## Rischi e note

- “Zero warning” vale per la configurazione GCC/Makefile usata in questa revisione; flag differenti possono rivelare warning aggiuntivi.
- La suite standard e la suite duale verdi non dimostrano equivalenza completa per tutti i programmi C.
- La gestione `realloc(p, 0)` varia storicamente tra implementazioni C; evitare di usarla come fondamento di logica portabile senza una policy esplicita.
- Nessuna nuova funzionalità deve precedere la chiusura dei gate P1 e P2.

## Comandi di verifica usati

Da `babyc/src/`:

```sh
make clean
make -j4
python3 ../tools/run_suite.py
bash tools/run_suite_dual.sh
```

Risultati della revisione: build con zero warning/errori; suite standard 816 passati, 0 falliti, 21 saltati; suite duale A-IR 816/0, B-NO_IR 816/0, C-IR-SHADOW0 816/0.
