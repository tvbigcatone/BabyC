# BabyC — audit dei percorsi diagnostici e degli expected vuoti (2026-10-17)

## Risultato sintetico

L'audit ha corretto il tracking alias/init Tree per `p[i]`, il controllo degli exit status con expected vuoto, due ingressi diagnostici legacy (`LexFail()` e `AssignFail()`) e un errore reale di propagazione della colonna in `100_array_type_mismatch`. La suite corpus è stata verificata dopo queste modifiche in Tree, Linear, token-only e IR shadow disattivato: 958/958 ciascuna, senza timeout.

Gli errori fatali ordinari del linguaggio convergono ora nel modulo `diagnostic.c`: `ProgramFail()`/`ProgramFailAt()` e `LexFail()` usano `DiagnosticReportV()`, mentre `AssignFail()` usa `DiagnosticReportAssign()`. Quest'ultimo condivide lo stesso renderer di posizione e conserva il formatter PicoC `%t`. `ProgramFailNoParser()` usa `DiagnosticReportV()` senza posizione sorgente. Non è ancora corretto dire che ogni messaggio del prodotto attraversi quel punto: i fault del sistema operativo e i messaggi di debug sono categorie diverse.

I gestori Unix di SIGSEGV/SIGBUS/SIGILL/SIGFPE dentro Intrinsic devono restare minimali e async-signal-safe (write(2), `_exit`), quindi non possono chiamare il renderer ordinario che usa stream buffered. I percorsi Windows SEH/signal hanno analogo output dedicato per il contesto di fault. Sono eccezioni esplicite per crash/fault, non bypass di errori semantici del linguaggio. `picoc.c` contiene inoltre l'errore CLI `argc < 2`, che precede l'inizializzazione dell'interprete e non è una diagnostica sorgente.

## Percorso comune verificato nel codice

- `ProgramFail()` costruisce una `DiagnosticLocation` dal `ParseState` e chiama `ProgramFailAtV()`.
- `ProgramFailAt()` inoltra una `DiagnosticLocation` esplicita a `ProgramFailAtV()`.
- `ProgramFailAtV()` chiama `DiagnosticReportV()` e termina con codice 1.
- `ProgramFailNoParser()` chiama `DiagnosticReportV()` senza locazione, poi termina con codice 1.
- `DiagnosticReport()` è un wrapper variadico su `DiagnosticReportV()`.

La convergenza finale non assicura la stessa posizione: chiamanti che usano `ProgramFail()` possono aver già sovrascritto `Parser->Line`/`CharacterPos`, o non aver propagato lo span dell'espressione. In `diagnostic.c`, la risoluzione del caret è anche euristica e limitata a messaggi contenenti `uninitialized variable`; non deve essere usata per mascherare posizioni sorgente sbagliate a monte.

## Risultati riprodotti

Build GCC del progetto riuscita dopo aver ricostruito gli oggetti.

| Test | Tree, non-strict | Linear, non-strict | Conclusione |
|---|---|---|---|
| `100_array_type_mismatch` | rc=1, `can't assign int[] from char[3]`, col. 27 | stesso messaggio e colonna | testo e caret ora coincidono con token-only |
| `952_tree_pointer_param_read_uninit` | rc=1, `uninitialized variable`, col. 36 | stesso messaggio e colonna | corretto il tracking Tree |
| `953_tree_pointer_alias_partial_init` | rc=1, `uninitialized variable`, col. 48 | stesso messaggio e colonna | corretto il tracking Tree; le scritture parziali restano precise |
| `253_array_struct_member_uninit` | rc=1, diagnostica col. 25 | rc=1, stessa diagnostica col. 25 | equivalenza osservata |
| `545_ir_struct_array_uninit` | rc=1, diagnostica col. 23 | rc=1, stessa diagnostica col. 23 | equivalenza osservata |
| `263_nested_struct_array_uninit` | rc=1, diagnostica col. 27 | rc=1, stessa diagnostica col. 27 | equivalenza osservata |
| `922_nested_struct_array_uninit` | rc=1, diagnostica col. 27 | rc=1, stessa diagnostica col. 27 | equivalenza osservata |

Il banner `[babyc] PATH=linear ...` è una riga informativa separata; il runner la filtra. Non è una differenza di diagnostica.

## Expected vuoti: classificazione

L'inventario dei `.expect` vuoti contiene due categorie distinte:

1. **Successo silenzioso intenzionale**: molti test di regressione/performance restituiscono 0 e non stampano nulla (ad esempio `701_point1_fuse_gate`, `703_point2_tight_for`, `799_vm_bytecode_brlt`). Per questi un expected vuoto è appropriato, ma soltanto se il test verifica anche il codice di uscita.
2. **Expected sospetto/incompleto**: `952_tree_pointer_param_read_uninit.expect` e `953_tree_pointer_alias_partial_init.expect` sono vuoti, ma i rispettivi `.expect.noir` contengono la diagnostica attesa e il percorso Linear produce la stessa classe di errore. Per il percorso Tree l'output vuoto coincide con rc=0, quindi il runner lo conta come pass senza verificare la semantica. Questi expected principali non vanno lasciati vuoti una volta corretto il motore Tree.

