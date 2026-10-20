# Stato prioritario — chiusura `047_grep` in Linear STRICT (2026-10-19)

Baseline esclusiva: `babyc_baseline_2026-10-18_loop_promotion.zip`, SHA-256 `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`. Delta cumulativo in ingresso: chiusura 651, SHA-256 `f77e4ef698b4c83df642ecf01a3e3a35f821a0ad0913e9796ba65acbb95a92e6`. Applicare il delta cancellando prima i percorsi di `babyc/DELETE_FILES.txt` e poi sovrapponendo i file.

- **Chiuso `047_grep`:** il corpo `main` viene eseguito in Linear STRICT, senza fallback, con output uguale a Tree e al `.expect` con ACCEL=0/1. La causa era duplice: veto sull'indicizzazione annidata con radice parametro (`argv[1][0]` via `char **`) e lookup globale rigido durante lowering dei rami runtime non eseguiti (`documentation` è esclusa da `#if 0`). La prima correzione ha poi esposto un veto di `FnCodeIndexedPointerParamsSafe()` sull'aritmetica esplicita pointer-plus-offset; il gate ora riconosce la sola forma larga emessa dall'emettitore, mantenendo il controllo per gli altri usi.
- **Nuovo test 1015:** `1015_param_ptrptr_index.c/.expect`, esercita davvero `char **` e `text[0][1]` (output `98`). Nessun `.expect` esistente modificato.
- **Matrice finale ricostruita:** Tree 1007/1007 a entrambi i valori ACCEL; Linear STRICT 1006/1007 a entrambi, unico residuo `126_csmith_modulo_zero`. La causa di 126 è distinta (`lower_reject` su espressione binaria del caso Csmith/modulo-zero), non una variante del problema 047.
- **Prestazioni:** A/B alternato tra il binario 651 e candidato 047, output stdout identici. Almeno 7 coppie per ognuna delle otto configurazioni dei quattro benchmark; `string_scan` ACCEL=0, entrambi `string_scan_bounded` e `dfs_rec` sono stati estesi a 36 coppie. I dump IR sono identici per tutti e otto gli abbinamenti, ma alcune mediane wall-time restano sfavorevoli e il controllo A/A mostra oscillazioni fino a decine di punti percentuali sullo stesso binario. Non è dimostrato un costo runtime/instruction-stream aggiuntivo; nemmeno è corretto certificare costo esattamente zero dalle misure rumorose. Dettagli nel report e CSV sotto `docs/reports/`.
- Build Linux/GCC completata da clean state, 0 warning/errori. Codice senza nuovi header POSIX o estensioni GCC. MSVC/Windows non compilato in questa sessione; compatibilità da verificare localmente. Bytecode compatto fuori perimetro.

Prossimo residuo Linear STRICT: `126_csmith_modulo_zero`, diagnosi autonoma; non modificarlo attribuendogli la causa del 047. Report: `docs/reports/LINEAR_GREP_047_CLOSURE_2026-10-19.md`.

---

# Stato prioritario — chiusura `651_sqlite_exec_null_varargs` Linear STRICT (2026-10-19)

Baseline esclusiva: `babyc_baseline_2026-10-18_loop_promotion.zip`, SHA-256 `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`. Delta cumulativo in ingresso: `babyc_cumulative_delta_2026-10-19_macro_058_closure.zip`, SHA-256 `e2695a8c71f3f4b6e9d2753091853b3ce0b3c3c4c74f714ffc0418ede6ceac18`. Ricostruire cancellando i path in `babyc/DELETE_FILES.txt` e sovrapponendo il delta finale.

