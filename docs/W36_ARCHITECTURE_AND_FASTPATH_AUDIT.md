# W36 — snapshot transazionali più selettivi e audit di generalità

## Intervento architetturale

`IrVmExecProd` salvava l'intero frame locale (`NLocals` valori e flag di inizializzazione) prima di eseguire il bytecode, anche quando il flusso poteva modificare soltanto una piccola parte dei locali. Il backup completo è necessario come strategia di rollback, ma copiare slot immutati è lavoro evitabile per frame grandi e scritture sparse.

Il preflight ora raccoglie i locali scrivibili dal flusso (`StoreLocal`, `AddLocal`, `AddStoreLocal`) e sceglie una strategia generale:

- frame con meno di 16 locali: resta il percorso precedente, senza bitmap/lista e con copia completa;
- frame grandi ma con scritture dense: resta la copia completa;
- frame grandi con meno della metà degli slot scrivibili: salva e ripristina solo gli slot effettivamente scritti.

La raccolta avviene durante la validazione già necessaria del bytecode. Gli slot vengono registrati una sola volta; valori e flag `LocalInit` sono salvati prima dell'esecuzione e ripristinati nel rollback. Nessuna istruzione può modificare un locale che non compaia in una delle tre operazioni di scrittura ammesse dal preflight.

## Test e misure

Aggiunto `src/tests/850_vm_sparse_local_frame.c/.expect`, con frame grande e poche variabili effettivamente modificate. `tools/test_sparse_local_snapshot.py` verifica l'output in modalità lineare, bytecode compatto e `vm_strict`, e richiede che la modalità bytecode abbia realmente eseguito il kernel. Il test è integrato in `tools/run_suite.py`.

Confronto paired end-to-end fra il codice W35 e W36, output identico `200000 700000`:

| Caso | W35 mediana | W36 mediana | Delta indicativo |
|---|---:|---:|---:|
| Frame piccolo, benchmark `849_vm_compact_loop_bench` — bytecode | 15,564 ms | 15,422 ms | -0,9% |
| Frame grande/sparso, test `850_vm_sparse_local_frame` — bytecode | 17,454 ms | 16,731 ms | -4,1% |

I campioni hanno jitter significativo (inclusi outlier); questi valori sono segnali locali, non una dimostrazione di accelerazione generale. Il frame piccolo non mostra regressione mediana misurabile nel campione raccolto. Non è stata aggiunta alcuna soglia temporale alla suite.

## Audit di generalità — segnalazioni da decidere con l'utente

Non ho rimosso automaticamente gli interventi precedenti: sono ancora nel delta, ma questi due meritano una decisione esplicita alla luce della direttiva anti-benchmark-tuning.

### W34 `IrTryFastInsertShift` — **segnalazione rossa: molto specifico**

Il matcher dell'insertion sort ammette un corpo di ciclo fino a otto istruzioni, ma l'unica istruzione aggiuntiva tollerata è l'autoassegnamento `key = key`, controllato come identificatore locale identico. La modifica è stata motivata da `isort_v2.c`; il caso canonico `isort.c` era già veloce e il grande guadagno misurato era concentrato nella variante con decoy.

Questa condizione è sospettosamente vicina a un accomodamento per il benchmark, non a una regola generale di ottimizzazione. **Raccomandazione:** rimuovere l'eccezione all'autoassegnamento, salvo che l'utente decida di conservarla o che un'analisi più ampia dimostri che il matcher rappresenta una classe comune di cicli reali.

### W35 `IrTryFastPtrScanPred` — **segnalazione gialla: kernel semantico stretto**

Il kernel riconosce una scansione di puntatori a `char`, un predicato di conteggio basato su confronti di caratteri collegati da OR (fino a quattro caratteri), incremento del puntatore e accumulatore. Il limite `p < end` è una forma C comune, ma la fusione dell'intero predicato resta specifica. I risultati disponibili riguardano `string_proc.c` e `string_proc_v2.c`; non dimostrano ancora benefici estesi a un corpus più ampio.

**Raccomandazione:** mantenere temporaneamente per non alterare il comportamento senza decisione, ma prima di promuoverlo come ottimizzazione generale misurare più scansioni rappresentative e valutare una trasformazione IR generale che riduca il costo per iterazione senza riconoscere una precisa forma AST.

Queste segnalazioni non sono state occultate né risolte unilateralmente. L'utente deciderà se rimuovere i due fast path; nel frattempo il lavoro nuovo W36 è una strategia generale di backup transazionale, non un matcher di benchmark.

## Validazione

- Build pulita GCC (`make clean && make -j2`): PASS, zero warning.
- `python3 tools/run_suite.py`: **846 pass, 0 fail, 23 skip**; include i controlli anti-regressione scansione stringhe e frame locale sparso.
- `python3 tools/test_bytecode_vm.py`: PASS, compresi i controlli strict e i fallback diagnostici.
- Output lineare/bytecode/strict del caso W36: identico.

Il gate prestazionale globale del progetto rimane aperto.
