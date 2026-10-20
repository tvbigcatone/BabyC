# BabyC — P0: correzione delle radici semantiche (14 ottobre 2026)

## Baseline e obiettivo

Baseline: `babyc(20261014-061913).zip`, con il delta P0 precedente applicato. Questo intervento approfondisce le discrepanze IR/no-IR sui puntatori multilivello e la nota sulla bridge delle chiamate della VM compatta.

## 1. Radice delle discrepanze IR/no-IR: profondità di dereferenziazione persa nel typing dell'IR

La causa comune dei test 402, 611, 966 e 967 era nella classificazione dei tipi delle espressioni di dereferenziazione in `IrCompileExprTypes` (`src/ir_compile.c`). La logica precedente propagava `IR_TF_PTR` e `ElemSize == sizeof(void*)` dal livello precedente senza consumare un livello del tipo puntatore dichiarato. Di conseguenza, per `int **pp`, anche `**pp` veniva marcato come puntatore invece che come `int`.

Questo rendeva `**pp + 1` una falsa operazione di aritmetica su puntatori: su host a 64 bit l'offset poteva diventare `sizeof(void*)`, e `7 + 8` produceva `15` invece di `8`. Il problema era quindi nel typing/lowering IR, non nell'expected e non nella suite no-IR.

La correzione conta i livelli di dereferenziazione e li confronta con la profondità del tipo dichiarato alla radice. Il risultato viene marcato puntatore solo se rimane almeno un livello puntatore dopo le dereferenziazioni; altrimenti viene classificato come valore scalare (o FP quando il tipo residuo è FP). Non sono stati modificati gli expected per mascherare il difetto.

Risultati dopo il fix:

- `420_multi_level_ptr.c`: IR `8 8`, no-IR `8 8`.
- `607_storm_multi_ptr_loop_ternary.c`: IR e no-IR entrambi `37` e `1 3 5 7`.
- `906_multi_level_ptr.c`: IR e no-IR entrambi `3`, `8 8`, `9`.
- `907_triple_ptr.c`: IR e no-IR entrambi `4`, `7 7`.

## 2. Valutazione della nota sulla compact-call bridge

La critica dell'utente è fondata. Rifiutare preventivamente una funzione perché scrive globali o memoria indicizzata è una misura conservativa di eleggibilità, non una spiegazione della semantica della chiamata né una correzione generale della VM.

Ho rimosso dalla classificazione della bridge il rifiuto basato esclusivamente su `StoreGlobal`, `IncG`, `StoreIndex` e `AddStoreLocal` per chiamate scalari terminali. Il motivo tecnico per cui è sicuro ammettere questi effetti in questo sottoinsieme è che il preflight della VM controlla l'intera coda del chiamante prima dell'esecuzione e il call diventa il punto di commit: dopo la chiamata non è ammesso un percorso che fallisca e provochi fallback con ripetizione degli effetti.

I test 839 e 840 ora esercitano davvero il percorso compatto (`attempts=1, runs=1`) e verificano che gli effetti si producano una sola volta:

- 839: output `1 1`;
- 840: output `12 12`.

Questo è un miglioramento reale rispetto al semplice fallback, ma **non equivale a completare la bridge delle chiamate**. La VM usa ancora stato condiviso per alcune tabelle di lowering/esecuzione; chiamate annidate e corpi con branch restano esclusi perché non è stata ancora introdotta una completa separazione dello stato VM per profondità di chiamata. I test 842 e 843 rimangono fallback intenzionali. La correzione architetturale generale richiede frame VM per-call-depth e ripristino esplicito dello stato di esecuzione, non altre guardie ad hoc.

## 3. Cache dei metadati per accessi globali indicizzati

`IdxGlobBase` è una cache runtime associata a slot di metadati riutilizzati durante il lowering. `IdxMetaPush` ora azzera il base cache quando riutilizza lo slot, evitando che un riferimento globale risolto in precedenza sopravviva al cambio di proprietario del metadato. È una correzione di coerenza della cache; non risolve da sola i fallimenti 3D/4D descritti sotto.

## 4. Verifiche

- Build GCC pulita: OK.
- Nessun warning rilevato nella build pulita (`-Wall -O2`).
- Suite generale: 906 PASS, 1 FAIL, 23 SKIP. L'unico fail riportato dalla suite è il gate aggregato `bytecode-vm-regression`, che resta rosso per i casi elencati sotto.
- FP: 28/28 PASS.
- Test 839 e 840: output corretto, compact VM `attempts=1, runs=1`.
- Test IR/no-IR 402, 611, 966 e 967: output identico.

## 5. P0 ancora aperti — non dichiarare completata la fase

Il gate bytecode resta a 25/29 gruppi. Restano da correggere e validare:

1. `831_vm_multidim_array.c`: il percorso lineare dà output corretto, ma il gate non dimostra ancora l'esecuzione effettiva del bytecode compatto.
2. `833_vm_multidim_3d.c` e `834_vm_multidim_4d.c`: il percorso lineare fallisce quando carica gli elementi globali multidimensionali in variabili scalari; le scritture agli array possono apparire corrette mentre il valore trasferito al locale non lo è. Non è stato modificato il test per nascondere questa discrepanza.
3. Gruppo `extras` del gate bytecode: ancora da diagnosticare.
4. La VM compatta non supporta ancora in modo generale chiamate annidate/branchy con stato per-depth; 842 e 843 restano fuori dal percorso compatto.

La baseline quindi **non è ancora P0-clean**. Il fix della profondità di dereferenziazione chiude la causa comune dei quattro differenziali IR/no-IR; i restanti difetti VM devono essere affrontati come problemi separati, con regressioni in `src/tests/` o aggiornando i test VM già esistenti. Non è stata creata `new_tests/`.
