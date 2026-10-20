# BabyC — audit della centralizzazione diagnostica (2026-10-17)

## Obiettivo

Indagare le differenze diagnostiche partendo da `src/diagnostic.c`, distinguendo tra:
1. rilevamento dell'errore;
2. origine/posizione sorgente propagata dal motore;
3. rendering e formattazione finali.

Non è stata applicata una modifica al renderer in questo turno: i casi riprodotti non dimostrano un difetto generale di `DiagnosticReportV()`, e una correzione euristica rischierebbe di spostare caret corretti.

## Percorso comune attuale

- `ProgramFailAt()` in `src/platform.c` costruisce l'ingresso esplicito e delega a `DiagnosticReportV()`.
- `ProgramFail()` costruisce una `DiagnosticLocation` dai campi del `ParseState` e delega allo stesso ingresso.
- `DiagnosticReportV()` in `src/diagnostic.c` stampa la riga sorgente e il messaggio; la risoluzione aggiuntiva della colonna è limitata ai messaggi che contengono `uninitialized variable`.
- `IrEvalFailUninitializedMember()` in `src/ir_eval.c` preferisce `Access->SourceLine` e `Access->CharacterPos`; se mancano, ricade su `ParseState` e sull'identificatore radice.
- Molti percorsi Linear in `src/ir_lin_exec.c`, `src/ir_lin_ptr.c` e `src/ir_lin_light.c` chiamano `ProgramFail()` direttamente. Questo riusa il renderer comune, ma può consegnare una posizione del parser non equivalente a quella dell'istruzione/accesso che ha rilevato il problema.

Quindi il renderer comune non basta a garantire una diagnostica equivalente: occorre propagare un'origine equivalente fino all'ingresso.

## Riproduzione

Build: `cd src && make clean && make -s -j2` — riuscita con GCC.

Esecuzione non-strict, con `BABYC_PATH=tree` e `BABYC_PATH=linear`:

| Test | Tree | Linear | Esito dell'audit |
|---|---|---|---|
| `100_array_type_mismatch` | errore colonna 28 | errore colonna 28 | messaggio/posizione coerenti in questa riproduzione |
| `952_tree_pointer_param_read_uninit` | termina senza diagnostica | `uninitialized variable`, colonna 36 | differenza di rilevamento o tracking, non solo di rendering |
| `953_tree_pointer_alias_partial_init` | termina senza diagnostica | `uninitialized variable`, colonna 48 | differenza di rilevamento o tracking, non solo di rendering |
| `253_array_struct_member_uninit` | errore colonna 25 | errore colonna 25 | posizione coerente |
| `545_ir_struct_array_uninit` | errore colonna 23 | errore colonna 23 | posizione coerente |
| `263_nested_struct_array_uninit` | errore colonna 27 | errore colonna 27 | posizione coerente |
| `922_nested_struct_array_uninit` | errore colonna 27 | errore colonna 27 | posizione coerente |

I risultati Tree in `BABYC_STRICT=1` non sono stati usati per giudicare la diagnostica quando il programma termina con `strict: tree fallback (body)`: è un rifiuto del fallback, non l'esecuzione del corpo nel motore Tree.

## Conclusioni e ordine di lavoro

1. Non modificare ora l'euristica di `DiagnosticResolvedColumn()`: nei casi di controllo le colonne sono già coerenti.
2. Per i test 1038/1039, investigare prima perché Tree non rileva l'accesso non inizializzato, e poi confrontare l'origine sorgente conservata nei percorsi Tree/IR/Linear.
3. Per gli errori effettivamente rilevati da entrambi i motori, confrontare `FileName`, `SourceText`, `Line`, `Column` immediatamente prima di `ProgramFailAt()`/`ProgramFail()`; non confrontare soltanto il testo finale.
4. In una modifica successiva, valutare un piccolo helper comune per costruire/propagare `DiagnosticLocation` dai metadati già disponibili. Non aggiungere campi per istruzione né lavoro sul percorso riuscito senza una necessità dimostrata.

## Limiti

Questa è una riproduzione mirata, non una dichiarazione di suite completa superata. Non sono state eseguite verifiche MSVC/Windows in questo turno. Il percorso bytecode sperimentale resta fuori dallo scope di questa indagine.


## Follow-up — ingressi legacy e caret array (2026-10-17)

Dopo il primo audit sono stati centralizzati anche `LexFail()` e `AssignFail()`: il primo usa `DiagnosticReportV()`, il secondo `DiagnosticReportAssign()`, che condivide il renderer di posizione e conserva la formattazione `%t`. I test di assegnazione esistenti restano invariati.

La differenza di caret in `100_array_type_mismatch` non era un problema del renderer: `IrCopyArrayIdentInit()` sommava la lunghezza dell'identificatore a `IrExpr::CharacterPos`, che era già la colonna finale del token. Questo spostava il valore oltre la riga e il caret veniva clamped sul `;`. Il codice ora usa direttamente `SrcE->CharacterPos`; Tree, Linear e token-only coincidono a colonna 27.

La suite corpus ha passato 958/958 in Tree, Linear, token-only e IR shadow=0. Restano non verificati MSVC/Windows e i gate ausiliari lunghi del runner completo. I fault handler Unix restano separati per necessità async-signal-safe; non vanno forzati attraverso il renderer buffered ordinario.
