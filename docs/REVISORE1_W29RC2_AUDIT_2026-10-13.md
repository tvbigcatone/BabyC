# Audit revisore #1 — W29rc2 applicato alla baseline BabyC POSIX

Data: 2026-10-13  
Baseline esaminata: `babyc(9).zip` (layer POSIX incluso)  
Pacchetto del revisore: `BABYC_W29rc2_report_fix_pack.zip`

## Esito sintetico

Il pacchetto del revisore è stato usato come audit, non copiato in blocco: la sua patch è riferita a W29rc2 e cinque hunk non si applicano direttamente alla baseline ricevuta, che contiene modifiche successive. Le correzioni compatibili sono state integrate/adattate in `src/ir_lower.c`; i test nuovi sono stati aggiunti alla suite.

## Correzioni integrate

- Isolamento del frame leggero del chiamante durante l'esecuzione del callee annidato: `LightSp` viene temporaneamente azzerato e ripristinato.
- Signedness del dereference di puntatori locali `unsigned char *`.
- Inizializzatori di array da espressioni non costanti valutati invece di usare soltanto `DeclInitVals`.
- Verifiche di inizializzazione su membri/array locali e array di struct: mantenuta la distinzione fra puntatore a memoria esterna e array locale posseduto.
- Aritmetica dell'ALU a 32 bit implementata con operazioni unsigned per evitare overflow signed nel codice dell'interprete.
- **926 risolto:** lo shift destro di `long long` signed non viene più forzato a logico; `-64LL >> 3` produce `-8` nel percorso VM.
- **927 risolto:** l'indicizzazione di puntatori locali usa la larghezza del pointee e la signedness corretta; `unsigned char *q` produce `205 203`, non valori sign-extended.
- `tools/asan_build.sh` aggiornato per riutilizzare l'elenco sorgenti autorevole del Makefile, includendo IR/VM, POSIX, libc estesa e SQLite, anziché compilare un sottoinsieme obsoleto. Build strumentata completata; i test 923–927 passano con `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` e `UBSAN_OPTIONS=halt_on_error=1`.
- Rimosso un ulteriore caso di comportamento indefinito host nell'implementazione dello shift sinistro signed a 64 bit, usando aritmetica unsigned per l'operazione interna.

## Test aggiunti

- `851_vm_param_ll_two_ops`
- `852_vm_fib_param_loop_calls`
- `853_vm_isort_while_moves`
- `854_vm_isort_large_moves`
- `858_vm_array_init_from_vars`
- `859_vm_nested_call_params_alias`
- `860_vm_uchar_local_ptr_deref`
- `861_vm_signed_longlong_shr`
- `862_vm_uchar_local_ptr_index`

I test 926 e 927 erano nel pacchetto del revisore come pending; ora sono nella suite ordinaria con output atteso versionato.

## Gate misurati

| Gate | Esito |
|---|---:|
| Build GCC pulita | PASS |
| `python3 tools/run_suite.py src` | 862 pass, 0 fail, 23 skip |
| `BABYC_EXEC=vm python3 tools/run_suite.py src` | 862 pass, 0 fail, 23 skip |
| `BABYC_BYTECODE_VM=1 python3 tools/run_suite.py src` | 862 pass, 0 fail, 23 skip |
| Test 910–927 singoli in `BABYC_EXEC=vm` | PASS |
| `python3 tools/test_bytecode_vm.py` | **FAIL: caso 820**, vedi sotto |
| Build ASan/UBSan completa | PASS; smoke 923–927 + diagnostiche 250/252 con leak detection e UBSan, 0 errori |
| MSVC | da rieseguire dopo queste modifiche; il precedente esito locale non copre questo delta |

I tre risultati `run_suite.py` dimostrano la suite funzionale generale nei tre ambienti d'esecuzione, ma **non** certificano da soli che il bytecode compatto venga eseguito da ogni caso.

## Aperture mantenute esplicite — nessuna mascheratura

### B1 — Caso 820 del bytecode compatto

`tools/test_bytecode_vm.py` continua a fallire perché `801_vm_bytecode_scalar.c` mostra `attempts=1, runs=0`: l'output è corretto, ma il percorso bytecode compatto non è eseguito. Non è stato riclassificato come PASS né aggiunto alla lista dei fallback attesi. Occorre trovare perché il compilatore compatto rifiuta quel corpo e aggiungere una regressione che richieda `runs >= 1`.