Il runner `tools/run_suite.py` e `src/tests/run_suite_tranches.py` confrontavano l'output ma non consideravano `returncode` per gli expected vuoti. Entrambi ora richiedono `returncode == 0` quando `.expect` è vuoto; le diagnostiche negative con output atteso continuano a essere validate dal testo. La regressione è stata verificata con un eseguibile fittizio che termina con codice 1 senza output: il runner segnala FAIL, non PASS.

## Causa profonda corretta per 1038/1039

Nel valutatore Tree, leggere un identificatore locale che rappresentava un array e veniva usato come indirizzo poteva marcare l'intera init map come inizializzata. Era una politica di escape troppo ampia: veniva applicata anche quando l'array era passato a un'altra funzione interpretata, quindi `p[i]` nel callee non vedeva le celle non inizializzate del chiamante.

La correzione mantiene la politica permissiva solo per gli usi che non sono argomenti di chiamate interpretate; la valutazione degli argomenti conserva la mappa precisa. Per accessi indicizzati attraverso parametri/puntatori locali, il Tree evaluator ora controlla la mappa dell'oggetto proprietario in qualsiasi frame attivo, senza chiedere all'helper Linear di applicare i propri gate di modalità. Le scritture `*p = v` e `p[i] = v` aggiornano la mappa del frame che possiede davvero l'indirizzo, non solo quella del frame corrente. L'ottimizzazione fused viene bypassata soltanto se il puntatore punta a un oggetto automatico tracciato; gli accessi a puntatori non tracciati mantengono il percorso fused.

La prima prova della modifica ha rivelato un effetto collaterale: gli array di puntatori a funzione non venivano più marcati quando erano inizializzati tramite `tab[i] = funzione`. Il percorso speciale di store è stato corretto per marcare l'elemento scritto, evitando di ripristinare la marcatura indiscriminata dell'intero array. Il caso `901_vm_dynamic_struct_array_copy_padding` è stato inoltre mantenuto corretto evitando il controllo alias dell'intero elemento per gli array struct posseduti direttamente.

Per `100_array_type_mismatch`, il ramo condiviso Tree/Linear usa il tipo elemento effettivo e la dimensione dell'array sorgente (`char[3]`). L'indagine sul caret ha trovato la causa a monte: `IrExpr::CharacterPos` era già la colonna finale dell'identificatore, ma `IrCopyArrayIdentInit()` aggiungeva di nuovo `strlen(Ident)-1`. Il renderer poi limitava la colonna alla fine della riga, puntando erroneamente al `;`. La correzione usa direttamente la colonna catturata dall'espressione. Tree, Linear e token-only riportano ora colonna 27 e output identico; `.expect` è stato allineato al risultato verificato.

## Validazione conclusiva

- Build GCC: `make clean && make -s -j2` completato senza errori.
- Microbenchmark pointer-param (`sum_ptr(p,n)`, 384.000 letture indicizzate per run, 5 misure dopo warm-up): prima mediana 0,0545 s, dopo 0,0569 s, variazione circa +4,4%; output identico (`48960000`). È un costo misurato nel caso pointer-heavy, accettato per non saltare la diagnostica di alias/init; l'accesso fused resta attivo quando il puntatore non raggiunge un oggetto automatico tracciato.
- Suite Tree: 959/959, zero fail e zero timeout (3,9 s).
- Suite Linear: 959/959, zero fail e zero timeout (4,1 s).
- Suite token-only: 959/959, zero fail e zero timeout (27,2 s; il test 278 richiede un timeout maggiore di 5 s).
- Suite IR shadow=0: 959/959, zero fail e zero timeout (3,9 s).
- Nuovo test `960_lex_fail_common_ingress`: verifica output, caret e codice di uscita del percorso `LexFail()` centralizzato.
- Regressione exit-status: `tools/test_empty_expect_exit_status.py` verifica che expected vuoto + exit code 1 sia rilevato come FAIL; gli assert equivalenti sono eseguiti anche da `tools/run_suite.py`.
- I test mirati 1037–1039, 424, 425, 441, 627 e 963 passano nei percorsi pertinenti.

La suite tranches verifica output/diagnostiche e gli expected; il build e questi test sono stati verificati su Linux/GCC. MSVC non è stato eseguito in questa tranche.

## Stato dell'inventario e limiti residui

- Errori ordinari del linguaggio: `ProgramFail*`, `LexFail` e `AssignFail` ora usano `diagnostic.c` per presentare messaggio/posizione. Il nuovo test `960_lex_fail_common_ingress` protegge il percorso lessicale.
- Differenze caret: il caso 097 è chiuso a monte; Tree, Linear e token-only puntano tutti alla colonna 27. Non è stata aggiunta un'euristica al renderer per correggere il sintomo.
- Fault OS/SEH: i gestori restano dedicati per vincoli di sicurezza del contesto di fault; non sono ingressi ordinari di errore semantico.
- Non ancora eseguiti in questa tranche: MSVC/Windows e i gate ausiliari lunghi del runner completo (`test_fp_modes` / `test_bytecode_vm`). La suite corpus nei quattro modi è stata invece completata.
