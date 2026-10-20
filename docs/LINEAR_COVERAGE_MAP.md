# BabyC — mappa della copertura sintattica/semantica Linear

> **Addendum Fase 2, turno 1 (2026-10-19): selezione senza Tree fallback.** La baseline storica e l'audit strutturale che seguono descrivono la fotografia iniziale; le modifiche di questo addendum sono lo stato più recente del codice.

## Addendum Fase 2: scelta del body e gate no-fallback

### Perché STRICT e non-STRICT sceglievano corpi diversi

La causa non era una differenza di correttezza del body né una lista di eccezioni per test. `IrVmStrictOn()` era usata in due ruoli diversi: policy (`non-STRICT può ripiegare su Tree`) e capability/eligibility (`questa forma ha accesso alle implementazioni Linear FP/member/index/pointer`). Il secondo ruolo trasformava in non eleggibili forme che la compilazione Linear sapeva già abbassare. Per separare i due ruoli, `src/ir_lower.c:880` espone `IrVmLinearFeaturesOn()`, che misura la disponibilità del percorso Linear di produzione indipendentemente dalla policy di fallback. I gate semantici in `src/ir_lin_emit.c` e i gate di preflight in `src/ir_lin_compile.c` usano questa funzione; `IrVmStrictOn()` resta per le decisioni che riguardano davvero la policy o una scelta d'ottimizzazione, non per disabilitare una capability supportata.

Questo spiega i repro dell'audit iniziale: `024_type_coercion` passava da `fncode_body=7/tree_body=3` non-STRICT a `fncode_body=10/tree_body=0` STRICT; `841_real_a08_text` eseguiva 6000 corpi Linear e un corpo Tree in non-STRICT pur compilando il body in STRICT. Dopo la correzione, entrambi passano per Linear anche senza STRICT.

### Ulteriori fallback individuati con il gate affidabile

Una prima misurazione aveva sottostimato i fallback: `IrPrintLowerStats()` (`src/ir_lower.c:2179-2190`) sopprimeva la riga `[babyc-cov]` quando statement, expression, instruction e `fncode_body` erano zero, anche se `tree_body` era positivo. La condizione ora include anche `BabycTreeBodyRuns == 0`, quindi un fallback solo-body non è più invisibile al runner.

Il gate ha poi esposto `623_multidim_3d` e `630_global_2d_index` solo con ACCEL 0. Il dump mostrava `lower_reject` nei rispettivi corpi: `s += a[i][j][k]` e `sum += A[i][k] * B[k][j]`. La causa condivisa era il const-trip unroll che abbassava ripetutamente gli stessi nodi AST contenenti `Index`; cache/metadati mutabili dell'indirizzamento non hanno un contratto generale di clonazione per ri-lowering. `src/ir_lin_compile.c:340-439` aggiunge una condizione conservativa (`IrConstUnrollStmtSafe`): questo unroll è permesso solo per corpi scalar-only con effetti circoscritti a locali/parametri. Esclude `Index`, `Member`, `Call`, address/deref e statement strutturali non validati; conserva il caso scalare `k++` di `978_const_local_increment`, inclusa la diagnostica alla riga 7. Non è una whitelist di funzioni/test e il loop generico continua ad essere abbassato normalmente.

Il kernel array-fill in `src/ir_fast.c:4577` non riesegue `InitStmt` mentre un body Linear è in esecuzione (`BabycLinExecDepth != 0`): il flusso Linear ha già emesso l'inizializzatore. La semantica Tree resta invariata quando il kernel è invocato da Tree. Senza questo controllo, `945_sibling_for_same_name` eseguiva due volte l'inizializzazione del loop.

### Gate permanente

`tools/run_numbered_matrix.py --path linear --accel {0,1} --require-no-tree-fallback` abilita `BABYC_COV=1` e fallisce ciascun caso che emetta `tree_body > 0`; il riepilogo contiene `TREE_FALLBACK_GATE` e `COVERAGE_TOTALS`. L'opzione è rifiutata per Tree o per `--strict`: il gate misura precisamente Linear non-STRICT. `IrPrintLowerStats()` deve mantenere visibile un eventuale `tree_body` anche quando tutti gli altri contatori sono zero.

È stato aggiunto `src/tests/1017_linear_non_strict_fp_calls.c/.expect` come copertura del percorso FP del selettore non-STRICT; non sono stati modificati gli `.expect` preesistenti. La matrice finale comprende 1009 test numerati e il gate è stato eseguito con entrambi i valori ACCEL; i riepiloghi sono nel report della Fase 2 e nei sei log `docs/reports/phase2_t1_verified_*.log`.

**Esito dinamico Fase 2 (snapshot verificata):** in Linear non-STRICT, `tree_body=0` su tutti i 1009 test con ACCEL 0 e 1. ACCEL 0: `tests_reported=995`, `lin_inst=365206505`, `tree_stmt=0`, `tree_expr=0`, `fncode_body=1023`, `tree_body=0`. ACCEL 1: `tests_reported=995`, `lin_inst=334507512`, `tree_stmt=0`, `tree_expr=0`, `fncode_body=1134`, `tree_body=0`. Con `BABYC_COV=1`, Linear STRICT verifica anch'esso `tree_body=0`, `tree_stmt=0`, `tree_expr=0` in entrambi gli ACCEL: `lin_inst=368303571` a ACCEL 0 e `337560194` a ACCEL 1; entrambi i percorsi 1009/1009. “tests_reported” è il numero di processi con una riga counter: test che terminano senza esecuzione di body possono legittimamente non stamparla; `tree_body` isolato è comunque reso visibile. Tutte e sei le matrici passano 1009/1009.

Il fixture `535_main_continue_after_extract` stampa una misura `clock()` che è variabile ma aveva un `.expect` fissato a `0 ms`. Per rendere la matrice deterministica senza toccare quell'aspettativa, `tools/run_numbered_matrix.py` normalizza esclusivamente la parte numerica del campo `Tempo esecuzione` per quel file. Non è una normalizzazione dell'output generale e non filtra alcuna riga `[babyc-cov]`: il no-fallback gate legge i contatori dalla stdout originale e continua a fallire se `tree_body > 0`.


**Audit statico del codice:** 2026-10-19  
**Baseline:** `babyc_baseline_2026-10-18_loop_promotion.zip` + delta cumulativo `babyc_cumulative_delta_2026-10-19_linear_126_modulo_closure.zip`, ricostruiti applicando `DELETE_FILES.txt`.  
**Scopo:** descrivere ciò che il path Linear effettivamente rappresenta, ammette, abbassa o rifiuta nel codice presente. Questa mappa non è una certificazione ISO C né una deduzione dalla sola suite di test. Le righe sono riferimenti al sorgente della snapshot sopra; dopo modifiche ai file, ri-verificare i numeri.

## 1. Come leggere la mappa

| Sigla | Significato operativo |
|---|---|
| **L** | Esiste un percorso esplicito di lowering Linear; la voce è supportata solo per le forme/type combination specificate. |
| **C** | Supporto condizionale: dipende da tipo, forma AST, ABI, limiti fissi, configurazione o preflight. Il resto può essere rifiutato. |
| **T** | Il codice contiene un gate/rifiuto esplicito per la compilazione lineare o un percorso che mantiene il corpo sul Tree. In modalità non-STRICT il comportamento può apparire come fallback; in STRICT è un errore. |
| **N** | Nessuna rappresentazione o implementazione Linear esplicita individuata. Non va conteggiato come supportato finché non esistono una traccia di percorso e test mirati. |

“Supportato” qui significa *il codice ha un percorso per la forma indicata*, non che tutte le combinazioni di qualificatori, conversioni, aliasing, overflow, UB e ABI siano conformi a ISO C. I limiti espliciti sono parte della copertura reale.

## 2. Pipeline reale e primo collo di bottiglia

1. `src/ir_compile.c` costruisce un IR didattico del corpo. `IrExprKind` ha **9** nodi (`IrExConst`, `IrExIdent`, `IrExBinOp`, `IrExAssign`, `IrExCall`, `IrExUnary`, `IrExIndex`, `IrExMember`, `IrExTernary`); `IrStmtKind` ha **16** nodi. Vedere `src/ir.h:37-57`.
2. Il tipo non è un type-system AST completo: gli elementi rilevanti sono `BaseType`, `FromType`, `Sizeof`, `FPFormat` e flag annotati nell'espressione. Vedere `src/interpreter.h:179-220` e `src/ir.h:39-46`.
3. Il lowering di espressioni è `LowerRec`/`LowerRecImpl` in `src/ir_lin_emit.c:464-7990`; è una sequenza di dispatch strutturali specializzati, non una tabella totale nodo×tipo. Il wrapper CSE è `src/ir_lin_emit.c:8048-8069`.
4. Gli statement sono abbassati da `LowerStmtRec1` (`src/ir_lin_compile.c:835-2569`); `IrLowerStmt` resetta le tabelle e rifiuta la funzione se la ricorsione fallisce (`src/ir_lin_compile.c:2572-2610`).
5. `FnCodeCompile` usa anche `IrStmtIntegerOk`/`IrExprIntegerOk` come preflight, quindi alcuni nodi che hanno un ramo nell'emettitore vengono comunque esclusi quando la loro struttura non passa il predicato (`src/ir_lin_compile.c:3082-3382`, `2728-2910`). I limiti di registri, istruzioni, call table, label e kernel costituiscono ulteriori rifiuti.
6. Il fallback di una funzione chiamata è selezionato in `src/ir_lower.c:1529-1659` e consumato dal chiamante in `src/ir_call.c:715-724`; STRICT usa `IrVmStrictTreeFallback` (`src/ir_lower.c:877-882`). Il hook di statement Tree/Linear è distinto (`src/ir_lower.c:2390-2570`).