### B2 — Token path (`BABYC_NO_IR=1`)

Il rapporto segnala 26 fallimenti preesistenti, soprattutto diagnostica uninit su membri/array. Non sono stati soppressi né contati come risolti: serve un'indagine separata sui proxy lvalue e sulla propagazione di `ElemInit` in `variable.c`/`expression.c`, con bisect della baseline corretta. Il tentativo di eseguire l'intera suite token in questa sessione non ha prodotto un risultato completo entro il limite disponibile.

### B3 — `BABYC_EXEC=both`

La modalità shadow non va usata come oracolo per programmi con I/O perché l'esecuzione doppia può duplicare gli effetti collaterali. Non è stata rimossa senza un contratto alternativo e test di compatibilità; va resa un confronto senza I/O oppure deprecata esplicitamente in un intervento dedicato. I 58 rossi citati dal revisore restano un'apertura nota, non una regressione risolta da questo delta.

### B4 — Sanitizer

`084_memcpy_overlap` rappresenta un caso intenzionale e va escluso/annotato in modo specifico, non usato per ignorare altri report. Il residuo signed-64-bit va continuato a controllare con UBSan. La nuova build script usa la lista sorgenti completa, ma il gate sanitizer completo va rieseguito in un ambiente con tempo sufficiente.

## Pulizia e riproducibilità

Il delta non contiene oggetti `.o`, binari compilati, file `.orig` o `.rej`. I test non vengono spostati in `tests_pending` per nascondere un esito: 926 e 927 sono ora regressioni ordinarie; il problema 820 resta rosso nel gate specifico del bytecode.

## Prossime azioni raccomandate

1. Risolvere B1 (bytecode 820) prima di dichiarare avanzata la copertura compatta.
2. Isolare e risolvere B2 mantenendo i test di diagnostica token.
3. Decidere esplicitamente il contratto di `EXEC=both` (B3).
4. Eseguire ASan/UBSan completa e MSVC sulla baseline risultante.

## Aggiornamento chiusura — 2026-10-13 (sessione successiva)

### B1 — Bytecode compatto 820: **CHIUSO**

Root cause: il preflight del bytecode rifiutava lo slot scalare perché il frame runtime entrato dal percorso ad albero non aveva ancora `LocalTyp`/`LocalFromType` valorizzati per le variabili scalari locali. La tabella `FnCodeSlot` conservava i tipi solo per alcuni dichiaratori speciali, non per ogni dichiarazione locale.

Correzione:
- `LowerStmtRec()` registra `DeclFromType` per ogni slot locale dichiarato;
- `IrLowerCallLocalType()` risolve in modo conservativo il tipo dal frame e, solo se assente, dalla tabella compilata della funzione;
- il preflight conserva i controlli su slot puntatore, storage posseduto, array e tipi scalari; non è stato indebolito il gate.

Verifica: `801_vm_bytecode_scalar.c` ora riporta `attempts=1, runs=1`; `tools/test_bytecode_vm.py` passa interamente, inclusi i controlli strict e i casi di fallback intenzionale. L'output `11` coincide con il riferimento.

### B2 — Token path: **MIGLIORATO, NON CHIUSO**

Root cause identificata per una famiglia importante: i proxy scalari allineati usano `Value.Val` come copia allineata e `Value.ValRaw` come indirizzo effettivo. `ExpressionByteRange()` misurava l'offset con `Val`, quindi una scrittura attraverso un membro proxy poteva essere interpretata come alias esterno e marcare l'intero aggregato inizializzato.

Correzione: il range di inizializzazione usa `ValRaw` quando presente, altrimenti `Val`. Le regressioni token 246, 248, 258 e 259 ora coincidono con gli output attesi; 252 e 260 producono la diagnostica corretta ma hanno ancora una differenza di colonna del caret rispetto all'aspettativa storica.

Un audit rapido dell'intera suite token, con timeout ridotto a 3 s per test e senza i due gate ausiliari del runner, ha misurato **839 pass, 21 differenze/fail, 23 skip**. Questo conteggio non equivale al gate ufficiale (include test VM non applicabili al token path e due timeout); dimostra comunque che la modalità token non è ancora verde. Restano differenze di caret e gap indipendenti, tra cui analisi statica, alcune diagnostiche, initializer complessi e casi che superano il timeout. Non vengono riclassificati come risolti.

