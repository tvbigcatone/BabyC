# BabyC — verifica refactoring helper e test suite a tranche (2026-10-17)

## Sintesi

Il task conferma la validità del refactoring dei metadati di frame. Non viene estesa in questa sessione l'ottimizzazione a tutti gli helper: prima si completa la verifica e si stabilisce una regola di riuso che non indebolisca le diagnostiche.

## Audit di applicabilità agli altri helper

- `IrFrameSetLocalOwned()` è il punto unico di scrittura per `LocalOwned[]`; la ricerca nel codice C/H non ha trovato altre assegnazioni dirette. Il contatore `NumOwnedLocals` resta quindi una fonte coerente, a condizione che ogni futuro cambio di ownership passi dal setter e che il reset completo del frame azzeri il contatore insieme ai metadati.
- `IrFrameLocalExtent()` è già condiviso da `IrLinCheckOwnedPointerAccess()` e da tre percorsi di accesso in `ir_lin_exec.c`. Il suo contratto è importante: se l'estensione non è nota restituisce la capacità massima, evitando di saltare slot validi. Non sostituire il fallback conservativo con uno zero o con un'estensione dedotta non provata.
- Il fast-path `NumOwnedLocals <= 0` è riutilizzabile solo negli helper che cercano specificamente intervalli di allocazioni possedute dal frame. Non è applicabile automaticamente agli helper che controllano `LocalInit`, `LocalInitMap`, tipi, dimensioni o accessi a celle scalari: tali metadati possono essere validi anche quando il frame non possiede allocazioni.
- Le assegnazioni dirette a `LocalInit[]` e `LocalInitMap[]` sono distribuite in più percorsi di esecuzione/emissione. È una candidata futura per centralizzazione, ma richiede una classificazione per semantica (scrittura scalare, elemento, byte, aggregato intero, alias/copia) e test mirati. Centralizzare indiscriminatamente rischierebbe di marcare inizializzati campi non scritti.
- Non è stata mantenuta la lista densa degli slot posseduti: l'esperimento ha rotto `951_direct_pointer_param_store_marks_init`, quindi è stato respinto. Non reintrodurlo senza un'invariante corretta e test differenziali.

## Test suite principale, Linear

Il corpus contiene 958 sorgenti con `.expect`; è stato eseguito a tranche, ciascuna con processo separato per non accumulare un timeout globale.

### Modalità non-strict (`BABYC_PATH=linear`, `BABYC_STRICT=0`)

- **955 pass, 3 fail, 0 skip** su 958.
- `100_array_type_mismatch.c`: output/posizione caret diversa dall'expected; messaggio effettivo `can't assign int[] from incompatible array` contro `can't assign int[] from char[3]`, caret una colonna più avanti.
- `952_tree_pointer_param_read_uninit.c` e `953_tree_pointer_alias_partial_init.c`: producono diagnostiche `uninitialized variable`, ma i rispettivi `.expect` sono vuoti. Il runner confronta soltanto stdout/stderr normalizzato e non interpreta il return code; questi test richiedono una decisione esplicita su come rappresentare le diagnostiche attese. Non si dichiarano pass.
- Tutte le altre 955 aspettative del corpus sono passate.

### Modalità strict (`BABYC_PATH=linear`, `BABYC_STRICT=1`)

- **900 pass, 58 fail, 0 skip** su 958.
- La maggioranza dei fail è il messaggio `strict: tree fallback (body)`, atteso come blocco in questa modalità per casi che ancora richiedono Tree. Ci sono anche differenze reali di output/diagnostica (ad esempio `050_bracket_evaluation.c`, `570_ir_global_struct_fp.c`, `841_real_a08_text.c`) da classificare separatamente.
- La tranche strict è utile come inventario delle aree senza esecuzione Linear pura, non è una misura della suite standard.

## Gate aggiuntivi

- `tools/test_fp_modes.py` con modalità non-strict: **32/32 pass**.
- Lo stesso gate in strict: **26/32 pass**; i sei casi falliti sono le tre modalità per `882_fp_math_intrinsic_return.c` e `098_fp_alias_store_matrix.c`, coerenti con i casi strict che richiedono ancora fallback Tree.
- `tools/test_bytecode_vm.py` è stato avviato in strict e non è terminato entro 300 s. L'output parziale mostra sia casi passati sia fail di fixture Tree/strict e di copertura bytecode; **il gate completo è indeterminato**, non PASS. Va rilanciato a gruppi tramite `BT_ONLY` (ogni sorgente in un processo separato), poi eseguiti i controlli supplementari `BT_ONLY=extras`.
- La build GCC era stata completata senza errori nel turno precedente; in questa tranche non sono state cambiate sorgenti C, soltanto questa documentazione.

## Decisione

Il refactoring resta valido come fondazione. Il prossimo passo sensato è completare il gate bytecode a gruppi, poi investigare i tre fail non-strict della suite e usare la classificazione degli helper per progettare una centralizzazione graduale dei metadati d'inizializzazione. Nessuna nuova ottimizzazione degli altri helper è applicata in questo turno.