- **Obiettivo chiuso:** `651_sqlite_exec_null_varargs` esegue in Linear STRICT senza fallback con ACCEL=0/1; output `rc=0`, identico al `.expect`. Il dump prima del fix mostrava `compile_reject=lower`, `skipped_call=1`, `calls=2`, quindi `[vm-rej] compile-null`. La causa era il gate di whitelist host che rifiutava funzioni riconosciute come Intrinsic registrate (`sqlite3_open`, `sqlite3_exec`, `sqlite3_close`) anche se hanno l'ABI comune `Value[]` invocabile da `IrCallHostFromRegs` e supporta il packing degli argomenti varargs. Il gate ora mantiene la whitelist per host name non risolti, ma consente le intrinseche effettivamente registrate, usando il percorso generico esistente; nessuna eccezione per nome/test e nessun nuovo fallback.
- **Nuovo test 1014:** `1014_intrinsic_varargs_null.c/.expect`; copre `sqlite3_exec` con zeri interi e `NULL` nei parametri opzionali e controlla open/exec/close. Output Tree e Linear: `open=0`, `zero=0`, `null=0`, `close=0`. Nessun `.expect` preesistente è stato modificato.
- Build pulita Linux/GCC: 0 warning/errori. Sulla ricostruzione del delta finale: Tree 1006/1006 con ACCEL=0/1; Linear STRICT 1004/1006 con ACCEL=0/1, zero timeout. Unici residui: `047_grep`, `126_csmith_modulo_zero`. Il fix non è condiviso con le loro cause osservate: non sono stati alterati.
- **A/B prestazioni:** confronto incrementale tra il delta 058 ricevuto e il fix 651 isolato; tutti gli output/hash stdout combaciano. Ogni configurazione ha almeno 21 coppie alternate AB/BA; 5 configurazioni con prime mediane grezze sfavorevoli sono state estese a 36 coppie. Le mediane appaiate sono comprese fra −2,55% e +1,66%; gli IQR includono zero, con range molto ampi su esecuzioni ACCEL brevi. Per esempio `string_scan_bounded` ACCEL=1: mediana grezza +3,58%, mediana appaiata +0,67%, IQR −7,41..+13,69%. Non c'è evidenza coerente di regressione attribuibile al fix, ma non si dichiara provato costo esattamente nullo.
- MSVC/Windows non eseguito in questo ambiente. Il codice modificato non aggiunge header POSIX né estensioni GCC; la build MSVC resta da confermare. Bytecode compatto fuori ambito.

Dettagli e dati grezzi: `docs/reports/LINEAR_VARARGS_INTRINSIC_NULL_651_CLOSURE_2026-10-19.md` e `docs/reports/linear_varargs_651_perf_ab_samples_2026-10-19.csv`.

---

# Storico — chiusura `058_macro_bug` Linear STRICT (2026-10-19)

Baseline esclusiva: `babyc_baseline_2026-10-18_loop_promotion.zip` (SHA-256 `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`). Ogni delta è cumulativo su questa baseline: applicare `babyc/DELETE_FILES.txt`, quindi sovrapporre lo ZIP. Delta ricevuto prima di questo turno: SHA-256 `73267686fc663005e23e1208732f11a3cd1c794795459b99519065db29f173f2`.

- **Obiettivo chiuso:** `058_macro_bug` ora esegue in Linear STRICT senza fallback. Il dump prima del fix indicava `lower_reject stmt_kind=1 line=10 expr_kind=4`, `compile_reject=lower`, `[vm-rej] compile-null`; causa: il gate FP confondeva il flag FP propagato alle relazioni (risultato int) e rifiutava il ternario FP nella macro `MIN(y, 13.5)`. Fix generale su classificazione del tipo-valore, promozione degli operandi relazionali misti, conversioni dei rami ternari e conversioni scalari implicite in assegnazioni/inzializzatori.
- Build pulita Linux/GCC (`make -C src clean && make -C src -j2`, `-Wall -O2`): PASS, zero warning/errori.
- Corpus: 1.013 sorgenti numerate contigue `001..1013`, 1.005 test `.expect` e 8 source-only. Nessun `.expect` preesistente modificato; nuovo solo `1013_conditional_fp_mixed.c/.expect`.
- **Matrice completa sulla copia ricostruita dallo ZIP finale:** Tree 1005/1005 con ACCEL=0 e 1; Linear STRICT 1002/1005 con ACCEL=0 e 1, zero timeout. Residui identici: `047_grep`, `126_csmith_modulo_zero`, `651_sqlite_exec_null_varargs`.
- Output `058_macro_bug` identico a `.expect` dopo la sola normalizzazione CRLF/LF; lo spazio finale significativo è preservato. Non si dichiara uguaglianza dei byte grezzi perché il file `.expect` usa CRLF e il processo emette LF. `1013_conditional_fp_mixed` produce `13.500000 14.000000 2.000000 3` in Tree e Linear STRICT a entrambi i valori ACCEL.
- **Performance:** sette coppie alternate AB/BA su `string_scan`, `string_scan_bounded`, `struct_score`, `dfs_rec` a ACCEL 0/1, confrontando sia baseline storica→candidato cumulativo sia delta ricevuto→fix isolato. I primi 7-pair del confronto incrementale hanno mediane positive sulle due scan workload; ripetizioni a 21 coppie riportano le mediane grezze a +0,94..+1,99% e le mediane appaiate a +0,41..+1,49% (IQR include zero), con accelerati brevi molto rumorosi. Non è provato un rallentamento riproducibile oltre la dispersione, ma questi dati non dimostrano costo esattamente nullo. Campioni completi in `docs/reports/LINEAR_MACRO_058_CLOSURE_2026-10-19.md` e CSV associati.
- MSVC/Windows non eseguito in questo ambiente; il codice nuovo evita header POSIX ed estensioni GCC, ma la build MSVC non è dichiarata verificata. Bytecode compatto fuori ambito.