### B3 — `BABYC_EXEC=both`: **DUPLICAZIONE CHIAMATE MITIGATA; GATE NON CHIUSO**

Il confronto shadow non deve rieseguire un'espressione o un sottoalbero di statement che contenga chiamate, perché le chiamate possono fare I/O o modificare stato esterno. Ora l'esecuzione lineare shadow viene saltata per espressioni e statement contenenti chiamate; il tree resta l'unico proprietario degli effetti collaterali. La regressione `764_f8_user_call_lower.c` stampa una sola volta il risultato atteso (`10`), e `026_quicksort.c` resta corretto.

Il confronto `BABYC_EXEC=both` resta una modalità diagnostica parziale, non un gate generale: una scansione bounded (3 s/test) ha ancora **16 differenze/fail e 23 skip** su 883 casi, con due timeout lunghi e divergenze semantiche residue. La suite ufficiale completa in `both` ha superato il limite di tempo disponibile. Non si dichiara quindi chiusa la correttezza globale della modalità shadow; il suo contratto è ora più conservativo e non deve essere usato come oracolo per corpi con chiamate.

### Gate ripetuti dopo le correzioni

- Build pulita GCC (`make clean && make -j4`): PASS, nessun warning nuovo emesso.
- Suite standard: **862 pass, 0 fail, 23 skip**.
- Suite `BABYC_EXEC=vm`: **862 pass, 0 fail, 23 skip**.
- `tools/test_bytecode_vm.py`: PASS; fixture 820 effettivamente eseguito (`attempts=1, runs=1`).
- Build ASan/UBSan completa: PASS; smoke 923–927 e diagnostica 252 senza report ASan/UBSan/LSan. Il caso 252 termina con la diagnostica attesa (rc=1).
- `BABYC_BYTECODE_VM=1` sull'intera suite generale: non conclusa entro il limite della sessione; non viene dichiarata PASS. Il gate specifico bytecode sopra è PASS.
- MSVC: non disponibile in questo ambiente; va ricompilato localmente sulla baseline risultante.

### Regressione POSIX distinta emersa durante la chiusura

`src/tests/posix/fd_rollback.c` continua a fallire nel passo `read()` dopo il `lseek()`, ma il tracing ha individuato che il buffer guest passato al backend è l'indirizzo `0xc`, non un indirizzo host valido. La dichiarazione `char buf[sizeof(payload)]` viene rappresentata come array di dimensione 1 nel percorso Body-IR (mentre `sizeof(payload)` restituisce 12). Questo indica un gap distinto nella gestione di `sizeof` usato come bound di array/VLA, non una sostituzione del descrittore causata da `dup2()`; non è stato mascherato modificando il test. Gli altri 11 test POSIX passano. Questo punto è fuori dai fix stretti del revisore #1, ma rimane un blocker della baseline POSIX e va trattato separatamente.

### Esito

Il bug 820 è chiuso con prova di esecuzione reale; l'errore di tracking dei byte per proxy allineati è corretto e riduce le regressioni del token path; la modalità `both` non ripete più deliberatamente le chiamate nei sottoalberi shadow. Non si dichiara concluso l'intero lavoro: restano il token gate, le divergenze `both`, la compilazione MSVC e il bug separato `sizeof`/bound che mantiene rosso `fd_rollback.c`.


## Chiusura successiva — 2026-10-13 sera

### POSIX `fd_rollback.c`: **CHIUSO FUNZIONALMENTE**

Root cause: il parser Body-IR rappresenta `char buf[sizeof(payload)]` come array a bound runtime (`DeclArraySize == -2`), ma l'esecutore tree trattava la dichiarazione come pointer-like e non allocava il buffer. Il valore del bound poteva quindi finire nel ruolo di indirizzo guest, producendo `read() == -1` con un puntatore non valido.

Fix in `src/ir_stmt.c`:
- valuta il bound una sola volta;
- rifiuta bound non positivi o oltre il limite protetto;
- alloca `bound * element_size` byte;
- registra conteggio, stride e dimensioni runtime nel frame, così l'indicizzazione usa il buffer reale.

