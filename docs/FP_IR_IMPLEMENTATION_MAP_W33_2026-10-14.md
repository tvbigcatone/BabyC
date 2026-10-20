# FP IR — mappa d'impatto per l'implementazione W33

## Esito del passaggio

Questo passaggio non introduce una modifica semantica al runtime: evita deliberatamente un altro split parziale del tipo sorgente. La build pulita GCC è riuscita senza warning; il gate FP storico resta 28/28. Il probe differenziale architetturale resta rosso (0/4 modalità conformi), quindi il difetto `float`/`double` è ancora aperto.

## Inventario delle dipendenze

Una ricerca statica sul codice attuale individua 98 occorrenze di `TypeFP` in 15 file. Non sono tutte bug autonomi, ma mostrano che lo split non può essere limitato a `type.c`:

| Sottosistema | File | Occorrenze `TypeFP` | Responsabilità da preservare |
|---|---|---:|---|
| Tipi e valori base | `type.c`, `interpreter.h`, `expression.c` | 9 | tipo sorgente, conversioni, promozioni, dimensione e valore |
| Compilazione IR | `ir_compile.c` | 26 | inferenza del tipo, costanti, puntatori, accessi, return e inizializzatori |
| Valutazione IR | `ir_eval.c` | 14 | valutazione Tree/IR, conversioni e load/store |
| Esecuzione e statement | `ir_exec.c`, `ir_stmt.c` | 13 | assegnazioni, dichiarazioni, valori globali/locali |
| Chiamate e frame | `ir_call.c` | 6 | argomenti, frame locali, parametri e ritorni |
| Fast path / dispatch | `ir_fast.c`, `ir_ops.c` | 10 | ammissibilità dei kernel, classificazione numerica |
| Lowering / memoria / VM | `ir_lower.c`, `ir_mem.c` | 16 | larghezza valori, stride, layout, operazioni e rappresentazione runtime |
| Librerie e runtime | `clibrary.c`, `cstdlib/stdio.c`, `ir_runtime.h` | 4 | interoperabilità e formattazione varargs |

Il conteggio è un inventario di simboli, non una stima di percentuale di completamento. I controlli su `sizeof(double)` e `IR_VW_I64` richiedono un audit separato perché non sono catturati dalla ricerca `TypeFP`.

## Sequenza d'implementazione vincolante

1. **Contratto del valore FP:** decidere una rappresentazione esplicita (precisione binaria e payload) che non confonda un valore binary32 promosso a `double` con un valore sorgente `double`. Evitare union/bitcast ambigui e definire conversioni centralizzate.
2. **Tipo e layout:** aggiungere `TypeFloat` e `FloatType` solo insieme a helper centrali `TypeIsFP`, `TypeFPWidth`, `TypeFPSize`, `TypeFPAlign` e aggiornare tutti gli switch sui tipi. `sizeof(float)` deve essere 4 sul target ABI previsto, indipendentemente dalla dimensione del contenitore usato dal runtime.
3. **Lvalue e memoria:** usare la dimensione del tipo effettivo per globali, locali, array, membri, pointer arithmetic e inizializzatori; non usare `sizeof(double)` come proxy per ogni `TypeFP`.
4. **Semantica e conversioni:** arrotondare a binary32 dove C richiede conversione a `float`; rispettare promozioni aritmetiche, conversioni implicite/esplicite e assegnazioni composte.
5. **IR e VM:** codificare precisione e larghezza nel valore IR e in ogni istruzione pertinente. Aggiornare verifier, serializzazione/compact bytecode, chiamate e return in modo atomico.
6. **Token e librerie:** allineare il percorso senza IR e le funzioni varargs (promozione predefinita di `float` a `double` nelle chiamate variadiche).
7. **Gate incrementali:** per ogni livello eseguire build senza warning, 28 test FP, probe GCC, suite standard e test specifici di layout; abilitare il probe come gate obbligatorio solo quando tutte le modalità sono conformi.

## Gate attuali riprodotti

- Build: `cd src && make clean && make -j2` — PASS, 0 warning osservati nel log.
- FP legacy: `python3 tools/test_fp_modes.py --babyc src/babyc` — 28/28 PASS.
- Probe architetturale: `python3 tools/audit_fp_ir_arch.py --babyc src/babyc` — FAIL atteso, 0/4 modalità conformi.

Non viene dichiarato alcun completamento FP né alcun miglioramento prestazionale in questo delta.