Diagnosi, sorgenti delle misure, matrice e limiti: `docs/reports/LINEAR_MACRO_058_CLOSURE_2026-10-19.md`.

---

# libc UB audit continuation — 2026-10-17 evening

`src/cstdlib/stdio.c` now pre-validates scanf format syntax and counts destination-consuming conversions before calling the host CRT. Missing destinations and malformed/unsupported conversions raise source-located BabyC diagnostics. Added tests 1055–1057; focused tests 1050–1057 match expected output and FP modes remain 32/32. The full dual-suite rerun exceeded the 120-second execution limit before reporting a summary, so no full-suite pass is claimed for this latest modified tree. Remaining work includes scanf destination-type/size validation, printf length/type compatibility, object provenance/bounds, full sanitizer/bytecode gates, and MSVC validation.

# Aggiornamento 2026-10-17 — prima scansione sistematica libc/UB

- Applicato il principio didattico alla libc core: le violazioni rilevabili delle precondizioni C devono fermare il programma con diagnostica prima di chiamare il CRT host. Non si sostiene che ogni UB sia rilevabile senza metadati di provenienza e dimensione degli oggetti.
- `string.h`: rilevazione overlap per `memcpy`, `strcpy`, `strncpy`, `strcat`, `strncat`; controlli null/size nei wrapper standard e correzione del padding `strncpy` legacy. `memmove` continua a consentire overlap; i wrapper memoria rifiutano puntatori null anche con size zero.
- `ctype.h`: validazione `EOF`/unsigned-char; `stdlib.h`: stringhe null nelle conversioni, comparator invalido di `qsort`/`bsearch`, `abs(INT_MIN)`, `labs(LONG_MIN)`, `free`/`realloc` su puntatori non registrati; `strdup` registra le allocazioni.
- `stdio.h`/`time.h`/`math.h`: controlli su argomenti null, size/origin/mode non validi e format/argument UB rilevabile; corretto anche il puntatore output di `modf`. `fflush(NULL)` resta valido.
- Nuovi test 1050–1054: ctype invalido, overlap `strcpy`, comparator `qsort` nullo, `free` su stack, `%s` nullo. Test 083 conserva il controllo `memcpy`/`memmove`.
- Verifica: build GCC `-Wall -O2` PASS; A-IR/B-NO_IR/C-IR-SHADOW0 965/965 ciascuno; FP modes 32/32. ASan/UBSan mirato sui test 083 e 1050–1054: nessuna segnalazione. Il gate bytecode ha superato il timeout di 120 s, quindi non è dichiarato PASS; MSVC e corpus sanitizer completo restano da eseguire. I test 1050–1052 in Linear strict ancora segnalano `strict: tree fallback (body)` e non sono coperti da quel percorso.
- Benchmark spot-check (5-run): nessuna regressione VM coerente; `stress.call_loop` VM_no 0,769 → 0,744 s (~−3,3%), VM_acc 0,737 → 0,739 s; `micro.isort` VM_no 0,306 → 0,301 s; `classic.isort` VM_no 0,039 → 0,039 s. Mancano benchmark dedicati a stringhe/allocatore/stdio.
- Lacune prioritarie: bounds/provenienza su stack/global e letture; `gets`/`sprintf`; compatibilità completa dei format specifier; completezza tracking heap; pass separato per POSIX/estensioni.