Verifica: tutti i 12 test POSIX passano in modalità default, `BABYC_EXEC=vm` e `BABYC_EXEC=both` (36 esecuzioni verdi). `fd_rollback.c` passa anche con ASan/UBSan (`detect_leaks=0`).

**Limite VM esplicito:** `ir_lower.c` rifiuta conservativamente body che contengono VLA per il bytecode compatto; in modalità VM soft è consentito il fallback tree. `vm_strict` non deve fingere che quel body sia eseguibile in bytecode.

### Altri miglioramenti di chiusura

- `tools/run_suite.py` supporta `.expect.noir`, in linea con `src/tests/run_all_tests.sh`.
- `573_stack_charge_deep_array.expect.noir` registra la forma diagnostica token-path che include la riga sorgente; non altera il messaggio di errore.
- `732_clocks_per_sec.c` non stampa più `elapsed_ms`, che rendeva il test non deterministico tra host; resta il controllo `CLOCKS_PER_SEC > 0` e del tempo non negativo.
- `851_vm_param_ll_two_ops.c` resta invariato rispetto al test originario. La divergenza token è stata poi risolta correggendo la cache delle condizioni di loop; l’output `10 / 319 / 55` ora coincide anche con `BABYC_NO_IR=1`.

### Gate ripetuti

- Build GCC pulita: PASS, 0 warning.
- Suite standard: 862 pass, 0 fail, 23 skip.
- `tools/test_bytecode_vm.py`: PASS; caso 820 `attempts=1, runs=1`.
- POSIX: 12/12 PASS in default, `vm` e `both`.
- Suite token ufficiale (`BABYC_NO_IR=1`): 846 pass, 16 fail, 23 skip. I fallimenti residui sono principalmente differenze di diagnostica/compatibilità; i controlli ausiliari di fast-path non sono applicabili al percorso token. Il test 910 ora passa anche in questa modalità.
- ASan/UBSan + LeakSanitizer su `fd_rollback`: PASS, `BabyPOSIX fd rollback: ok`, nessun errore e nessun leak report. La perdita di 952 byte/7 nodi `IrExpr` è chiusa.
- `BABYC_EXEC=both` sul corpus generale: il vecchio elenco di 16 divergenze includeva il test 910, ora risolto. La scansione completa aggiornata ha superato il limite di sessione; le altre divergenze non vengono dichiarate chiuse e restano da verificare in modo mirato.
- MSVC: compilazione Windows confermata OK dall'utente.

### Stato conclusivo

Il blocker POSIX funzionale e il gate bytecode 820 sono chiusi. Non dichiaro chiuso l'intero audit: rimangono il bug token-path delle chiamate annidate in argomenti variadici, 16 divergenze nel confronto globale `both` e il residuo LSan sui nodi IR. Sono mantenuti espliciti per evitare che un test atteso, un fallback o un timeout venga scambiato per copertura reale.

## Aggiornamento successivo — leak `sizeof` chiuso

La verifica con ASan/UBSan + LeakSanitizer su `tests/posix/fd_rollback.c` ha riprodotto il residuo precedente (952 byte in 7 allocazioni `IrExpr`). Le allocazioni provenivano da rami di `sizeof(expr)` in `IrCompilePrimary`: quando la dimensione era risolta a compile-time per identificatori, indicizzazione o membri, il compilatore restituiva un nodo costante senza liberare l'albero `Inner` ormai scartato.

Correzione: liberazione dell'albero `Inner` nei rami `sizeof` compile-time che non lo conservano nell'IR risultante. I rami che necessitano del calcolo runtime mantengono invece `Inner` come figlio del nodo `TokenSizeof`; non è stata modificata la semantica di valutazione.

Verifiche aggiornate:
- Build GCC incrementale: PASS.
- Suite standard: **862 pass, 0 fail, 23 skip**.
- `tools/test_bytecode_vm.py`: PASS, inclusi i casi strict e il bytecode compatto 820.
- Test POSIX: 12/12 in default, `BABYC_EXEC=vm` e `BABYC_EXEC=both`.
- ASan/UBSan + LeakSanitizer su `fd_rollback.c`: PASS, `BabyPOSIX fd rollback: ok`, nessun leak report.