### Rami di selezione osservabili

- `IrStmtIntegerOk` non è una prova di correttezza: ad esempio rifiuta VLA (`DeclArraySize == -2`), diversi `static`, struct/union inizializzati e array di puntatori fuori dai gate specifici (`src/ir_lin_compile.c:3230-3365`).
- `FnCodeCompile` rifiuta il corpo se `IrLowerStmt` fallisce, se una call è stata saltata, o se si superano i limiti di cap (`src/ir_lin_compile.c:2870-2905`).
- `LowerStmtRec` stampa il primo rifiuto annidato del chain solo quando `BABYC_VM_PROFILE` è attivo (`src/ir_lin_compile.c:812-832`); `BABYC_VM_TRACE` spiega vari rifiuti di preflight (`src/ir_lower.c:1521-1659`). Questi diagnostici esistono già: in questa fase non ho introdotto un contatore nuovo né aggiunto overhead al percorso spento.

## 3. Nodi di espressione: inventario esaustivo dell'enum

| Nodo IR | Forme sorgente rappresentabili | Stato Linear | Combinazioni e condizioni documentate nel codice |
|---|---|---|---|
| `IrExConst` | costanti intere/carattere, string literal, costanti FP | **L/C** | Le costanti intere e stringa vengono caricate come immediati/puntatori; le FP sono ammesse quando `NO_FP` non è definito e i flag/tipi sono coerenti (`ir_lin_emit.c:620-680`, `interpreter.h:179-205`). La durata/decadimento di string literal e la conversione della costante dipendono dal metadato frontend. |
| `IrExIdent` | locale, parametro, globale, enumeratore, function designator | **L/C** | Slot locali/parametri, globali risolti e function designator hanno rami espliciti (`ir_lin_emit.c:3168-3221`, `1180-1217`, `7716-7848`). Globali con risoluzione non disponibile al compile time o variabili con storage speciale possono rifiutare. |
| `IrExBinOp` | operatori binari; include virgola, logici, aritmetici, confronti, bitwise, shift | **L/C** | Gli operatori sono gestiti da rami multipli, spesso dipendenti da `TypeFlags`, larghezza e lvalue; virgola e short-circuit hanno percorsi specifici (`ir_lin_emit.c:524-534`, `4470-4586`; condizioni in `ir_lin_compile.c:760-810`). Non esiste un singolo ramo generale che provi tutte le coppie di tipo. |
| `IrExAssign` | `=`, compound assignment e assegnamenti a slot/global/index/member/deref/bitfield | **L/C** | Sono presenti percorsi distinti per identificatori, index, member, global e pointer/deref (`ir_lin_emit.c:4874-5128`, `5154-6040`, `6397-7128`). Una destinazione aggregata o una conversione non rappresentabile da store grezzo può essere rifiutata. |
| `IrExCall` | funzione nominata, intrinseca/host, utente, puntatore a funzione, callback, varargs | **C** | Argomenti e metadati sono costruiti in `ir_lin_emit.c:7238-8000`; il preflight ammette solo forme di callee riconosciute (`ir_lin_compile.c:3082-3218`). Intrinseche registrate usano l'ABI condiviso (`ir_lin_emit.c:1180-1217` e bridge di `ir_lower.c:1699-1739`); una chiamata saltata (`LinSkippedCall`) rifiuta il corpo. La whitelist host non è equivalente all'intero insieme di funzioni C/libc. |
| `IrExUnary` | cast, `sizeof`, `&`, `*`, `!`, `~`, segno, increment/decrement | **L/C** | Rami separati: cast/sizeof (`ir_lin_emit.c:3222-3330`), `&` (`683+`), dereference (`1218+`), increment/decrement (`3510-4470`). `&bitfield` è rifiutato da `IrExprIntegerOk`/gates; `sizeof` di VLA richiede metadati dinamici che Linear non implementa. |
| `IrExIndex` | `a[i]`, `p[i]`, `a[i][j]`, `argv[i][j]`, array in membri di struct | **L/C** | Supporta catene di indice a radice riconosciuta, stride e accessi memorizzati nel metadato (`ir_lin_emit.c:535-1217`, `2240-3160`, `4000-4070`, `ir_lin_compile.c:3097-3138`). `IR_MAX_ARRAY_DIMS` è 4 (`ir.h:7`); forma del root, tipo del pointee, stride e param-pointer preflight possono escludere casi (`ir_lin_compile.c:3097-3138`, `1297-1518` in `ir_lower.c`). |
| `IrExMember` | `s.m`, `p->m`, `(*p).m`, membro annidato, array o fptr membro, bitfield | **L/C** | Caricamenti/store distinti per global/local/aggregato/array, bitfield e pointer-to-aggregate (`ir_lin_emit.c:1416-2080`, `4220-4470`, `4915-6040`, `5154+`). Il caso per valore di struct/union e le conversioni del membro sono più ristretti del semplice accesso scalare; `&bitfield` non è valido. |
| `IrExTernary` | `c ? a : b` | **L/C** | Viene abbassato a branch e join quando ha `Left`, `Right`, `Args[0]` (`ir_lin_emit.c:4527-4578`; short-circuit in `ir_lin_compile.c:765-810`). Le conversioni al tipo comune sono esplicite solo per tipi gestiti dal lowerer: scalari interi, FP e puntatori sono i casi principali; aggregate/valori speciali restano da verificare. |

### Espressioni non rappresentate da un nodo AST dedicato

`IrExprKind` non contiene nodi distinti per lvalue conversion, array-decay, designator di funzione, compound literal, initializer-list expression, GNU statement-expression, `_Generic` o `_Atomic`; questi concetti devono essere piegati in uno dei nove nodi oppure rimangono fuori dal Body-IR. Il frontend primario e parser cast sono in `ir_compile.c:708-1030`; non ho trovato un percorso esplicito completo per compound literal o statement-expression. Questi elementi sono **N**, non “supportati perché la suite passa”.

## 4. Operatori: matrice per famiglia e tipo degli operandi