Dettagli: `docs/LIBC_UB_AUDIT_2026-10-17.md`.

---

# Aggiornamento 2026-10-17 — audit secondo valutatore e overflow host

- **Signed overflow confermato e corretto:** il test 819 faceva emergere UB del runtime host nei percorsi di fusione espressioni, `IrLinAddI`, aritmetica token e divisione `LLONG_MIN / -1`. I percorsi sono stati resi definiti usando operazioni unsigned per il wrapping e casi espliciti per divisione/modulo min/-1. Test 819 ora copre anche divisione e modulo.
- **Sanitizer:** test 819 PASS sotto ASan/UBSan con `halt_on_error=1` in Tree, Linear strict, token-only e VM-selected. Gate bytecode mirato PASS con `attempts=1, runs=1`.
- **Suite duale:** A-IR 960/960, B-NO_IR 960/960, C-IR-SHADOW0 960/960; `test_fp_modes.py` 32/32. Non si dichiara completata la suite Linear completa né il gate bytecode aggregato.
- **097 e 1038/1039:** segnalazioni non più riprodotte sul sorgente corrente; messaggio/colonna del 097 corretti, Tree ora segnala la lettura non inizializzata tramite puntatore nei test 1038/1039. Aspettative verificate in Tree, Linear strict, token-only e VM-selected.
- **Performance (7-run mediane, baseline e corrente ricompilate con GCC -O2 sullo stesso host):** `call_loop` VM_no 0,902 → 0,737 s; VM_acc 0,878 → 0,750 s. `micro.isort` VM_no 0,315 → 0,327 s (circa +3,8%, piccolo rischio residuo); `classic.isort` VM_no 0,040 → 0,037 s. Il rallentamento generalizzato del 30% non è stato riprodotto.
- **Test 083 / UB di `memcpy`:** lacuna reale corretta. Le entrypoint standard `memcpy` ora controllano l'overlap prima di chiamare la libc e interrompono l'esecuzione con `memcpy source and destination regions overlap (undefined behavior in C)`. Il test verifica anche che `memmove` continui a gestire l'overlap (`memmove=ababcd`). La funzione helper è comune e il controllo si trova solo nel wrapper della funzione, non nel percorso di copia interna del runtime.
- **Igiene:** lo ZIP completo distribuito contiene zero `.o`/`.obj`; il working tree locale può contenerli dopo la build. Non è presente una configurazione CI (`.github/workflows`, GitLab CI o Jenkins), quindi l'assenza di un gate VM/bytecode automatizzato resta confermata.
- **Gate aperti:** esecuzione completa del gate bytecode, copertura sanitizer sull'intero corpus, CI esplicita per Linear strict/bytecode e verifica MSVC/Windows.

Dettagli: `docs/SECOND_EVALUATOR_AUDIT_2026-10-17.md`.

---

# Aggiornamento 2026-10-17 — chiusura tracking alias diagnostico Tree

- Corretti i test `952_tree_pointer_param_read_uninit` e `953_tree_pointer_alias_partial_init`: Tree ora controlla l'init map dell'oggetto proprietario attraverso i frame chiamanti e propaga le scritture tramite `*p`/`p[i]` al frame che possiede la memoria.
- L'indirizzo di un array passato a una funzione interpretata non marca più indiscriminatamente tutte le celle come inizializzate. La marcatura permissiva per gli escape resta separata; il percorso fused viene bypassato solo per puntatori che raggiungono un oggetto automatico effettivamente tracciato.
- Ripristinata la marcatura degli elementi assegnati negli array di puntatori a funzione (`442_array_of_fptr`, `443_array_fptr_dispatch`, `454_complex_dispatch`, `624_nested_fptr`) senza reintrodurre il mascheramento. `901_vm_dynamic_struct_array_copy_padding` resta PASS.
- `100_array_type_mismatch`: il messaggio e il caret Tree/Linear/token-only ora coincidono (`char[3]`, colonna 27); causa radice: la lunghezza dell'identificatore era sommata una seconda volta a `IrExpr::CharacterPos`.
- `tools/run_suite.py` e `src/tests/run_suite_tranches.py` ora rifiutano un processo non-zero con `.expect` vuoto. Il caso harness sintetico è stato verificato.
- Gate corpus Linux/GCC dopo clean build: Tree 958/958; Linear 958/958; token-only 958/958; IR shadow=0 958/958. Token-only richiede timeout 40 s per evitare un falso timeout del test 278.
- Microbenchmark pointer-param: mediana 0,0545 s prima vs 0,0569 s dopo (+4,4%), output identico; il costo è limitato agli accessi che devono consultare la init map di un oggetto automatico tracciato.
- MSVC/Windows non eseguito in questa tranche. Restano da inventariare separatamente i percorsi di fault/SEH che non passano da `ProgramFail*`.

