# Sessione 2026-10-19 — chiusura `126_csmith_modulo_zero` in Linear STRICT

- Dump `BABYC_DUMP_LIN=1` iniziale: rifiuto sulla formazione dell'indirizzo `&g_126[1][3][2]` in array globale tridimensionale; i passaggi successivi hanno isolato anche initializer pointer-array, operatore virgola e argomento `*ptr` scalare.
- Fix generali in `src/ir_lin_emit.c` e `src/ir_lin_compile.c`: stride row-major da tipo dichiarato, initializer di array di puntatori con riuso dei temporanei, semantica sequenziale `TokenComma` con invalidazione CSE, dereferenziazione scalare/puntatore come argomento per valore. Nessuna condizione specifica per nome test e nessun fallback.
- Nuovo test `1016_global_multidim_addr_comma_ptr_init.c/.expect`; nessun `.expect` precedente modificato. Il `126` arriva ora al medesimo `modulo by zero` di Tree.
- Clean build Linux/GCC 0 warning/errori. Matrice candidata: Tree 1008/1008 e Linear STRICT 1008/1008 a ACCEL=0/1.
- A/B vs binario 047: 7 coppie AB/BA per le otto combinazioni; `dfs_rec` ACCEL=1 esteso a 21 coppie. Output hash e dump IR identici. Le misure brevi sono rumorose; report e CSV conservano mediane e campioni. MSVC non eseguito.
- Report: `docs/reports/LINEAR_CSMITH_MODULO_126_CLOSURE_2026-10-19.md`; log matrice, build pulita, dump post-fix, CSV A/B e confronto IR in `docs/reports/`.

---

# Sessione 2026-10-19 — chiusura `047_grep` in Linear STRICT

- Ricostruita la baseline `loop_promotion` applicando il delta cumulativo 651 dopo `DELETE_FILES.txt`; la diagnosi è stata riprodotta con `BABYC_DUMP_LIN=1`, tracing temporaneo dell'eleggibilità e del gate puntatori.
- Root cause generale: `IrExprIntegerOk()` vietava `argv[i][j]` quando la radice era un parametro, lowering risolveva prematuramente global assenti in rami non eseguiti, e `FnCodeIndexedPointerParamsSafe()` rifiutava l'add pointer-plus-offset emesso prima del load indiretto tipizzato. Corrette le tre regole strutturali senza nomi/test speciali e senza fallback.
- Nuovo test `1015_param_ptrptr_index.c/.expect`; nessun `.expect` esistente modificato. Build Linux/GCC da clean state: 0 warning/errori.
- Matrice sulla ricostruzione: Tree 1007/1007; Linear STRICT 1006/1007 a ACCEL=0/1, unico residuo `126_csmith_modulo_zero`. Il dump del 126 mostra una reiezione separata su espressione binaria Csmith/modulo-zero.
- A/B ha almeno 7 coppie alternate per tutti gli 8 benchmark/configurazioni e follow-up a 36 coppie per i benchmark/configurazioni inizialmente sfavorevoli. Output hash identici e IR emesso invariato per tutti i benchmark. Le misure hanno però elevata dispersione anche in A/A stesso-binario: il report non afferma overhead zero dimostrato e conserva i numeri sfavorevoli.
- Report: `docs/reports/LINEAR_GREP_047_CLOSURE_2026-10-19.md`; CSV wall-time, CPU-time, A/A, log di matrice e dump sotto `docs/reports/`.

---

# Sessione 2026-10-19 — chiusura `058_macro_bug` in Linear STRICT

- Ricostruito il candidato applicando i 2.023 path di `DELETE_FILES.txt` alla baseline loop_promotion e sovrapponendo il delta initializers_062; nessun path eliminato mancava.
- Riprodotto il dump originale: la riga 10 `z = MIN(y, 13.5)` terminava con `lower_reject`, `compile_reject=lower` e `[vm-rej] compile-null`, quindi STRICT cadeva in fallback.
- Fix generale nei path IR/Linear: separata la classificazione FP del valore dall'annotazione FP propagata; abilitato lowering ternario FP e relazioni con promozione degli operandi misti; aggiunte conversioni scalari condivise per assegnamenti e inizializzatori; executor Linear compara correttamente double IEEE per le sei relazioni. Nessun caso speciale sul nome test.
- Nuovo test `1013_conditional_fp_mixed.c/.expect`; tutti gli `.expect` già esistenti sono byte-identici rispetto al delta d'ingresso.
- Build Linux/GCC pulita: 0 warning/errori. Matrice sulla copia ricostruita dallo ZIP finale: Tree 1005/1005, Linear STRICT 1002/1005 a ACCEL 0 e 1. Restano soltanto 047, 126, 651; nessun timeout.
- A/B Linear con output/exit verificati: 7 coppie AB/BA per ogni configurazione dei 4 workload; due scan workload ripetute a 21 coppie. I campioni sfavorevoli sono conservati; i follow-up non mostrano un rallentamento riproducibile oltre la dispersione, ma non provano costo esattamente nullo.
- MSVC non disponibile in questo ambiente; il codice aggiunto non usa header POSIX né estensioni GCC. Bytecode fuori ambito.
- Dettagli: `docs/reports/LINEAR_MACRO_058_CLOSURE_2026-10-19.md`; report perf/CSV con i campioni completi.