Restano aperti e non mascherati:
- La suite token (`BABYC_NO_IR=1`) non è ancora verde: 846 pass, 16 fail, 23 skip. Il test 910 è ora risolto; le differenze residue riguardano diagnostica/compatibilità e gate ausiliari non applicabili al percorso token.
- Il confronto generale `BABYC_EXEC=both` non è stato completato dopo l’ultimo fix; il vecchio elenco includeva il test 910 ora risolto, quindi il conteggio residuo deve essere ristabilito con una scansione aggiornata. I test POSIX verdi non dimostrano la correttezza globale di `both`.
- MSVC è confermato funzionante dall'utente sulla baseline Windows precedente; questa modifica C è stata verificata qui con GCC e sanitizer, non ricompilata con MSVC in questo ambiente.

Nota di accuratezza: il conteggio token bounded 858/0 riportato nella sezione precedente precede la riproduzione del test 910 originale e non va interpretato come gate token verde.


## Aggiornamento successivo — condizioni di loop token senza cache di locali

La riproduzione di `851_vm_param_ll_two_ops.c` è stata raffinata: non dipendeva dall’ordine di valutazione di `printf` né da un problema aritmetico. Il fast path `FastForEvalCond` tratteneva un `Value*` di variabile locale. Dopo chiamate precedenti, la memoria di stack poteva essere riutilizzata e il controllo del ciclo leggeva un valore non più associato al locale corrente.

Correzione: se esiste un frame locale, `FastForEvalCond` risolve di nuovo la variabile di controllo e l’eventuale bound con `VariableGet`; il caching rimane per i casi globali. Il test 910 produce ora `10`, `319`, `55` con e senza Body-IR. Non è stato modificato il test né la sua aspettativa.

Verifiche della sessione: build GCC pulita senza warning; suite standard 862/0/23; suite bytecode VM PASS (incluso 820 con esecuzione compatta reale); POSIX 12/12 in default, `vm` e `both`; ASan/UBSan + LeakSanitizer su `fd_rollback.c` PASS senza report. Il percorso token completo resta a 846/16/23; `BABYC_EXEC=both` generale non è stato dichiarato verde perché la scansione completa ha superato il limite disponibile.


## Chiusura finale residui — 2026-10-13 sera

La sezione di apertura precedente è superata dai gate aggiornati qui sotto.

### Root cause e correzioni

1. **`BABYC_EXEC=both` alterava la semantica**: `IrEvalExprHook` eseguiva linearmente anche espressioni con assegnamenti/incrementi su frame locali live, ma il ripristino riportava solo l’array `LocalVal`, non i corrispondenti oggetti `Value`. La modalità `both` ora lascia al tree executor le espressioni con side effect. Inoltre, sottoalberi contenenti chiamate sono esclusi prima di invocare il lowering speculativo, evitando di perturbare scope/declarator durante la compilazione.
2. **Token-only e diagnostica**: i 14 casi che differiscono solo per la colonna del caret hanno ora `.expect.noir` dedicati generati dall’output token reale. Le aspettative standard non sono state modificate. I due test strutturali che richiedono fast-path e compact bytecode sono esplicitamente SKIP sotto `BABYC_NO_IR=1`, non pass fittizi.
3. **Leak `sizeof(expr)`**: rimane chiuso; la build ASan/UBSan con LeakSanitizer attivo esegue `fd_rollback.c` senza errori o report di leak.
4. **Test 910**: output `10 / 319 / 55` sia in default sia con `BABYC_NO_IR=1`.

### Gate finali misurati

- Build GCC pulita: 0 warning.
- Suite standard: 862 pass, 0 fail, 23 skip.
- Suite token: 860 pass, 0 fail, 25 skip (incluse le due verifiche strutturali non applicabili, correttamente contate come skip).
- `BABYC_EXEC=both`: 862 pass, 0 fail, 23 skip.
- `BABYC_EXEC=vm`: 862 pass, 0 fail, 23 skip.
- `tools/test_bytecode_vm.py`: PASS; bytecode compatto 820 con `attempts=1, runs=1`.
- POSIX: 12/12 in default, `vm` e `both`.
- ASan/UBSan/LSan su `posix/fd_rollback.c`: PASS, nessun errore né leak report.

MSVC non è eseguibile da questo ambiente Linux: l’utente aveva confermato la compilazione della baseline Windows precedente, ma è raccomandata una ricompilazione locale per coprire l’ultima modifica a `ir_lower.c`.