Dettagli: `docs/DIAGNOSTIC_PATH_AND_EXPECT_AUDIT_2026-10-17.md`.

---

# BabyC — stato corrente

**Aggiornamento:** 2026-10-16  
**Baseline di partenza:** `babyc-2.zip`  
**Delta:** fix ingresso diagnostico incompleto (compile + caret)

## Fix 2026-10-16 (diagnostic ingress)

| ID | Difetto | Stato |
|---|---|---|
| DIAG-1 | `DiagnosticLocation` usato ma non definito → build rotta | Corretto: typedef + prototipi in `interpreter.h` |
| DIAG-2 | `DiagnosticReportV` implicit declaration | Corretto: prototipo pubblico |
| DIAG-3 | Colonna forzata a 1 quando era 0 → fail 132/694 | Corretto: si preserva Column==0 (convenzione storica parse-error) |
| DIAG-4 | Expect 252 non allineato a `DiagnosticMemberColumn` (member `a`) | Expect aggiornato a `:8:22` |

### Verifica

| Gate | Risultato |
|---|---:|
| `make clean && make -j` GCC `-Wall -O2` | PASS |
| Suite corpus (`run_suite.py`) | 955 pass, 0 fail corpus diagnostici; 28 skip |
| Test 132 / 252 / 694 | PASS (caret e messaggio allineati) |
| `test_fp_modes.py` | 32/32 |
| `test_bytecode_vm.py` | 51/53 (820/825: `runs=0` pre-esistente, non toccato da questo delta) |

Modifiche file: `src/interpreter.h`, `src/diagnostic.c`, `src/tests/257_member_array_uninit.expect`.

---

## Storico — stato dopo W30-F1/F2/F3

**Aggiornamento:** 2026-10-14  
**Baseline:** `babyc(20261014-151857).zip`  
**Piattaforma:** Linux/GCC verificata; MSVC/Windows non verificato sulla baseline W30.

## Correzioni W30 chiuse in questo delta

| ID | Difetto | Stato |
|---|---|---|
| W30-F1 | Stride di riga perso per `int a[][N]` passato a funzione | Corretto in IR e token; test `983` copre `int a[][4]` e `int (*a)[4]` |
| W30-F2 | Promozione/signedness `unsigned char` contro `signed char` | Corretto; test `984` |
| W30-F3 | `(unsigned short)sh` sign-extend nella VM lineare | Corretto; test `985`, cast tipizzato nel bytecode |
| W30-R1 | Aspettativa token del caret per test 977 | Aggiunto `.expect.noir` |
| W30-R2 | Timeout del test 278 in token mode | Timeout dedicato 35 s; misura 27,48 s |
| W30-R3 | Test string-scan influenzato da flag ambientali | Ambiente isolato nel runner e nel test autonomo |
| W30-D1 | Manifest e documentazione obsoleti | Rigenerati/aggiornati |

## Verifiche aggiornate

| Gate | Risultato |
|---|---:|
| Build GCC incrementale, file modificati `-Wall -O2` e relink | PASS |
| Scansione corpus Tree | 921 pass, 0 fail, 28 skip |
| Scansione corpus VM lineare | 921 pass, 0 fail, 28 skip |
| Tre regressioni W30, cinque modalità | PASS |
| `test_fp_modes.py` | 28/28 PASS |
| Test token 977 | PASS |
| Test token 278 | PASS in 27,48 s |
| String-scan con flag honest ereditati | PASS in verifica dedicata precedente alla ricompilazione finale |
| `test_bytecode_vm.py` | Non concluso entro 300 s in questa sessione |
| Suite token completa | Non conclusa entro il timeout della scansione parallela |
| ASan/UBSan, fuzzing, MSVC | Non eseguiti in questa sessione |