---

# Aggiornamento sessione — 19/10/2026: chiusura struct return → array locale

- Causa provata per `893_vm_struct_return_effect_fallback`: la dichiarazione `struct S v[1]` non inizializzata aveva `DeclArraySize=1`, `DeclIsPtr=1`, dimensione elemento 8, ma il lowering dell’allocazione locale ammetteva soltanto `DeclArraySize > 1`. Il body veniva respinto prima di eseguire la copia del blob restituito nell’elemento. `skipped_call=1` era un contatore aggregato fuorviante: le tracce mostrano che `make_effect` e `printf` erano entrambi ammessi.
- Fix generale in `src/ir_lin_compile.c`: ammettere la lunghezza 1 solo per un array puntatore non inizializzato senza initializer/count/values/expressions, preservando la copia esistente dei temporanei aggregati inizializzati. Un primo predicato più largo aveva causato regressioni in bitfield/struct-return; è stato ristretto e verificato prima della matrice finale.
- Output Tree/Linear STRICT, ACCEL=0/1: `7 1 1`; dump prima/dopo e metadati in `docs/reports/LINEAR_STRUCT_RETURN_ARRAY_CLUSTER_2026-10-19.md` e file `struct_return_array_*`. Nessun `.expect` esistente modificato; non serve un nuovo test perché `893` esercita esattamente la sintassi corretta.
- Build GCC pulita, zero warning/errori. Matrice: Tree 1002/1002 a ACCEL=0/1; Linear STRICT 996/1002 a ACCEL=0/1; sei residui (`047`, `058`, `062`, `126`, `651`, `873`), zero timeout/regressioni nuove.
- A/B contro la baseline loop_promotion: 7 coppie alternate AB/BA per tutte le otto combinazioni; ACCEL=1 esteso a 21 coppie. Output identici. Tutte le ACCEL=0 migliorano nelle mediane appaiate; per le workload accelerate i risultati più brevi sono rumorosi, ma non emerge peggioramento persistente >1% oltre la dispersione misurata. CSV `docs/reports/struct_return_perf_ab_*.csv`.
- MSVC/Windows non compilato in questo turno; bytecode fuori ambito.

---

# BabyC – Note di sessione (18/10/2026)

Obiettivo: rendere BABYC_PATH=linear semanticamente completo, con BABYC_STRICT=1 (nessun fallback al tree) come requisito. Il tree resta l'oracolo.

