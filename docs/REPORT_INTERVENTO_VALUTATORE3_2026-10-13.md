# BabyC — intervento sulle osservazioni del valutatore 3

Data: 2026-10-13. Baseline di lavoro: `babyc(10).zip` integrata con le correzioni cumulative del valutatore 2. Le correzioni W37 sono state portate selettivamente: il pacchetto del valutatore 3 deriva da una versione precedente e alcuni file, in particolare `ir_lower.c`, non sono compatibili con una sostituzione integrale.

## Sintesi

- Corretti i difetti silenziosi nei risultati FP: ritorni di funzioni utente, risultati di intrinseci matematici, indicizzazione/dereferenziazione di `double*`, conversioni int→double nei `return` e assegnamenti tra scalari e array FP.
- Impedito ai kernel integer, alle scorciatoie leaf/thin-kernel e ai percorsi mono/memo di trattare come interi funzioni o statement che usano `float`/`double`.
- Ripristinate le tabelle del chiamante dopo chiamate utente annidate nel bytecode, mantenendo il fix cumulativo del test 882.
- Corretto un falso positivo in `BabycHeapCheckWrite`: il controllo one-past-end non deve precedere la ricerca di un altro blocco allocato che inizia esattamente a quell'indirizzo.
- Rafforzati i gate: differenziale FP in quattro modalità, bytecode coverage gate con `attempts >= 1` / `runs >= 1` quando è atteso il percorso compatto, e manifest rigenerabile.
- Tutti i test nuovi e trasferiti sono sotto `src/tests/`. Non viene introdotta alcuna cartella `new_tests/`.

## Dettaglio delle correzioni

### FP / IR

1. `ir_compile.c`: risoluzione del tipo di ritorno delle chiamate utente anche quando `CachedFunc` è assente; annotazione FP di elementi da puntatori/array e dereferenziazioni di tipo `double`; conversione del valore restituito quando la funzione ha ritorno FP.
2. `ir_runtime.h`, `ir_call.c`, `ir_eval.c`: `IR_RET_BITS` conserva i bit IEEE del risultato FP quando un valore attraversa il confine di chiamata; gli intrinseci non troncano più il risultato tramite `IrValueAsInt64`.
3. `ir_eval.c`: conversioni usuali int↔FP sugli store negli elementi degli array e nei cast verso `TypeFP`.
4. `ir_fast.c`, `ir_lower.c`, `ir_call.c`, `ir.h`: classificazione per statement dei tocchi FP; i kernel integer e i percorsi specializzati sono esclusi per statement/funzioni FP.
5. `parse.c`: il tipo di ritorno della funzione viene passato al compilatore IR, così `return n;` in una funzione `double` non restituisce i bit dell'intero.

### Bytecode e diagnostica memoria

- `ir_lower.c`: dopo una chiamata utente annidata, reinstallazione delle tabelle del chiamante se il body chiamato ha installato tabelle proprie.
- `cstdlib/stdlib.c`: `BabycHeapCheckWrite` ora fa prima la ricerca di un blocco che contiene l'indirizzo e solo dopo controlla i puntatori one-past. Evita falsi errori quando due allocazioni vive sono adiacenti senza indebolire il controllo delle scritture fuori limite.

### Tooling e documentazione

- Aggiornati i percorsi benchmark e il gate segnala i file mancanti invece di saltarli silenziosamente.
- `run_suite.py` include `test_fp_modes.py` e `test_bytecode_vm.py`.
- `test_bytecode_vm.py` esegue ogni gruppo in un processo separato, riporta tutti i fallimenti e richiede copertura bytecode effettiva.
- `tools/gen_manifest.py` rigenera `MANIFEST_SHA256.txt`; documentazione shape-FP allineata al comportamento del codice.

## Verifiche eseguite

| Controllo | Risultato |
|---|---:|
| Compilazione GCC (`-Wall -O2`) | OK, nessun warning nel log di build |
| Suite Tree | 886 PASS, 0 FAIL, 23 SKIP |
| Suite VM lineare | 886 PASS, 0 FAIL, 23 SKIP |
| Suite VM bytecode | 886 PASS, 0 FAIL, 23 SKIP |
| Test differenziale FP (`test_fp_modes.py`) | 28/28 |
| Gate bytecode (`test_bytecode_vm.py`) | 29/29 gruppi |
| `perf_gate.py --runs 1` | OK, nessun benchmark mancante |
| ASan/UBSan mirati | 45 esecuzioni, 0 diagnostiche; `detect_leaks=0` |
| Verifica `MANIFEST_SHA256.txt` | OK, 0 hash non corrispondenti |

Le 45 esecuzioni ASan/UBSan comprendono i sette test FP 950–956, i casi 882/930/931/945/949, due riproduttori di accesso a struct/lista e il nuovo test 957, nelle tre modalità. Leak detection è disabilitata: non si dichiara una verifica completa dei leak né una prova sanitizer sull'intera suite.

## Residui / limiti

- Build MSVC/Windows non eseguita in questo ambiente.
- Il supporto FP dell'IR resta strutturalmente parziale; il differenziale token/Tree/VM rimane parte obbligatoria del gate per future modifiche.
- Il performance gate è stato eseguito con una sola run: è un controllo di disponibilità e regressione grossolana, non una misura statistica.