La scansione indipendente confronta i file con `.expect`; non include i gate strutturali aggiuntivi di `tools/run_suite.py`. Il conteggio di 28 skip corrisponde ai file senza aspettativa nella baseline usata qui. Non confondere la conformità del corpus con copertura diretta del bytecode: `runs=0` indica fallback, non esecuzione compatta.

Per i dettagli delle cause, dei test e dei limiti vedere `docs/REPORT_W30_FIXES_2026-10-14.md`.


## Gate ausiliari chiusi

La verifica finale Linux/GCC ha completato la suite integrata e la suite bytecode.
Durante il gate è stata generalizzata la verifica delle chiamate terminali compatte
per consentire calcoli scalari puri tra chiamate supportate; il test `904` ora
esegue direttamente il bytecode (`attempts=1, runs=1`). I test `503` e `507`
sono anch'essi eseguiti direttamente. I test `986` e `987` isolano la copertura
diretta dei bitfield senza confonderla con `sizeof`.

La matrice ASan/UBSan è mirata a 11 regressioni in quattro modalità; il fuzzing
ha verificato 10 semi. Non si dichiara una scansione sanitizer completa del corpus,
una campagna fuzz lunga o la compatibilità MSVC. Dettagli e log in
`docs/REPORT_AUXILIARY_GATES_2026-10-14.md` e `logs/`.

---

## Update 2026-10-19 — Linear member/call cluster

Baseline: `babyc_baseline_2026-10-18_loop_promotion.zip` plus cumulative renumbering/performance delta.

The Linear STRICT cluster `606_storm_inc_dec_member_call`, `609_storm_enum_union_fptr`, and `618_inc_member_call_arg` is closed. The related `454_complex_dispatch` is also closed through generalized strict call-argument eligibility and function-pointer identity handling. The complete corpus is now Tree 1002/1002 with ACCEL=0/1 and Linear STRICT 995/1002 with ACCEL=0/1. The seven remaining Linear failures are: 047, 058, 062, 126, 651, 873, 893. The qsort comparator cluster 947/948 is closed by typed-cast validation for void-pointer callback parameters; see `docs/reports/LINEAR_QSORT_COMPARATOR_CLUSTER_2026-10-19.md` and its matrix/performance logs.

A/B with seven alternating pairs across the four established benchmarks and both ACCEL settings found no persistent regression beyond 1%; `string_scan_bounded` ACCEL=1 measured +0.86% median-of-pair with a wide noise interval. MSVC/Windows and bytecode remain unverified/out of scope for this update.


## Update 2026-10-19 — Linear STRICT initializer/array closure

Closed `062_initializers` in Linear STRICT. The initial dump showed a completed 386-instruction body rejected by `FnCodeIndexedPointerParamsSafe()` as `ptr-indexed` because `main`'s unused `char **argv` parameter was subjected to pointer-index checks. The general gate now applies those restrictions only to pointer parameters referenced by the lowered body. Allowing the body exposed two semantic bugs, also fixed generally: `sizeof(global_array)` was not folded from the declared type, and global multidimensional `grid[i]` did not perform row decay. Added test `1012_global_array_size_row_decay`; all older `.expect` files remain byte-identical. Matrix is Tree 1004/1004 and Linear STRICT 1000/1004 at ACCEL=0/1; four residuals are 047, 058, 126, and 651. See `docs/reports/LINEAR_INITIALIZERS_062_CLOSURE_2026-10-19.md`. Seven alternating AB/BA pairs cover every benchmark/configuration. Follow-up 21-pair series for short ACCEL=1 runs were mixed in raw medians but paired medians were not persistently slower; `string_scan_bounded` ACCEL=1 showed raw +3.69% in one repeat, raw -3.49% in another, and median paired deltas -2.41%/-2.74%. Incremental patch-vs-prior-cumulative A/B was +0.29% raw and -0.12% paired, with wide IQR. The short-run timing gate is within measured noise, but individual samples are not all faster; see supplementary CSVs. MSVC/Windows and bytecode remain unverified/out of scope.
