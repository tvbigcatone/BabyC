# Audit anti-benchmark-tuning — 2026-10-17

## Risultato dell'ispezione

Nella baseline esistevano ottimizzazioni che non confrontano i nomi dei programmi, ma riconoscono idiomi algoritmici ad alto livello nell'AST. Questo è comunque troppo vicino al rischio indicato dall'utente: un benchmark può essere accelerato perché la sua forma coincide con il matcher, anche senza un confronto esplicito del nome del test.

### Politica aggiornata dopo chiarimento dell'utente

Non si deve confondere un'accelerazione di un idioma comune con un trucco basato sul nome o sul contesto del benchmark. `IrTryFastPtrScanPred` viene abilitato quando `BABYC_ACCEL=1`: riconosce una forma comune di scansione di caratteri e deve restituire lo stesso risultato del percorso generale. `IrTryFastInsertShift` viene anch'esso governato dal medesimo switch ACCEL; è un pattern più ristretto, perciò resta esplicitamente sottoposto a test differenziali e revisione di generalità. Nessuno dei due legge il nome del benchmark né sceglie il percorso in base alla modalità di misura.

`IrTryScoreCallAccum` / `IrTryLeafPtrStructAccum` sono stati riattivati sotto `BABYC_ACCEL=1` dopo la precisazione dell'utente: non sono un dispatch basato sul nome del benchmark, ma un matcher AST specifico per l'idioma struct/callback. Il runner deve misurare tale effetto nella colonna accelerata e non confonderlo con il motore non accelerato. La sua generalità rimane una questione di revisione tecnica, non una ragione per bypassare il master switch.

`IrShapeFpEnabled()` segue ora `BabycAccelOn()`. Le misure della baseline senza accelerazioni devono usare `BABYC_ACCEL=0`, che disabilita sia questi pattern sia i kernel IR generali. La suite verifica output identico a GCC in entrambe le configurazioni e un hit del matcher di scansione solo quando ACCEL è attivo.

### Cosa NON conclude questo audit

- Non prova che ogni ottimizzazione storica sia stata concepita per ingannare i benchmark: non è possibile inferire l'intento dal codice soltanto.
- Non è stato trovato nei file sorgente un confronto esplicito con i nomi `tak`, `qsort` o `heapsort` per scegliere il percorso.
- Restano kernel AST/loop strutturali (per esempio array accumulation/fill, pointer walk, matmul e riscritture di ricorsione pura). Tutti seguono `BABYC_ACCEL`; le misure non accelerate li disattivano con `ACCEL=0`, senza interruttori per-feature.
- Le misure storiche con `BABYC_NO_KERNEL=1 BABYC_NO_SHAPE_FP=1 BABYC_NO_MEMO=1` descrivono una revisione intermedia e non sono l'interfaccia corrente. Ora le colonne senza accelerazioni usano esclusivamente `BABYC_ACCEL=0`.

## Regola per il lavoro successivo

Niente dispatch basato su nomi o su una forma completa di benchmark/algoritmo. Le ottimizzazioni nuove devono operare su primitive IR/VM generali o su invarianti semantici verificati, e vanno provate su un corpus più ampio con A/B alternato. Se una vecchia ottimizzazione non supera questo criterio, va disattivata o sostituita da una trasformazione generale.

## Misure dopo la disattivazione

Build GCC `-O2`, esecuzioni alternate old/new (3 run per versione, `BABYC_EXEC=vm`, `BABYC_NO_MEMO=1`, kernel prodotto attivi), mediane in secondi:

| Caso | Matcher attivo nella vecchia revisione | Nuova revisione | Output |
|---|---:|---:|---|
| `micro.isort` | 0,0077 | 0,3466 | identico |
| `micro.string_scan` | 0,0150 | 0,3953 | identico |
| `micro.struct_score` | 0,0021 | 0,1402 | identico |
| `real.i06_tak` | 0,8395 | 0,8130 | identico |
| `real.a02_qsort` | 0,2308 | 0,2307 | identico |
| `real.a03_heapsort` | 0,5102 | 0,5203 | identico |
| `stress.call_loop` | 0,7990 | 0,7636 | identico |

Questi rallentamenti misuravano il costo dei matcher disattivati nella revisione intermedia. Dopo il chiarimento, le accelerazioni vengono riabilitate sotto `BABYC_ACCEL=1`; per misurare il motore non accelerato si usa `BABYC_ACCEL=0`. Il matcher struct/callback è anch'esso governato da ACCEL e resta da misurare nella matrice completa, non viene disattivato incondizionatamente.

Nella misura storica con kernel, matcher e memo disattivati (`BABYC_NO_KERNEL=1 BABYC_NO_SHAPE_FP=1 BABYC_NO_MEMO=1`), A/B non mostrava una variazione significativa e coerente su `tak`, `qsort`, `heapsort` e `call_loop` nelle 3 ripetizioni; per `heapsort` il nuovo valore mediano è circa il 2% più lento, entro la variabilità di questi campioni. Non rivendico quindi uno speedup del percorso chiamate in questa tranche.

## Profilo generale di heapsort

Profilo `gprof` della VM lineare senza kernel/matcher/memo (strumentato, quindi non usato per confrontare tempi):

- `IrExecLinLocals`: 52,5% del tempo campionato, 199.999 chiamate;
- `LinTryParamIndexedAccess`: 18,6%, circa 9,58 milioni di chiamate;
- `IrStorePtrElem`: 5,1%, circa 2,55 milioni;
- `LinExecIndexCopy`: 3,4%, circa 9,34 milioni;
- `IrStorageDescSeedFrameFromFnSlot`: circa 200.000 chiamate.

Questo profilo non conferma che lo snapshot/restore sia da solo la causa principale nell'attuale revisione. Il bersaglio generalizzabile per la prossima tranche è il costo del dispatch e dell'accesso indicizzato tipizzato nella VM lineare; la prossima ottimizzazione dovrà conservare i controlli di tipo, ownership e inizializzazione, e dimostrare miglioramenti su più programmi.

### Esperimento non mantenuto: snapshot locali a extent limitato

Ho provato a ridurre il rollback per chiamata da 128 slot all'estensione derivata dagli opcode. Il confronto A/B non ha mostrato un guadagno ripetibile su `heapsort`/`call_loop` e in alcuni campioni è peggiorato; ho quindi scartato la modifica prima del pacchetto finale. Non è inclusa nel delta consegnato.

## Verifiche di questa revisione

- Build GCC `make clean && make -j2`: zero warning.
- Suite sorgente a tranche da 100: Tree 973/973. La suite Tree non certifica la copertura completa Linear/Bytecode.
- `tools/test_string_scan_fastpath.py`: verifica output uguale a GCC, hit della scansione con ACCEL=1 e zero hit con ACCEL=0.
- `tools/test_sparse_local_snapshot.py`: PASS; Linear/Bytecode e `FALLBACK=0`, con esecuzione compact confermata.
- `tools/test_fp_modes.py`: 32/32.
- `tools/test_bytecode_vm.py`: PASS, 53/53 gruppi, completati in tranches indipendenti per evitare timeout globali.