| Operatore/token | interi signed/unsigned e narrow | FP `float`/`double` | puntatori | stato e decisioni nel codice |
|---|---|---|---|---|
| `,` (`TokenComma`) | **L**: side effects sinistra, valore destro | **L** quando il valore destro FP è ammesso | **L/C** se il valore destro è puntatore | `LowerRecImpl` abbassa sequenzialmente e resetta CSE (`ir_lin_emit.c:524-534`, `4579-4586`). Le espressioni a sinistra possono comunque rifiutare. |
| `+`, `-` binari | **L/C**, dipende da larghezza, segno e promozioni | **L/C**, FP ALU add/sub abilitata | **L/C** con scalatura per il tipo puntato e forma di puntatore riconosciuta | `ir_lin_emit.c:3357-3470`, `4470-4526`, helper stride `97-390`. Non è provata ogni conversione prevista dallo standard. |
| `*`, `/` binari | **L/C**, width/signedness e divisione per zero runtime | **L/C**, ramo FP add/mul/div nel sottoinsieme ammesso | non applicabile, salvo cast/deref separato | `ir_lin_emit.c:3378-3470` e `ir_lin_exec.c` operazioni; modulo per zero è diagnostica runtime, non fallback semantico. |
| `%` | **L/C** per interi ammessi; signedness/overflow edge-case da provare | non valido nel C standard per FP | non valido sul puntatore | È generato come operazione binaria dal lowerer e interpretato dal runtime; non equivale a supporto completo di UB/edge cases. `ir_lin_emit.c:1040-1050`, runtime in `ir_ops.c`. |
| `<<`, `>>` | **C**: esistono opcode/branch, ma kernel scalar-only li esclude e signed shift/shift count richiedono test dedicati | non valido per FP | non valido | `ir_lin_compile.c:322-355` rifiuta shift nel kernel scalar-only; `TokenShift*` gestito in emitter/runtime. Le regole C per shift di signed negativi o count fuori range non sono una prova di copertura. |
| `<`, `>`, `<=`, `>=`, `==`, `!=` | **L/C**; confronto signed/unsigned selezionato tramite flag | **L/C**: confronto FP esplicito e predicate int | **C**: uguaglianza/relazioni pointer sono limitate dal lowerer e dalla rappresentazione host | `Rel`/`RelU` (`ir_lower.c:1744-1795`), branch e FP compare (`ir_lin_compile.c:760-810`, `ir_lin_emit.c:4470+`). Comparazioni fra puntatori a oggetti diversi/ordering non dimostrate come regole portabili. |
| `&&`, `||`, `!` | **L/C** con short-circuit / normalizzazione truth | **C**: confronto/logica produce int, non FP | **C**: null/truth-value su address bits | Short-circuit per condizioni in `ir_lin_compile.c:760-810`; unary in `ir_lin_emit.c:3500+`. Gli effetti nei rami devono invalidare le cache. |
| `&`, `|`, `^`, `~` bitwise | **L/C**, larghezza/signedness del risultato va testata per narrow integer e promotion | non validi per FP | unario `&` è indirizzo, binari non validi | `ir_lin_emit.c` unary/address `683+`, bitwise op emissione distribuita nei binari; kernel scalar-only rifiuta alcuni compound bitwise (`ir_lin_compile.c:322-355`). |
| `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `<<=`, `>>=`, `&=`, `|=`, `^=` | **L/C**, lvalue e narrowing differenti | **C**, conversione float/int e FP storage possono rifiutare | **C**, pointer assignment e pointer compound arithmetic richiedono pointee stride | I rami per lvalue sono specializzati (`ir_lin_emit.c:4874-7128`). Store che non possa preservare tipo/diagnostica non è genericamente ammesso. |
| pre/post `++`, `--` | **L/C**, int, narrow, signed/unsigned | **C**, FP increment/decrement da verificare per valore pre/post e storage format | **C**, stride puntatore su pointee noto | numerosi rami specializzati in `ir_lin_emit.c:3510-4470`; la presenza di un ramo non copre tutti gli lvalue. |
| `cast` e conversioni implicite | **C**: narrow, signed/unsigned, enum e integer promotions sono annotate in più livelli | **C**: conversione int↔FP e binary32/binary64 ha opcode, ma non esiste `long double` nativo | **C**: pointer/integer cast e function pointer hanno ABI/size-sensitive paths | `IrLinEmitScalarConversion` (`ir_lin_emit.c:448-461`), cast (`3222-3292`), type/width helpers `ir_ops.c`, `type.c`. Promozioni, usual arithmetic conversions e conversioni di ritorno non sono dimostrate esaustive da un singolo gate. |
| `sizeof` | **L/C** se dimensione compile-time nota | **C** per FP/aggregati noti | **L/C** su puntatori con size host nota | `ir_lin_emit.c:3293+`; VLA escluso. `sizeof(long double)` è tradotto a `sizeof(double)` nel frontend (`ir_compile.c:1219-1276`), quindi la semantica è emulata. |
| `[]`, `.`, `->`, `*p`, `&obj` | **L/C** per oggetti e stride supportati | **C** per elementi/membri FP e relative conversioni | **L/C** con pointer/index metadata, null/bounds e array-decay da validare | `ir_lin_emit.c:535-3160`, `1218+`, `1416+`, `683+`; metadata e lvalue in `ir_storage.c`, `ir_lin_ptr.c`, `ir_mem.c`. |
| `? :` | **L/C** per scalar arms; common-type conversions da controllare | **C**, il ramo condizionale normalizza FP sotto strict | **C**, pointer arms compatibili dipendono dal tipo anotato | `ir_lin_emit.c:4527-4578`. Struct/union per valore non è certificato. |

**Importante:** “non valido” indica l’operatore con quel tipo di operandi non appartenente al C standard (per esempio `%` fra FP). “C” indica casi validi del linguaggio che non sono coperti in modo dimostrato per ogni combinazione.

### Inventario per token operatore (`enum LexToken`)

La tabella seguente rende esplicite le combinazioni più importanti per ogni token, invece di inferire che il supporto di una famiglia copra automaticamente tutti i tipi. I token provengono da `src/interpreter.h:103-128`; lo stato descrive il percorso sorgente Linear e i gate visti in `ir_lin_emit.c` / `ir_lin_compile.c`, non la validità sintattica di un operatore su tipi per cui il C lo vieta.

| Token / operatore | signed/unsigned e narrow | `float` / `double` | `long double` | puntatori | struct/union, array, bitfield | Evidenza e limite |
|---|---|---|---|---|---|---|
| `TokenComma` `,` | L/C; sinistra valutata per effetti, risultato destro | C; dipende dal formato del risultato destro | T/semantica double | L/C se il risultato destro è pointer | C; aggregate result non certificato | `ir_lin_emit.c:524-534`, `4579-4586`; invalida CSE fra i lati. |
| `TokenAssign` `=` | L/C, incluse conversioni/narrowing | C, binary32/64 | T/semantica double | C per object/fptr e conversione null/integer | C/T per aggregate e bitfield | store specializzati in `ir_lin_emit.c:4874-7128`; preflight in `ir_lin_compile.c:3082-3382`. |
| `TokenAddAssign` `+=` | L/C | C | T/semantica double | C per pointer + integer con stride valido | T per aggregate/array/bitfield non indirizzabile | `ir_lin_emit.c:4874-5128`, `6397-7128`; promozione + store da convalidare insieme. |
| `TokenSubtractAssign` `-=` | L/C | C | T/semantica double | C per pointer − integer con stride valido | T per aggregate/array/bitfield non indirizzabile | Stessi rami compound assignment e helper stride `ir_lin_emit.c:97-390`. |
| `TokenMultiplyAssign` `*=` | L/C | C | T/semantica double | N/A nel C | N/A nel C per aggregate; bitfield dipende dallo store | Ramo compound assignment, conversione al tipo di destinazione da verificare (`ir_lin_emit.c:4874-5128`). |
| `TokenDivideAssign` `/=` | L/C; divisione per zero runtime | C; FP divide | T/semantica double | N/A nel C | N/A per aggregate | Rami arithmetic compound e runtime `ir_ops.c` / `ir_lin_exec.c`. |
| `TokenModulusAssign` `%=` | C; interi soltanto | N/A nel C | N/A nel C | N/A nel C | N/A per aggregate | Il modulo viene abbassato come integer op; edge case signed/overflow restano da validare. |
| `TokenShiftLeftAssign` `<<=` | C; promozioni, signed UB/range da testare | N/A nel C | N/A nel C | N/A nel C | N/A per aggregate | L'operatore può essere rifiutato dai kernel scalar-only (`ir_lin_compile.c:322-355`). |
| `TokenShiftRightAssign` `>>=` | C; signed negative/right-shift implementation-defined | N/A nel C | N/A nel C | N/A nel C | N/A per aggregate | Come sopra; opcode presente non prova comportamento standard per tutti i target. |
| `TokenArithmeticAndAssign` `&=` | C; bitwise integer promotion | N/A nel C | N/A nel C | N/A nel C | N/A per aggregate | Rami compound specializzati; kernel scalar-only pone limiti (`ir_lin_compile.c:322-355`). |
| `TokenArithmeticOrAssign` `|=` | C; bitwise integer promotion | N/A nel C | N/A nel C | N/A nel C | N/A per aggregate | Stessi limiti del gruppo bitwise. |
| `TokenArithmeticExorAssign` `^=` | C; bitwise integer promotion | N/A nel C | N/A nel C | N/A nel C | N/A per aggregate | Stessi limiti del gruppo bitwise. |
| `TokenQuestionMark` / `TokenColon` `?:` | C; tipo comune + branch/join | C; common FP type e rounding da testare | T/semantica double | C; conversione fra pointer compatibili | C/T per struct/union per valore | `ir_lin_emit.c:4527-4578`; aggregate/non-scalari non certificati. |
| `TokenLogicalOr` `||` | L/C, short-circuit | C, risultato `int` | T/semantica double negli operandi | C, truth/null pointer | N/A su aggregate | `ir_lin_compile.c:760-810`; effetti nei rami devono essere rispettati. |
| `TokenLogicalAnd` `&&` | L/C, short-circuit | C, risultato `int` | T/semantica double negli operandi | C, truth/null pointer | N/A su aggregate | `ir_lin_compile.c:760-810`. |
| `TokenArithmeticOr` `|` | L/C; promotions e width | N/A nel C | N/A nel C | N/A nel C | N/A nel C | Emitter/runtime distribuiti; kernel scalar-only può rifiutare. |
| `TokenArithmeticExor` `^` | L/C; promotions e width | N/A nel C | N/A nel C | N/A nel C | N/A nel C | Emitter/runtime distribuiti; kernel scalar-only può rifiutare. |
| `TokenAmpersand` `&` binario | L/C; promotions e width | N/A nel C | N/A nel C | N/A come binario (unario `&obj` separato) | N/A nel C | Binario bitwise; il token unario address-of ha path separato (`ir_lin_emit.c:683+`). |
| `TokenAmpersand` `&` unario (address-of) | L/C su object lvalue addressable | C su FP object | T/semantica double solo per il tipo dell’oggetto indirizzato | L/C per object pointer e function designator, secondo ABI | C per array/member/aggregate; **T** su bitfield (`&bitfield` rifiutato) | Ramo address `ir_lin_emit.c:683+`; gate `IrExprIntegerOk` in `ir_lin_compile.c:3082-3230`. |
| `TokenEqual` `==` | L/C signed/unsigned conversions | C, FP compare | T/semantica double | C per equality object/function pointer sotto rappresentazione host | C per aggregate non ammesso direttamente | `Rel`/`RelU` in `ir_lower.c:1744-1795`; FP compare `ir_lin_emit.c:4470+`. |
| `TokenNotEqual` `!=` | L/C | C, FP compare | T/semantica double | C | C per aggregate non ammesso direttamente | Stessa implementazione relazionale di `==`, risultato boolean-like `int`. |
| `TokenLessThan` `<` | L/C signed/unsigned conversions | C, FP compare | T/semantica double | C; ordering fra oggetti distinti non portabile | N/A per aggregate | `Rel`/`RelU` e condition branch. |
| `TokenGreaterThan` `>` | L/C | C, FP compare | T/semantica double | C; ordering limitato | N/A per aggregate | Stesse famiglie di confronto. |
| `TokenLessEqual` `<=` | L/C | C, FP compare | T/semantica double | C; ordering limitato | N/A per aggregate | Stesse famiglie di confronto. |
| `TokenGreaterEqual` `>=` | L/C | C, FP compare | T/semantica double | C; ordering limitato | N/A per aggregate | Stesse famiglie di confronto. |
| `TokenShiftLeft` `<<` | C; promotions/count/range da verificare | N/A nel C | N/A nel C | N/A nel C | N/A nel C | Token presente ma rifiutato da alcuni kernel (`ir_lin_compile.c:322-355`). |
| `TokenShiftRight` `>>` | C; signed negative behavior target-sensitive | N/A nel C | N/A nel C | N/A nel C | N/A nel C | Stessi limiti shift; `ir_lin_exec.c`, `ir_ops.c`. |
| `TokenPlus` `+` unario | L/C per interi promossi | C, unary plus deve preservare il tipo FP | T/semantica double | N/A nel C | N/A nel C | Ramo unary `ir_lin_emit.c:3500+`; il rank/promoted type dipende dai metadati frontend. |
| `TokenMinus` `-` unario | L/C per signed/unsigned/narrow dopo promozione | C, unary minus FP | T/semantica double | N/A nel C | N/A nel C | Ramo unary `ir_lin_emit.c:3500+`; edge signed overflow e rounding non sono coperti dalla sola presenza del ramo. |
| `TokenPlus` `+` binario | L/C | C, FP add | T/semantica double | C per pointer + integer / integer + pointer ammesso dalla forma | N/A su aggregate | `ir_lin_emit.c:3357-3470`, FP `4470-4526`, stride `ir_lin_emit.c:97-390`. |
| `TokenMinus` `-` binario | L/C | C, FP subtract | T/semantica double | C per pointer − integer e differenza di pointer in casi rappresentabili | N/A su aggregate | Stessi helper arithmetic/stride. |
| `TokenAsterisk` `*` binario | L/C integer multiply | C, FP multiply | T/semantica double | N/A nel C | N/A su aggregate | `ir_lin_emit.c:3378-3470`, runtime in `ir_lin_exec.c`. |
| `TokenAsterisk` `*` unario (dereference) | C per puntatore a scalare integer | C per puntatore a `float`/`double` | T/semantica double pointee | L/C se il pointee è tipizzato e stride/address valido; `void *` richiede cast | C per pointer-to-struct/union, array e member; bitfield resta un lvalue non-addressable | Ramo dereference `ir_lin_emit.c:1218+`; parameter/pointer gate `ir_lower.c:1297-1520`; access path dipende dal tipo pointee. |
| `TokenSlash` `/` | L/C; divisione per zero diagnostica | C, FP divide | T/semantica double | N/A nel C | N/A su aggregate | `ir_lin_emit.c:3378-3470`, `ir_ops.c`. |
| `TokenModulus` `%` | L/C per gli interi ammessi; signed edge case da testare | N/A nel C | N/A nel C | N/A nel C | N/A su aggregate | Emissione integer op e runtime (`ir_lin_emit.c:1040-1050`, `ir_ops.c`). |
| `TokenIncrement` `++` (pre/post) | L/C per lvalue scalare e narrow | C; storage e valore pre/post da provare | T/semantica double | C; stride del pointee necessario | N/A su aggregate; bitfield solo nei percorsi aggiornabili | `ir_lin_emit.c:3510-4470`; ogni lvalue ha path diverso. |
| `TokenDecrement` `--` (pre/post) | L/C | C | T/semantica double | C; stride necessario | N/A su aggregate; bitfield condizionale | Stessi rami increment/decrement. |
| `TokenUnaryNot` `!` | L/C; produce `int` | C; test di zero FP | T/semantica double negli operandi | C; null/truth-value | N/A su aggregate | `ir_lin_emit.c:3500+`, branch conditions `ir_lin_compile.c:760-810`. |
| `TokenUnaryExor` `~` unario | L/C per interi promossi | N/A nel C | N/A nel C | N/A nel C | N/A nel C | Ramo unario e integer width (`ir_lin_emit.c:3500+`, runtime ops). |
| `TokenSizeof` `sizeof` | L/C se size statica e tipo completo | C | T: tradotto come double | L/C per size host del pointer | C per dimensione compile-time aggregate/array completo | `ir_lin_emit.c:3293+`; VLA escluso; frontend `ir_compile.c:1219-1276`. |
| `TokenCast` cast `(T)e` | C; integer rank/conversion completezza da auditare | C int↔FP, Binary32/64 | T/semantica double | C pointer/integer/function pointer, ABI-sensitive | C/T per cast aggregate | `IrLinEmitScalarConversion` (`ir_lin_emit.c:448-461`), cast `3222-3292`. |
| `TokenLeftSquareBracket` `[]` | L/C per array/pointer e stride noto | C per array di FP | T/semantica double element | L/C per `p[i]`, `argv[i][j]`, pointer to array ammesso dalla forma | C per array membro / bitfield non indirizzabile | `ir_lin_emit.c:535-3160`, `ir_lin_compile.c:3097-3138`; max 4 dimensioni. |
| `TokenDot` `.` | C per member scalare | C per member FP | T/semantica double member | N/A direttamente sul pointer (`->` o `(*p).m`) | C/T per nested struct/union, array-member e bitfield | `ir_lin_emit.c:1416-2080`, `4220-4470`. |
| `TokenArrow` `->` | C per pointer-to-struct/union | C per member FP | T/semantica double member | L/C con base pointer tipizzata | C/T per nested aggregate/bitfield e store | `ir_lin_emit.c:1416-2080`, `4915-6040`; preflight `IrExprIntegerOk`. |

`TokenOpenBracket`, `TokenCloseBracket`, `TokenLeftBrace`, `TokenRightBrace`, `TokenSemicolon` e i token di dichiarazione/controllo non sono operatori espressivi: sono coperti dal parser e dalla matrice degli statement. `TokenEllipsis` è coperto nella sezione varargs, con le limitazioni ABI indicate sopra.

## 5. Statement: ogni valore di `IrStmtKind`

| `IrStmtKind` | Stato Linear | Limiti effettivi |
|---|---|---|
| `IrStExpr` | **L/C** | Espressione abbassabile; `LowerRec` deve riuscire. Assegnamenti e call possono rifiutare per forma/tipo. `ir_lin_compile.c:1470-1534`. |
| `IrStBlock` | **L/C** | Walk sequenziale dei figli; il rifiuto di un figlio rifiuta il corpo. `1535-1544`. |
| `IrStIf` | **L/C** | Lower condizionale + branch patching; condizione richiede `LowerCondFalse`. `1545-1559`, `760-810`. |
| `IrStReturn` | **L/C** | Return void o valore; FP scalar wrapper esiste nel frontend. L'aggregate return/by-value ABI e conversioni non sono complete in tutte le forme. `2548-2569`, `ir_compile.c:2450-2475`. |
| `IrStBreak` | **L/C** | Patch di salto; limite 12 break pendenti per loop/switch. `ir_lin_compile.c:400-430`, `2464+`. |
| `IrStContinue` | **L/C** | Patch verso il punto continue/step; loop annidati limitati da tabelle fisse. `ir_lin_compile.c:400-430`, `2466+`. |
| `IrStWhile` | **L/C** | Condizione e body generici, più shape/kernel facoltativi; limiti a loop depth, branch patch e risorse. `1561-1766`. |
| `IrStDo` | **C** | Emitter presente (`2421-2460`), ma un commento nel preflight descrive un residuo sotto il vecchio `EXEC=vm`; non assumere che tutte le modalità/preflight accettino ogni corpo. |
| `IrStFor` | **L/C** | init/cond/body/inc generici con promotion di locali e kernel opzionali; call, VLA, goto/label o regioni non compatibili possono rifiutare. `1767-2420`. |
| `IrStDecl` | **C/T** | Distinti percorsi per scalari, pointer, function pointer, array, array di pointer, struct/union e static storage. VLA è esplicitamente rifiutato; limiti di grandezza/cap e forme di initializer sono enumerati sotto. `873-1450`, preflight `3230-3361`. |
| `IrStDelegate` | **T** | Rifiuto esplicito in `IrStmtIntegerOk` (`ir_lin_compile.c:3365`); nessun lowering generale del delegate. |
| `IrStSwitch` | **C** | Switch a espressione + blocco, al massimo **16** case; table/loop/branch patch hanno limiti fissi. `ir_lin_compile.c:2468-2546`. |
| `IrStCase` | **L** come etichetta | Non emette codice autonomo, viene usato dal walk dello switch. `843-845`, `2500-2545`. |
| `IrStDefault` | **L** come etichetta | Non emette codice autonomo; l'ingresso al default e il fallthrough dipendono dall'ordine sequenziale nel blocco. `843-845`, `2500-2545`. |
| `IrStGoto` | **C** | Salto assoluto/patch avanti e sync dei locali. Tabelle limitate: 16 label e max 8 patch pending per label; salti attraverso scope con VLA o inizializzazioni non sono equivalenti automaticamente alle regole C. `858-871`, helper `LabGet` e `ir_call.c:732+`. |
| `IrStLabel` | **C** | Posiziona l'etichetta e patcha i goto; duplicati o tabella piena rifiutano. `850-857`, helper `LabGet`. |

### `switch` e fallthrough

L'emettitore trova i case in ordine e poi abbassa sequenzialmente i figli non-case: questo implementa il fallthrough per i corpi rappresentabili (`ir_lin_compile.c:2468-2546`). Non implica che siano accettati switch con oltre 16 case o con espressioni case non abbassabili. Le estensioni compiler-specifiche (case ranges GNU) non sono parte dei nodi IR elencati.

## 6. Matrice incrociata dei tipi e delle forme di memoria

| Tipo/forma C | Stato | Evidenza / limiti nel sorgente |
|---|---|---|
| `char`, `short`, `int`, `long`, `long long` signed e unsigned; enum | **L/C** | `BaseType` separa i tipi di intero (`interpreter.h:179-211`). L'emettitore porta larghezza e signedness tramite flag/metadati; `IrStmtIntegerOk` ammette molti slot scalari (`ir_lin_compile.c:3340-3360`) ma la corretta usual arithmetic conversion va auditata per ogni coppia e ogni operatore. |
| integer promotions e narrow assignment (`char`/`short`) | **C** | Narrowing dei local store e increment è implementato in runtime/storage (`ir_lin_exec.c`, `ir_ops.c`, `ir_storage.c`); il tipo temporaneo è spesso un `long long` di trasporto. Non c'è una sola funzione che codifichi tutte le regole ISO per `int` rank, rank dei tipi e conversione signed/unsigned. |
| `float` e `double` | **C** | Entrambi sono `TypeFP`, distinti da `FPFormat` Binary32/Binary64 (`interpreter.h:189-204`, `type.c:115-143`). L'ALU FP strict è parziale e il valore usa bit pattern host; binary32 memory storage non prova rounding esatto a ogni operazione/cast/call. Frontend con `NO_FP` esclude questo supporto. |
| `long double` | **T/semantica non conforme** | Non c'è `TypeLongDouble` o `FPStorageBinary80/128`; `ir_compile.c:1219-1276` e `3247+` trattano long double come double. Non supporta la precisione/range C del long double host. |
| `void *` e object pointer | **C** | Puntatori scalari, cast, dereference, pointer arithmetic con stride noto e null sono rappresentati. Alcuni usi `void *` sono ammessi solo con cast immediato a tipo tipizzato (`ir_lower.c:1297-1359`), e la prova dei parametri pointer esclude diversi pattern (`1362-1520`). |
| function pointers, array di function pointers, callback | **C** | Ci sono slot/descriptor `DeclIsFptr`, function designator e callee indirette per identificatore, `tab[i]`, member (`ir_lin_emit.c:7716-8000`, `ir_lin_compile.c:1298-1355`). Array dichiarati in forme non riconosciute, ABI callback o ritorno/argomenti speciali possono essere rifiutati. |
| array statici/globali/locali monodimensionali | **L/C** | Storage blob, metadati di elemento e kernel decl esistono; size/cap e i tipi di elemento controllano l'ammissibilità (`ir_lin_compile.c:918-1110`, `ir_lin_emit.c:535+`). |
| array multidimensionali statici/globali/locali | **C** | `IR_MAX_ARRAY_DIMS=4`; lowering appiattisce indici per forme a radice riconosciuta e scala per stride (`ir_lin_emit.c:800-1177`, `2400-3160`, `ir_lin_ptr.c`). Array annidati dietro cast/return/call/alias o `T (*)[N]` non sono una garanzia generale. |
| array di pointer e `char **` (`argv[i][j]`) | **C** | Percorso kind-4 è esplicito per pointer-to-pointer e ha preflight di live range/stride (`ir_lower.c:1362-1520`), mentre array di pointer locali hanno limiti size (`ir_lin_compile.c:968-1045`, `3330-3339`). Non ogni pointer-to-pointer può essere dereferenziato arbitrariamente. |
| struct/union scalar e member access | **C** | Member load/store e oggetto posseduto sono implementati; accessi semplici/membri annidati dipendono dalla forma base (`ir_lin_emit.c:1416-2080`, `4915-6040`). |
| struct/union per valore: assegnazione, argomenti, ritorni, ternario | **C/T** | Alcuni oggetti locali sono blob owned (`ir_lin_compile.c:1119-1257`) e il runtime di call ha un ABI separato (`ir_call.c:649+`). Non tutte le copie/conversioni/ritorni aggregate passano il preflight: `IrStmtIntegerOk` esclude vari inizializzatori strutturati (`3249-3265`, `3347-3359`); aggregate nel ternario e aggregate return richiedono gap-specific tests. |
| union active member / type-punning / aliasing | **C/semantica non provata** | Lo storage è blob, ma il frontend/runtime non dimostrano il modello ISO di active member, effective type e aliasing per tutte le letture. |
| bitfield signed/unsigned, spanning/storage unit, update e indirizzo | **C** | `BitOffset`, `BitWidth`, `BitSigned` esistono nell'IR (`ir.h:45`), con helper per extract/insert (`ir_mem.c`, `ir_ops.c`) e rami member/store. `&bitfield` è rifiutato; initializer di struct che tocchi bitfield viene rifiutato in vari percorsi (`ir_lin_compile.c:1185-1187`). Layout/packing è dipendente dal parser/ABI: non è universale ISO a livello di bit allocation. |
| argomenti varargs per intrinseche/host | **C** | `FuncDef.VarArgs`, arg count e flags di argomento vengono tenuti nelle tabelle call; packing intrinseche passa per il bridge condiviso (`ir_lin_emit.c:7238-8000`, `ir_lower.c:1699-1739`, `ir_call.c:813-820`). Va provato tipo default-promotion, FP, pointer, `NULL`, formato printf/scanf e target MSVC. |
| funzioni user-defined variadic (`...`) | **C/T** | Il preflight del bytecode terminal-call esclude `VarArgs` (`ir_lower.c:1949-1954`); la chiamata lineare generica può gestire alcune forme, ma nessun ABI completo e trasversale per `va_start`/`va_arg` è dimostrato. |
| struct/union con array membri, nested arrays, initializer | **C** | `IrBraceInitPlan` gestisce alcuni percorsi di nested aggregates/designator (`ir_lin_compile.c:1153-1205`). Limiti: massimo 64 initializer expressions per il piano, conversioni int↔FP non coperti dal raw store e bitfield rifiutati (`1164-1207`). |
| bitfield/FP/membro aggregato in initializer | **T/C** | Inizializzatori con target bitfield o mismatch integer↔FP sono rifiutati esplicitamente (`ir_lin_compile.c:1183-1207`); float member con storage non di 8 byte rifiutato. |
| initializer designati `.field = v` in struct/union | **C** | Frontend registra i designator; `IrBraceInitPlan` li interpreta per i casi aggregati ammessi (`ir_compile.c:4262-4298`, `ir_lin_compile.c:1153-1179`). Non equivale a supporto esaustivo di array-designator, designator annidati arbitrari o ogni conversione. |
| initializer designati `[i] = v` per array | **N/C** | IR di dichiarazione contiene `DeclInitVals`, `DeclInitExprs`, `DeclInitNames`, ma non una sequenza generale di path/index designator (`ir.h:52-56`). Non ho trovato una prova unica di lowering completo per ogni forma standard nested/designated; lasciare in stato non verificato. |
| local `static` | **C/T** | Fast paths separati per scalar, array scalar BSS e struct/union BSS non inizializzati, con limiti (`ir_lin_compile.c:889-960`; preflight `3239-3280`). Static function-pointer, scalar pointer, FP static e brace-init struct/array sono rifiutati dal preflight o da un ramo specifico (`3240-3265`). |
| global `static` e `extern` | **C** | Global lookup e load/store esistono, ma l'interpretazione resta un singolo environment/runtime, non un linker multi-TU C. L'IR non trasporta tutta la semantica di linkage/ODR-like collision, tentative definitions e visibility in un oggetto completo; verificare ogni storage class per ogni tipo. |
| `extern` locale, `register`, `auto`, `volatile`, `const` | **C** | Parser e tipo mantengono alcuni qualifier/attributi; l'IR usa `DeclIsConst`, `TypeFlags`, `StaticQualifier`. La volatilità di ogni accesso non è dimostrata da load/store generici; `const` è protetto solo nei percorsi con gate/fusione specifici (`ir_lin_emit.c: fuse helper`, `ir_storage.c`). |
| VLA `int a[n]` e `sizeof a` dinamico | **T** | Il frontend annota `DeclArraySize == -2` e l'espressione della dimensione (`ir_compile.c:3366-3575`); sia `LowerStmtRec1` sia `IrStmtIntegerOk` rifiutano esplicitamente il corpo (`ir_lin_compile.c:873-880`, `3234-3239`). È un buco lineare noto. |
| compound literal `(T){...}` | **N/T** | Nessun `IrExCompoundLiteral`/nodo initializer-expression. `IrCompilePrimary` riconosce parentesi/cast e poi expression, non un nodo con durata/storage per compound literal (`ir_compile.c:708-1030`). Non segnare come supportato senza un test indipendente e una rappresentazione esplicita. |
| `setjmp` / `longjmp` | **N/T** | Non esiste opcode o nodo IR dedicato né state-save/restore di register temporaries, label/goto state e cleanup per il nonlocal jump nel path Linear. L'eventuale presenza come symbol/host-call non basta: fino a test che dimostrino il ritorno doppio e lo stato automatico, non è supporto. |
| ricorsione, call stack e side effects di call | **C** | Per-body `FnCodeSlot` e frame/call bridge esistono, ma fixed tables/call arg caps, per-depth state e eligibility differiscono fra Linear e compact bytecode. Call che può osservare locali, FP, aggregate e varargs deve preservare/convertire l'ABI correttamente (`ir_lin_exec.c:2430+`, `ir_lower.c:1040+`, `ir_lin_emit.c:7238+`). |
| memoria dinamica (`malloc/free/realloc/calloc`) e puntatori invalidati | **C** | Alcune intrinseche sono ammesse via bridge e store/load checked; lifetime tracking, use-after-free, double-free, interop con guest/native e aliasing sono runtime semantics distinte dal lowering e non sono automaticamente coperte dal gate. `ir_mem.c`, `heap.c`, `ir_call.c`. |

## 7. Feature C che non vanno implicitamente considerate coperte

Questa lista è intenzionalmente esplicita. In assenza di un nodo/ramo e di test indipendenti la classificazione resta N/T, anche se un token path potrebbe riuscire a interpretare la stessa sorgente.

- Compound literal, statement expressions GNU, label-as-value/computed-goto GNU, `typeof`/`__typeof__` e altre estensioni compiler-specifiche.
- `_Generic`, `_Atomic`, atomiche C11, `_Alignas`, `_Alignof`, `_Static_assert`, `_Thread_local`, `_Complex`, `_Imaginary` e feature moderne che il parser/IR non rappresenta espressamente.
- Semantica completa `setjmp`/`longjmp`, signal/interrupt, unwind, cleanup attributes, destructors o salti non locali attraverso corpi compilati.
- Array VLA, puntatori a VLA, `sizeof` su VLA e salto `goto` in/out di scope VLA.
- Long double di precisione nativa: la rappresentazione attuale lo traduce a double.
- ABI universale per struct/union per valore in argomenti, ritorni, varargs, function-pointer callback e membri FP/bitfield misti.
- Tutte le forme di designator annidati e initializer C99/C23, inclusi array index designator combinati con nested aggregate, conversioni, unioni e bitfield.
- Tutti i casi di conversione intera standard: integer rank/precision per target diversi, usual arithmetic conversions in ogni operatore, shift di signed/negativi, overflow signed UB, trap/implementation-defined behavior e width-specifici MSVC/32-bit.
- Semantica completa di effective type/strict aliasing, padding/packing, object lifetime, uninitialized reads, indeterminate values e puntatori one-past/invalidi per ogni path.
- Linkage multi-translation-unit, tentative definitions e tutte le interazioni `static`/`extern`/TLS come in un toolchain C completo.

## 8. Bachi ordinati per priorità e piano di chiusura

L'ordine sotto è una **priorità ingegneristica qualitativa**, basata su quanto spesso questi costrutti compaiono in programmi C ordinari e su quanto ampiamente toccano l'ABI. Non è una statistica misurata sul corpus; la frequenza va misurata con un corpus C indipendente prima di fissare gli obiettivi numerici.

1. **Promozioni/conversioni intere e signedness su ogni operatore** — molto frequenti, alta propagazione d'errore. Costruire oracle differential per ogni coppia `char/short/int/long/long long`, signed/unsigned, enum, costante e variabile; `+/-/*//%`, shift, compare, compound assignment, increment e cast; target host LP64 e Windows LLP64/32-bit. Codificare le conversioni in una singola API tipizzata condivisa, non nei singoli rami del lowerer.
2. **Valori e ABI di chiamata** — scalar/pointer/FP, fptr, callback, varargs, `NULL`, default argument promotions, ritorni e ricorsione. Inventariare per ogni call site il tipo formale/effettivo e il pack, quindi testare ABI intrinseca e user-defined separatamente; nessun nome host deve funzionare solo perché è in whitelist.
3. **Array/pointer e aliasing** — multidimensionali, `T (*)[N]`, `T **`, array di pointer/fptr, cast pointer, member arrays, pointer arithmetic one-past e puntatori invalidi. Generare test da tipologia dell'espressione e dimensioni, poi separare calcolo indirizzo da policy bounds/lifetime.
4. **Struct/union per valore e initializer** — copia/assegnamento, parametri e return, nested aggregate, array members, union, designated initializer, conversioni e inizializzazione parziale. Usare un layout engine condiviso e un piano initializer con offset, tipo del target e conversione obbligatoria per ogni initializer; aggiungere test per bitfield e FP anziché fallback silenzioso.
5. **FP: rounding, binary32/binary64, conversioni, comparator, call/return** — `float`, `double`, pointer-to-FP, array/members FP, varargs `%f/%lf`, `scanf`, cast int↔FP. Specificare i punti di rounding e verificare bitwise vs GCC; dichiarare `long double` non supportato finché non c'è un formato dedicato.
6. **Control flow totale** — `switch` oltre i limiti, nested loop/switch, fallthrough, `goto`/label cross-scope, `continue`/`break` label patch ed effetti prima/dopo call. Rimuovere limiti fissi solo dopo validazione e storage dinamico sicuro; testare salti da ogni blocco e stato dei locals/initializer.
7. **Storage duration/linkage/qualifiers** — static local scalare/pointer/FP/aggregate/array, extern, tentative definition, const/volatile e uninitialized safety. Un descriptor comune deve separare type, storage duration, ownership, initialized state e aliasing; evitare risoluzioni globali che anticipano scope irraggiungibili.
8. **VLA** — frequenza media nei programmi C tradizionali, utile in C99 ma facoltativa in C11. Implementare un descrittore runtime per dimensioni/stride/sizeof e stack charge; solo poi ammettere il VLA nel lowering e nei goto gate.
9. **Compound literal e initializer-list expression** — molto usati in C99+ per API, callback e return-by-value. Aggiungere nodo IR/descrittore di lifetime esplicito per block/file scope e connect al piano degli initializer; prima implementare aggregati generali per valore.
10. **`setjmp`/`longjmp` e feature C11/C23 rare** — costo architetturale alto; implementare soltanto dopo frame, cleanup, register state e storage duration chiaramente modellati. Feature atomiche/thread/TLS richiedono una specifica di concorrenza/runtime, non un semplice opcode.

### Gate di chiusura raccomandato per ogni voce

1. Scrivere una specifica di semantica/formati ABI prima di modificare il lowering.
2. Aggiungere test minimi per tutte le combinazioni tipo×operatore e test composti per aliasing/side effect; non riutilizzare i nomi/shape dei benchmark.
3. Confrontare output, stato di terminazione e diagnostiche con Tree e GCC dove il comportamento è definito; isolare esplicitamente UB/implementation-defined e non inventare un oracle.
4. Eseguire Tree e Linear STRICT con ACCEL 0/1, build GCC e MSVC (x64 e 32-bit dove applicabile), test di memoria e controlli su `.expect` invariati.
5. A/B alternato su benchmark indipendenti, misurare overhead e aggiungere contatori per contesto quando la causa non è attribuibile; non accettare una media aggregata come prova di assenza di regressione.
6. Registrare per ogni costrutto **Supportato / Condizionale / Fallback / Rifiutato** in una suite di microtest separata dalla suite regressione. La copertura dei test di output è un asse, la copertura delle feature del linguaggio è un altro.

## 9. Copertura dinamica e fallback osservati sulla suite numerata

È stata eseguita una seconda passata sui **1008** file numerati con `.expect` in modalità diagnostica non-STRICT usando soltanto contatori già esistenti (`BABYC_COV=1`, `BABYC_VM_PROFILE=1`, `BABYC_PATH=linear`, `BABYC_ACCEL=1`). Non è stata aggiunta nuova strumentazione al sorgente. Gli output del run profilato, normalizzati rimuovendo le righe di contatori/profilazione (`[babyc-cov]`, `[babyc-exec-profile]`, `[babyc-bytecode-vm-prod]` e gli altri tag diagnostici), hanno **0 differenze** dagli `.expect`.

| Contatore / evento | Valore osservato | Interpretazione |
|---|---:|---|
| Corpi Linear eseguiti (`fncode_body` / `profile_body_runs`) | 9.669 | Esecuzioni dinamiche del bytecode/Linear body, non funzioni staticamente distinte. |
| Tentativi body VM (`profile_attempts`) | 10.735 | Include tentativi non conclusi con una run Linear. |
| Fallback Tree effettivi (`tree_body`) | 36 (misura storica Fase 1) | Valore della profilazione iniziale, precedente alla separazione tra policy STRICT e capability Linear e alla correzione della visibilità del contatore. Non descrive lo stato del codice Fase 2. |
| File di test con almeno un fallback Tree | 34 (misura storica Fase 1) | 34 dei 1008 processi allora testati avevano `tree_body > 0`; questa è la baseline diagnostica, non il risultato attuale. |
| Istruzioni Linear eseguite (`lin_inst`) | 324.567.469 | Totale dinamico del contatore per l'intero corpus profilato. |
| Rifiuti pointer (`profile_reject_ptr`) | 939 | Conteggio delle decisioni/rifiuti durante tentativi, non equivalentemente 939 fallback Tree. |
| Compilation failures (`profile_compile_fail`) | 38 | Corpi che non hanno prodotto un FnCode utilizzabile in tentativi profilati. |
| Ineligible cache/body (`profile_ineligible`) | 26 | Casi già classificati non idonei, non necessariamente una chiamata completata via Tree. |
| Diagnostici `compile_reject=lower` | 241 | Occorrenze diagnostiche durante compile/probe; possono ripetersi e non equivalgono a 241 test falliti. |

Il dettaglio per test e per rifiuto è allegato in [`docs/reports/LINEAR_COVERAGE_RUNTIME_COUNTERS_2026-10-19.md`](reports/LINEAR_COVERAGE_RUNTIME_COUNTERS_2026-10-19.md) e nel CSV per test [`docs/reports/LINEAR_COVERAGE_RUNTIME_COUNTERS_2026-10-19.csv`](reports/LINEAR_COVERAGE_RUNTIME_COUNTERS_2026-10-19.csv). La lista dinamica di fallback identifica 34 test; i principali osservati includono `024_type_coercion.c`, `062_initializers.c`, `410_member_cache_call.c`, `666_fp_member_index.c`, `841_real_a08_text.c` e `846_real_a11_bfs.c`. Per ciascuno, il report dà il numero effettivo di corpi inviati a Tree.

**Limiti di questo conteggio:** è un campionamento dinamico del corpus di regressione corrente, non un C application corpus indipendente e non una coverage AST edge/branch. Il processo per test azzera i contatori; corpi staticamente presenti ma mai chiamati non compaiono. `tree_body` misura i fallback eseguiti, mentre `compile_reject`, `reject_ptr` e gli altri contatori misurano tentativi/decisioni e non devono essere sommati come se fossero fallback distinti. Strict matrix è una verifica separata della correttezza e non usa questo passaggio non-STRICT per dichiarare copertura totale.

### Matrice regressione verificata in questa fase

| Configurazione | Passed | Failed | Timeout | Esito |
|---|---:|---:|---:|---|
| Tree, ACCEL 0 | 1008 | 0 | 0 | PASS |
| Tree, ACCEL 1 | 1008 | 0 | 0 | PASS |
| Linear STRICT, ACCEL 0 | 1008 | 0 | 0 | PASS |
| Linear STRICT, ACCEL 1 | 1008 | 0 | 0 | PASS |

Log della prima matrice sul candidato in `docs/reports/coverage_matrix_{tree_a0,tree_a1,linear_a0_strict,linear_a1_strict}.log`. Nella ricostruzione fresca del pacchetto, Tree ACCEL 0 ha avuto un primo esito intermittente negativo solo sul test temporizzato 535 (1007/1008), poi il rerun completo ha passato 1008/1008; Tree ACCEL 1 e Linear STRICT ACCEL 0/1 hanno passato 1008/1008 al primo tentativo. I log di tutte queste esecuzioni, inclusa la prima anomalia, sono conservati nel report `docs/reports/LINEAR_COVERAGE_FINAL_RECONSTRUCTION_2026-10-19.md`. Build GCC pulita: 0 diagnostici `warning:` e 0 `error:`.

## 10. Cosa è stato verificato per questa mappa

- Snapshot ricostruita dalla baseline e delta cumulativo 126 con tutte le cancellazioni di `DELETE_FILES.txt` applicate.
- La mappa è derivata dall'enum IR reale e dai gate in `ir_compile.c`, `ir_lin_emit.c`, `ir_lin_compile.c`, `ir_lower.c` e dal tipo/runtime condiviso; non sono state modificate semantiche del linguaggio in questa fase.
- Il gate di regressione eseguito in questa fase è Tree 1008/1008 e Linear STRICT 1008/1008 con ACCEL 0 e 1. La passata dinamica non-STRICT registra 36 fallback Tree in 34 test. Una matrice verde non dimostra completezza del C: molte combinazioni type×operator e feature non raggiunte da quei test sono elencate sopra come condizionali o gap.
- Non è stata eseguita una build MSVC in questa fase; la compilazione GCC non certifica l'ABI su LLP64, le larghezze `long`, il layout dei bitfield o il packing varargs Windows.

## 11. Fase 3 — promozioni intere, conversioni aritmetiche usuali e signedness

**Stato aggiornato al 2026-10-19, dopo i test 1018–1023.** Questa sezione prevale sulle valutazioni provvisorie più vecchie delle righe “integer promotions” e “narrow assignment” sopra; non trasforma il campione di test in una prova di conformità completa.

### Regola condivisa introdotta

`src/ir_ops.c` ora centralizza il rank/segno dei tipi interi e le usual arithmetic conversions con:

- `IrIntegerPromotionBase()` — promozione di `char`, `short` ed enum al tipo intero promosso;
- `IrUsualArithmeticCommonIntegerBase()` — selezione del tipo comune usando rank, signedness e larghezza host del tipo;
- `IrExprBinaryUsesUnsigned()` e `IrExprBinaryUsesWide()` — inferenza del tipo effettivo dell'operazione, con trattamento distinto degli shift (il tipo promosso del solo operando sinistro decide il segno/width dell'operazione).

`src/expression.c` usa la stessa regola per l'operatore binario condiviso; `src/ir_lin_emit.c` usa i due helper per impostare `IR_LIN_F_UNSIGNED` e `IR_LIN_F_WIDE`. Questo sostituisce l'euristica precedente “uno qualunque degli operandi è unsigned/wide” e rimuove la scorciatoia che forzava alcuni cast a unsigned-wide. L'obiettivo è evitare che il tipo di trasporto a 64 bit o il flag di un solo operando determini la semantica del C.

### Nuovi microtest confrontati con GCC `-O0`

Tutti i sei sorgenti e i rispettivi `.expect` sono stati compilati/eseguiti con GCC `-std=c11 -O0` sullo stesso host; l'output normalizzato coincide esattamente con il `.expect`. Le espressioni in 1019 che fanno right-shift di un valore signed negativo verificano il comportamento GCC della piattaforma corrente (implementation-defined in C), non una regola portabile tra tutte le ABI.

| Test | Copertura specifica |
|---|---|
| `1018_integer_promotions_signedness` | `char`, `signed char`, `unsigned char`, `short`, `unsigned short`; promozioni; confronti mixed signed/unsigned per `int`, `long`, `unsigned int`, `unsigned long`; ternario. |
| `1019_integer_shift_divmod` | shift su bit alti unsigned, shift signed negativo, shift a larghezza `unsigned long long`, divisione/modulo con segni misti e operandi unsigned. |
| `1020_integer_narrow_call_return` | troncamento/estensione in assegnamento e cast, passaggio argomenti, ritorno da funzione, conversioni di `short` e `unsigned long`. |
| `1021_unsigned_wrap_compound` | wraparound unsigned, `+=`, `<<=`, `>>=`, `/=`, `%=` su tipi stretti e signed. |
| `1022_integer_conditional_conversions` | `?:` con rank e segni differenti, conversione del tipo del risultato, confronti mixed signed/unsigned. |
| `1023_integer_bitfields` | bitfield signed/unsigned di 5 e 9 bit, aggiornamenti composti, divisione e shift, restringimento al campo. |

### Validazione di questa fase

- Build Linux/GCC pulita: **0 warning, 0 errori** (log `build_clean.log` e `build_clean_retry.log`).
- Matrice numerata: **1015/1015** in Tree ACCEL 0/1 e Linear STRICT ACCEL 0/1.
- Linear non-STRICT con `--require-no-tree-fallback`: **1015/1015**, `tree_body=0`, `violations=0` sia ad ACCEL 0 sia ad ACCEL 1.
- Il test del percorso `--log` esterno al repository è passato con exit code `0`; il runner stampa il percorso assoluto quando `relative_to(project)` non è applicabile (`tools/run_numbered_matrix.py`).
- Nessun `.expect` del corpus storico `001`–`1017` è stato modificato. Nel sandbox gli `1018` e `1019` erano presenti come bozze iniziali: i loro `.expect` sono stati riallineati all’output GCC `-O0`; `1020`–`1023` sono stati verificati contro GCC. L’overlay cumulativo include la batteria `1018`–`1023`; `1015`–`1017` erano già presenti.

Log della matrice: `docs/reports/phase3_integer_matrix/`. Campioni A/B alternati contro il binario 047 di riferimento: `docs/reports/phase3_integer_ab_samples.csv`.

### Gap 1: parzialmente chiuso, non esaustivo

La selezione del tipo comune è ora condivisa e coperta da casi rappresentativi; **non** dichiaro chiusa ogni combinazione operator×tipo C. Restano da affrontare/validare esplicitamente:

1. Rank e rappresentabilità per target con padding/precisioni non standard: l'helper usa `sizeof` come proxy di larghezza e non un modello completo dei value bits del target.
2. Tipo effettivo di tutte le espressioni: `IrExprIntegerBase()` ricostruisce bene identificatori, cast, dereference, index, costanti, unary, binop, ternario e assegnamento; i rami complessi/member/call e le forme cui manca `ValueType` possono ancora richiedere il fallback conservativo a `TypeFlags`.
3. Promozione degli enum con base/range non `int`, plain `char` a signedness ABI particolare, bitfield con larghezze/layout dipendenti dall'ABI e narrow conversions su tutti i percorsi di storage/memoria.
4. `long` LP64 contro LLP64/ILP32 e layout varargs: i test sono stati eseguiti su Linux/GCC, non su MSVC/Windows.
5. Shift con conteggio negativo/o >= larghezza, overflow signed e `INT_MIN / -1` hanno comportamento undefined o non portabile in C; questi casi non vanno trasformati in una falsa aspettativa universale. Servono test separati per i diagnostici didattici di BabyC, distinti dal confronto byte-for-byte con GCC.
6. Conversioni intere che attraversano float/double, ABI varargs reali (`va_arg`), initializer designati, aggregate e tutte le combinazioni operator×tipo restano fuori dalla copertura completa di questo micro-set.

Prossimo passo raccomandato: generare test guidati dalla matrice del tipo risultante (per ogni coppia rank/sign e ogni operatore), confrontare l'output GCC soltanto per casi definiti o implementation-defined fissati esplicitamente alla piattaforma, poi aggiungere un target model esplicito per i value bits invece di estendere euristiche `sizeof`.


## Addendum Phase 4 (2026-10-19): integer model and differential probes — targeted failures closed

- `src/platform.h` exposes the central integer-width model (`BABYC_VALUE_BITS_CHAR`,
  `BABYC_VALUE_BITS_SHORT`, `BABYC_VALUE_BITS_INT`, `BABYC_VALUE_BITS_LONG`,
  `BABYC_VALUE_BITS_LLONG`, `BABYC_VALUE_BITS_POINTER`) and
  `BABYC_PLAIN_CHAR_IS_SIGNED`. LP64 and LLP64/ILP32 widths derive from the target
  compiler ABI; no scattered ABI-specific `#ifdef`s were added in this closure.
- The shared IR integer model uses these widths for storage and promotion decisions.
  The fused Tree relational evaluator now consults the same canonical usual-arithmetic
  conversion helper as the general evaluator, fixing mixed `int`/`unsigned int`
  comparisons (test 1026).
- Linear type inference now resolves bitfield width/signedness from the declared
  aggregate member layout before lowering stamps expression metadata. An unsigned
  bitfield whose width fits in `int` promotes to `int` (test 1027).
- Clean Linux/GCC build: 0 warnings, 0 errors. Tree and Linear STRICT each pass
  1019/1019 at ACCEL=0/1; Linear non-STRICT passes 1019/1019 with
  `--require-no-tree-fallback` at ACCEL=0/1, with `tree_body=0`. Tests 1024–1027
  match `gcc -std=c11 -O0` output byte-for-byte.
- The seven-pair alternating performance A/B is recorded in
  `docs/reports/phase4_gap1/perf_ab7_phase3_vs_phase4.json`. Several positive CPU-time medians (up to 14.88%) remain unresolved; short-run timing
  variation makes causality uncertain, but this data does not support a zero-regression claim. Full expression-kind canonicalization
  exactly once during lowering, Windows/MSVC validation, and broader operator×type
  coverage remain open; this is targeted closure of the two reported failures, not
  a claim that every Phase 4 architectural item is complete.


## Canonical expression type annotation (Phase 5, 2026-10-19)

`IrExpr` now carries `CanonicalBase` and `CanonicalFlags`. `IrAnnotateExpr` walks each expression bottom-up after child annotation and stamps the canonical type once; the shared integer-base resolver consumes that cache before using its legacy inference path for IR nodes that cannot yet be classified. The annotation covers constants, identifiers, member/index access, unary/cast/increment forms, binary/relational/shift/comma forms, assignments, ternaries, and calls with resolvable return signatures. Integer usual arithmetic conversions use the existing `IrIntegerPromotionBase` and `IrUsualArithmeticCommonIntegerBase` routines and the target-width model.

New differential fixtures: `1028_canonical_type_forms.c` and `1029_canonical_comma_enum_char.c`; their `.expect` files were generated with `gcc -std=c11 -O0`. All six numbered-test configurations pass 1021/1021; Linear non-STRICT fallback gates report `tree_body=0`.

**Remaining architectural work:** legacy inference helpers (`IrExprDeclaredBitField`, `IrExprBinaryUsesUnsigned`) remain as compatibility fallbacks for incomplete/unbound type metadata; this turn does not claim their complete removal. In particular, type recovery for nested pointer/member chains and all call-return/aggregate combinations needs broader targeted coverage before the canonical cache can be considered the sole source of truth for every possible expression shape.

## Addendum Phase 6 — canonical type as sole operator-decision source (2026-10-19)

`IrAnnotateExpr` now assigns both the expression result type (`CanonicalBase/CanonicalFlags`) and the promoted/common operation type (`CanonicalOpBase/CanonicalOpFlags`) bottom-up. Tree evaluation and Linear emission read those fields for signedness/width; `IrExprBinaryUsesUnsigned`, `IrExprBinaryUsesWide`, `IrExprDeclaredBitField`, `IrExprIntegerBase`, and the legacy fallback inference in `ir_ops.c` have been removed. Synthetic address/member nodes created during lowering carry the canonical metadata from their known source/type at construction. Target widths use `BABYC_VALUE_BITS_INT`, `BABYC_VALUE_BITS_LONG`, `BABYC_VALUE_BITS_LLONG`, and `BABYC_VALUE_BITS_POINTER` from `platform.h`.

The matrix runner sets `BABYC_CANONICAL_GATE=1`. Each process reports its counter, the summary includes `CANONICAL_TYPE_GATE`, and the runner fails if any process lacks a report or any evaluated/emitted node lacks `IR_CF_VALID`. Verified snapshot: 1023/1023 in Tree and Linear STRICT at ACCEL 0/1; Linear non-STRICT 1023/1023 at ACCEL 0/1 with `tree_body=0`; canonical invalid-node count 0 in all six runs. Tests 1030 and 1031 add GCC `-std=c11 -O0` differential coverage. Full logs and benchmark A/B are in `docs/reports/phase6/`. A/B is mixed/noisy and the Windows/MSVC build remains unverified; see `PHASE6_CANONICAL_TYPE_REPORT.md`.

### Gap 2 ABI verification update (2026-10-19)

Tests `1032`–`1038` were added with GCC `-std=c11 -O0` expectations. Fixed-parameter scalar/FP/pointer calls, function-pointer callbacks, tested default promotions, and `printf` formats `%d %ld %lld %u %lu %x %f %lf %s` pass across Tree and Linear. User-defined varargs remain unsupported: tests `1035` and `1037` fail at `va_start` (`undefined` in Tree; body fallback in Linear STRICT; `tree_body=1` each under Linear non-STRICT). The six-mode suite therefore reports 1028/1030 in each configuration, with canonical invalid-node count zero. Gap 2 remains open until the runtime has a shared typed variadic-call frame plus `va_start`/`va_arg`/`va_copy`/`va_end` semantics and Linear non-STRICT reports `tree_body=0`. See `GAP2_CALL_ABI_REPORT_2026-10-19.md` and `docs/reports/gap2/` for exact logs and A/B samples.


## Gap 2 variadic ABI implementation status (2026-10-19)

The shared `CallAbiFrame` metadata is now populated by the Tree and IR fallback call paths, and IR fallback allocation reserves the supported 32-argument variadic tail. This is infrastructure only: `va_start`/`va_arg`/`va_end`/`va_copy` are not implemented, so tests 1035 and 1037 remain failing and Linear no-fallback coverage is not closed. See `GAP2_VARIADIC_IMPLEMENTATION_STATUS_2026-10-19.md`.