## Numeri della suite
| run | tree ok | linear STRICT ok | fallback | mismatch |
|---|---|---|---|---|
| baseline originale | 973 | 913 | 55 | 5 |
| d13 | 985 | 958 | 27 | 0 |
| d16 | 987 | 962 | 25 | 0 |
| d19 | 989 | 969 | 20 | 0 |
| **finale (d24)** | **990/990** | **971** | **19** | **0** |
(28 skip invariati. L'aumento dei casi tree ok viene dai nuovi test, che passano tutti.)

## Cause radice e correzioni (principali)
- **Chiamate a funzioni non dichiarate**: in linear ora passano per il lookup condiviso IrCalleeLookupOrFail; il caret dell'errore è deterministico anche nel tree (cluster 593, 1030).
- **Chiamate utente con più di 8 argomenti**: nel tree restituivano 0. Corretto anche nella baseline.
- **Globali double/float in linear**: davano 0.00. Corretto.
- **Ritorno double da una funzione IR verso un chiamante token**: dava 0.00. Corretto.
- **printf in linear**: ora accetta fino a 16 argomenti.
- **Parametri long long in linear**: la larghezza era a 32 bit. Ora ValWidth del parametro è derivato dalla sua dimensione con IrVwFromSize (1074, 1077).
- **Regressione perf in linear**: i programmi che prima andavano in fallback al tree perdevano i kernel del tree. Ho promosso le catene condivise IrTryFastWhileChain/IrTryFastForAfterInit, emesse in linear come kernel IR_K_TREE_WHILE/FOR_PRE/FOR tramite EmitTreeLoopTry.
- **Argomenti int passati a funzioni math host** (es. `sqrt(k)`): il valore arrivava grezzo. Ora c'è la conversione da prototipo IrHostCoerceParams, in tree e in linear. Math e ctype ora sono anche host linear (IrLinHostMathCtype).
- **printf("%s", text+2) nel tree**: falso mismatch di formato. Ora IrExprYieldsPointer riconosce il BinOp.
- **Aritmetica di puntatori annidata in linear**: veniva fatta come sottrazione stretta. Ora usa il classificatore ricorsivo LinOperandIsGenuinePtr.
- **`T *a[N]`** (1080):
  - nel tree a[i][j] usava passo 1 (char) e `a[i]+n` non era scalato;
  - in linear a[i][j] veniva rifiutato.
  - Ora il tipo puntato dichiarato (PointeeType) viene marcato all'annotate per locali, parametri e globali.
  - Il linear ha i percorsi di lettura e scrittura a[i][j] per elementi di 1/2/4/8 byte, puntatori e double (con conversione int→double).
  - `p - q` e `p ± n` usano il passo dichiarato in entrambi i path.
- **Cast a puntatore `(T *)x`** (1081): l'annotate lo tipizzava come T. Così `(double*)malloc(..)` diventava un double: il linear convertiva l'indirizzo in bit FP e andava in **SEGV**, anche nella baseline. `(char*)p` diventava un int a 8 bit. Ora il risultato è sempre PTR, con ElemSize uguale alla dimensione puntata.
- **`double *q` locale nel tree**: q[i] stampava i bit IEEE come intero (bug della baseline). Ora l'elemento è FP. Inoltre `double **` non viene più scambiato per FP: DeclFromType è il primo livello puntato.
- **calloc/realloc**: aggiunti agli host linear.
- **Limite della dimensione del corpo linear**: alzato da 384 a IR_LIN_STMT_MAX-64 istruzioni. Il limite precedente era un residuo storico: il buffer è di 2048 e la suite non va in crash. Chiude 792.

## Test aggiunti
1064–1081, tutti con expect verificato con gcc dove possibile. Alcuni esempi:
- 1074/1077: parametri wide
- 1078: conversione da prototipo math
- 1079: aritmetica di puntatori negli argomenti host
- 1080: `T *a[N]` tipizzato
- 1081: cast a puntatore, double* e calloc/realloc

Passano tutti in tree e in linear STRICT.

## Performance
Script bench.py, mediana di 5 run interleaved, base contro nuovo binario.
- **Kernel ptrscan a tabella hitmap[256]**: tree string_scan -40% circa.
- **Linear**: string_scan -60%, cat_global_index -58%, matmul -50%, fib_iter e dfs_rec molto migliori, grazie alle catene di kernel condivise.
- **Rumore**: il resto oscilla tra ±5 e 10%. Nell'ultimo run alcune voci sono a +6/+12% (gcd lin, struct_score lin_noa, cat_param_read tree). La ri-misura mirata è stata interrotta per la consegna: da verificare.
- **Residuo noto**: lin_noa string_scan è +42% (il generico linear è più lento del tree senza acceleratori).

## Generalizzazioni / funzioni promosse o condivise
- IrChargeLocalArrayOrFail
- IrBraceInitPlan / IrPackStructBraceInit
- IrFailReadOnlyAssign
- IrExprPtrValuedTF
- IrLvalueRootConstTF
- IrCalleeLookupOrFail
- ParserSetCaretOnStmtEnd
- IrValueStoreBits
- copie bounded delle call-table
- loop generico dei dichiaratori struct
- IrVwFromSize: sostituisce 6 catene if duplicate
- IrTryFastWhileChain / IrTryFastForAfterInit + EmitTreeLoopTry: kernel condivisi tra tree e linear
- IrHostCoerceParams: rimossa la copia duplicata IrPackFpArg, usata IrPackFPArg
- IrLinHostMathCtype
- LinOperandIsGenuinePtr: sostituisce la classificazione sinistra/destra duplicata
- LinPtrOperandStride: passo condiviso tra ptr-ptr e ptr±int
- IrLowerPtrRowPointee / IrCompileIdentElemPointee (con fallback sulla GlobalTable) / IrCompileSetElemPointee / IrExpr.PointeeType
- IrPtrElemSizeOf, esteso al BinOp ± ricorsivo e all'Index con PointeeType

## Matrice di copertura dei costrutti C (linear STRICT)
| Costrutto | Stato |
|---|---|
| aritmetica int/unsigned 8–64 bit, parametri wide | OK |
| FP double: locali, globali, ritorni, argomenti printf | OK |
| float come elemento puntato in `T *a[N]` | fallback (residuo) |
| puntatori: deref, ±n, p-q, annidati, cast `(T*)` | OK |
| `T *a[N]`: a[i][j] lettura e scrittura (int/short/char/unsigned/double/ptr) | OK |
| array multidimensionali, `T *a[N][M]` | OK |
| struct: membri, brace-init, bitfield | OK |
| `&s.arr[i]` (379/943), `n.wide.vcount++` (942) | fallback |
| ++/-- su membro come argomento di chiamata (610/622) | fallback |
| decl `char *names[]={a,b}` (785) | fallback |
| ternario FP via macro (057) | fallback |
| while/for/do, break/continue, switch | OK |
| chiamate utente >8 argomenti, ricorsione | OK |
| host: stdio, string, memoria (malloc/calloc/realloc/free), math, ctype | OK |
| qsort/bsearch con callback (1033/1034), strtol (1061), sqlite varargs (661) | fallback |
| struct return con effetti (959), dispatch fptr complesso (441, 613) | fallback |

## Residui
- 19 fallback STRICT, elencati sopra (046, 057, 061, 1031, 1033, 1034, 1061, 121, 280, 379, 441, 610, 613, 622, 661, 785, 942, 943, 959).
- Path token (NO_IR): aritmetica unsigned long long / long errata (preesistente). Bug FP nel token-loop.
- Serve un tipo statico dell'espressione unico. Oggi il tipo è ricostruito da TypeFlags/ElemSize/PointeeType.
- Elementi puntati float in `T *a[N]` in linear.
- Perf lin_noa string_scan. Da ri-misurare le piccole oscillazioni dell'ultimo bench.

## 2026-10-19 — Linear member/call residual cluster

Closed Linear STRICT tests `606_storm_inc_dec_member_call`, `609_storm_enum_union_fptr`, `618_inc_member_call_arg`, and shared `454_complex_dispatch`. Root causes were pointer-to-pointee type metadata for `->`, function-designator identity encoding for `&fn`, and syntax-only rejection of valid scalar/function-pointer call arguments. The generalized indirect pre/post-increment store now uses `IrLinStoreDeref` so source-position packing does not add a branch to the shared indexed-store opcode. No `.expect` changes/new tests. Matrix now Tree 1001/1001 and Linear STRICT 992/1001 at ACCEL=0/1, with nine residuals; see `docs/reports/LINEAR_MEMBER_CALL_CLUSTER_2026-10-19.md`.


## 2026-10-19 — `873_alignment_extended` Linear STRICT closure

- Root cause: `n.wide.vcount++` (line 16) was rejected by the nested member increment path. The type walk required a pointer intermediate and lacked global aggregate root resolution; embedded struct members were misclassified as unsupported.
- Fix in `src/ir_lin_emit.c`: support embedded aggregate member chains by resolving their declared types and obtaining the parent member address through the shared safe-member operation, then typed load/add/store. Pointer-valued parent chains keep their previous value-loading behavior.
- Added test 1011 `nested_embedded_volatile_inc` (+ `.expect`), covering global/local nested aggregates and prefix/postfix increment. No existing `.expect` changed.
- Matrix: Tree 1003/1003; Linear STRICT 998/1003 at ACCEL=0/1. Remaining: 047, 058, 062, 126, 651.
- A/B baseline-vs-candidate: seven alternating pairs in all eight combinations; ACCEL=1 `string_scan_bounded` and `struct_score` extended to 21 pairs after initial raw median delta >1%; outputs identical, no final raw median regression >1%.
- MSVC/Windows not run; bytecode out of scope.


## 2026-10-19 — Linear STRICT 062 initializers

- Fixed pointer-index eligibility so unreferenced pointer parameters do not reject an otherwise valid lowered function; referenced pointer parameters retain existing safety checks.
- Folded `sizeof(global object/array)` from declared type and added a dedicated Linear row-decay load mode for `global_2d[i]`.
- Closed `062_initializers`; added `1012_global_array_size_row_decay`; pre-existing `.expect` files unchanged.
- Matrix: Tree 1004/1004; Linear STRICT 1000/1004 for ACCEL=0/1; remaining 047, 058, 126, 651.
- A/B: seven alternating pairs across four benchmarks and both ACCEL modes; no raw median regression above +1%.


## 2026-10-19 — Linear STRICT `651_sqlite_exec_null_varargs` closure

- Root cause: `LowerStmtRec` accepted the body, but calls to registered intrinsics were rejected by the emitter host-name whitelist (`skipped_call=1`, `compile_reject=lower`, `vm-rej: compile-null`). These intrinsics implement the shared `Value[]` ABI and are callable through `IrCallHostFromRegs`; the whitelist should constrain unresolved host names, not resolved registered intrinsics.
- Changed `src/ir_lin_emit.c` to allow resolved `Intrinsic` functions through the existing generic host ABI. No SQLite/test-name fast path and no fallback added.
- Added `1014_intrinsic_varargs_null.c/.expect`, covering varargs tails passed as both integer zero and `NULL`. No existing `.expect` changed.
- Matrix on fresh reconstruction: Tree 1006/1006 on ACCEL=0 and 1; Linear STRICT 1004/1006 on both, residuals only `047_grep`, `126_csmith_modulo_zero`. 651 and 1014 pass in all four modes.
- Performance A/B against delta 058: 21 alternating pairs per each of eight configurations, extended to 36 pairs for five configurations whose initial raw median was adverse. Output hashes identical. Paired medians −2.55%..+1.66%; IQRs span zero and outliers show substantial host noise. No consistent regression found; zero cost cannot be asserted as proven.
- GCC/Linux clean build, 0 warnings/errors. MSVC not run; modified code uses no POSIX-specific header/GCC extension. Report and full samples: `docs/reports/LINEAR_VARARGS_INTRINSIC_NULL_651_CLOSURE_2026-10-19.md`, `docs/reports/linear_varargs_651_perf_ab_samples_2026-10-19.csv`.
## 2026-10-19 — Linear C coverage map phase 1

- Added `docs/LINEAR_COVERAGE_MAP.md`, code-derived from the live `IrExprKind`/`IrStmtKind` enums, lowering/preflight and runtime sources. It itemizes expression/statement nodes, every operator token and material type/ABI combinations, explicit rejects/gaps, and a ranked closure roadmap. No lowering/runtime source or existing `.expect` file changed.
- Ran the strict regression matrix: Tree 1008/1008 and Linear STRICT 1008/1008 at ACCEL 0 and 1. Clean Linux/GCC build: 0 warnings/errors.
- Reused existing `BABYC_COV`/`BABYC_VM_PROFILE` diagnostics over all 1008 numbered cases in non-STRICT Linear: 0 normalized output differences; 9,669 Linear body runs, 36 actual Tree-body fallbacks in 34 test processes. Added report and per-test CSV under `docs/reports/`.
- MSVC and external application-corpus coverage remain unverified; this phase maps gaps and does not claim complete ISO C coverage.

## 2026-10-20 — attempt17: canonical argument types and single-evaluation root fixes

Retained attempt16's static-type native argument packing. Reconstructed complete fixed-size local array types from declaration metadata in `ir_compile.c`, made escape bookkeeping reject side-effecting/non-local member paths before resolving them, and fixed Linear VM variable-index struct-array member decay to lower/pass the index without a second AST evaluation. Added numbered tests 1066/1067. Clean GCC build has zero warnings; Tree is 1051/1051 in A0/A1. Linear STRICT and no-fallback gates are 1050/1051 because 1066 currently triggers a strict Tree fallback. No seven-pair A/B result or MSVC result is claimed. See `GAP2_VA_ARG_ROOT_FIX_REPORT_2026-10-20_attempt17.md`.


## attempt18 — pointer dereference / canonical type root fixes (2026-10-20)
- Added full local pointer declaration type stamping and type-aware Linear pointee recovery; retained attempt16 native-call packing single-evaluation behavior.
- Corrected integer-plus-pointer arithmetic, multidimensional array row dereference, and initialization state on computed pointer arithmetic values so `(p + n)->member` and `(*(p + n)).member` work.
- Added numbered tests 1068 and 1069.
- Validation: clean GCC build, 0 warnings/errors; 1053/1053 in all six Tree/Linear configurations; canonical gate invalid_nodes=0; Linear non-STRICT tree_body=0.
- Seven alternating A/B pairs per requested benchmark/config completed; outputs equal, but performance ratios are mixed and are documented in `GAP2_VA_ARG_ROOT_FIX_REPORT_2026-10-20_attempt18.md`. MSVC/LLP64 not run.
