# BabyC — punto fermo provvisorio prima della seconda valutazione

Data: 2026-10-17

## Scopo

Chiudere la tornata senza introdurre ulteriori modifiche al runtime prima delle considerazioni del secondo valutatore. Il presente documento distingue i risultati riprodotti in questa sessione dalle verifiche storiche e dai controlli ancora aperti.

## Baseline e composizione

- Baseline di lavoro: `babyc(20261017-074837).zip`.
- Applicato il delta cumulativo `BABYC_DELTA_CONSOLIDATION_BYTECODE_GATE_2026-10-17.zip`.
- Ricompilazione completa da zero con GCC `-O2`: PASS, nessun warning osservato.
- Nessuna modifica al runtime in questa tornata; questo report registra l'esito dei controlli e non aggiunge nuovi rami di esecuzione.

## Risultati riprodotti

- Suite C principale, eseguita con un runner diagnostico temporaneo che esclude esclusivamente il gate bytecode annidato: **963 PASS, 0 FAIL, 28 SKIP**.
- Test FP: **32/32 PASS**.
- `test_string_scan_fastpath.py`: PASS (differenziale nativo, hit del percorso e controllo senza shape).
- `test_sparse_local_snapshot.py`: PASS (Linear/compact/strict; esecuzione compatta confermata).
- Il runner diagnostico è stato creato solo nella copia di lavoro e rimosso al termine; `tools/run_suite.py` non è stato modificato.

## Diagnosi del mancato riepilogo aggregato

Il runner ufficiale `tools/run_suite.py` completa la scansione dei file C, poi esegue `tools/test_bytecode_vm.py` come sottoprocesso con timeout di 900 secondi. Una copia strumentata del runner ha attraversato 988 file e raggiunto il gate finale dopo 960 casi marcati PASS; non ha mostrato righe FAIL prima del timeout esterno. Il riepilogo ufficiale non è stato emesso perché il gate bytecode aggregato non ha restituito entro il limite.

La precedente verifica documentata ha eseguito separatamente i 53 gruppi del gate bytecode: **53/53 PASS** con timeout individuali. Questo risultato non equivale a un PASS del runner aggregato: la durata cumulativa o l'interazione fra gruppi resta da spiegare. Il punto aperto è quindi circoscritto al gate aggregato/alla sua durata, non a un generico blocco durante l'esecuzione dei test C.

## Performance e correttezza

Restano valide le misure A/B documentate in `ROOT_FIX_LIGHT_CALL_AND_FIB_PROFILE_2026-10-17.md`: il root fix delle chiamate leggere ha ridotto il tempo mediano di `fib_ptr` da circa 0,871 s a 0,261 s in condizioni controllate, con output invariato (`832040 2692537`). In questo turno non sono state eseguite nuove misure A/B; non viene rivendicato un ulteriore guadagno.

I controlli mirati di diagnostica, FP, snapshot e fast path già riportati nei documenti precedenti restano parte dell'evidenza cumulativa. Non è stata eseguita una build MSVC/Windows in questa tornata; tale verifica rimane aperta.

## Stato al punto di sosta

**Consolidato:** build GCC pulita; suite C principale separata dal gate aggregato 963/0/28; FP 32/32; fast path e snapshot PASS; gate bytecode individuale precedentemente 53/53; root fix ricorsione con miglioramento prestazionale misurato.

**Non dichiarato concluso:** runner bytecode aggregato; suite ufficiale completa nel suo insieme; verifica MSVC/Windows; ulteriori sanitizer completi.

## Prossimo passo dopo la seconda valutazione

Confrontare le osservazioni del valutatore con questo stato, risolvere prima eventuali contestazioni di correttezza o di metodo, poi affrontare la durata del gate aggregato senza allentare timeout, criteri di confronto o contatori di esecuzione e senza nascondere i casi problematici.
