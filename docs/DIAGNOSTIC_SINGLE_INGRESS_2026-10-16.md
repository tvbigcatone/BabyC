# BabyC — ingresso diagnostico unico e audit performance (2026-10-16)

## Decisione architetturale

I percorsi che dispongono di una posizione sorgente esplicita possono consegnarla a `ProgramFailAt()` / `DiagnosticReportV()` in `diagnostic.c`. `ProgramFail()` resta l'adattatore per i call site legacy. L'ingresso comune è una base architetturale, ma la migrazione non è completa: non tutti i motori consegnano ancora una posizione equivalente e alcune categorie di errore richiedono ancora lavoro.

## Vincolo VM compatta

- Nessun campo è stato aggiunto a `IrLinInst` (`ir_lin.h` è identico al controllo precedente).
- Nessun opcode o marker diagnostico aggiunto.
- `IrExpr` già conteneva `SourceLine` e `CharacterPos`: le modifiche valorizzano campi esistenti, senza aumentare la dimensione del nodo.
- Il resolver di posizione è invocato soltanto sul percorso d'errore; il ciclo compatto di esecuzione riuscita non consulta la diagnostica.
- Le colonne vengono acquisite durante la compilazione/analisi, non ricostruite per ogni istruzione eseguita.

## Performance — misure riproducibili

Build pulite GCC `-O2` della baseline, del controllo cumulativo precedente alla diagnostica e del candidato. Ogni esecuzione compatta ha verificato l'output `200000 700000` o, per il ciclo allungato, `10000000 35000000`.

| Misura | Baseline | Controllo pre-diagnostica | Candidato | Candidato vs controllo |
|---|---:|---:|---:|---:|
| `849_vm_compact_loop_bench.c`, 200k iterazioni, mediana di 18 run | 14,249 ms | 14,797 ms | 15,897 ms | +7,43% |
| `--analyze` sullo stesso sorgente, mediana di 13 run | 1,207 ms | 1,194 ms | 1,200 ms | +0,49% |
| Ciclo temporaneo di 10M iterazioni, mediana di 13 run | 830,865 ms | 872,112 ms | 864,757 ms | -0,84% |

Il benchmark corto end-to-end è rumoroso e mostra una differenza del +7,43% fra candidato e controllo; non è corretto attribuirla automaticamente al resolver, dato che la misura combina avvio, compilazione ed esecuzione e i campioni sono dispersi. La misura lunga, che enfatizza l'esecuzione, non mostra una regressione misurabile rispetto al controllo; l'analisi-only non mostra un incremento misurabile significativo. Questi dati supportano l'assenza di un costo aggiuntivo per istruzione, ma **non dimostrano un costo matematicamente nullo**. Prima di ulteriori modifiche prestazionali serve ripetere il benchmark su un runner stabile e registrare il profilo completo.

## Diagnostiche e limiti — stato verificato da build pulita

- `545_ir_struct_array_uninit.c`: caret `8:23` identico in Tree, VM e VM strict.
- `263_nested_struct_array_uninit.c`: caret `10:27` identico nei tre motori.
- `922_nested_struct_array_uninit.c`: caret `9:27` identico nei tre motori.
- **`253_array_struct_member_uninit.c` non è risolto in VM/VM strict:** Tree segnala `7:25`, mentre VM e VM strict terminano con successo stampando `0`, senza diagnostica. Non è una semplice discrepanza di caret: è un problema di rilevamento dell'inizializzazione nel percorso VM e resta aperto. Le precedenti note che lo indicavano risolto non sono confermate dalla ricostruzione pulita e vanno considerate superate.
- Gate bytecode struct-array: 7/7 superati con esecuzione compatta reale (`runs > 0`); `BT_ONLY=extras` superato.
- Build pulita GCC: superata senza warning/errori.
- Suite completa non terminata; MSVC/Windows non verificato in questa tranche.

## Conclusione

La struttura introdotta non allarga le istruzioni compatte né aggiunge lavoro al ciclo di esecuzione riuscita. Le misure non mostrano una regressione nel benchmark lungo, ma non autorizzano a dichiarare costo zero in senso assoluto. La migrazione diagnostica non è completa finché il caso 248 non viene rilevato correttamente dalla VM senza introdurre bookkeeping per istruzione.
