# Audit delle stringhe radice diagnostiche — 2026-10-17

## Metodo

Sono state cercate le radici testuali dei messaggi runtime nei sorgenti, poi ogni occorrenza è stata classificata in base al percorso di emissione, non soltanto alla presenza della stringa. Questo dettaglio è necessario perché `diagnostic.c` è oggi un renderer comune, non un catalogo centrale di messaggi: le stringhe semantiche sono passate dai motori a `ProgramFail()`/`ProgramFailAt()` e vengono formattate in `DiagnosticReportV()`.

Perciò una stringa ripetuta in più call-site è un segnale di controlli semantici duplicati da confrontare, ma da sola non prova un bypass del catcher. Il bypass si dimostra quando un percorso fatale emette direttamente testo/caret senza passare per `DiagnosticReportV()` o `DiagnosticReportAssign()`.

## Risultati per radice

### `array index ... out of bounds`

Occorrenze nei percorsi Tree (`expression.c`), IR (`ir_eval.c`, `ir_mem.c`), Linear (`ir_lin_exec.c`), VM (`ir_vm.c`) e bytecode sperimentale (`irb.c`). I call-site ordinari usano `ProgramFail()`; il bytecode usa `IrbFail()`, che alla fine delega a `ProgramFail()`. Quindi questi call-site raggiungono il catcher comune, ma le varianti testuali non sono uniformi: alcune includono indice/dimensione, altre dicono solo `array index out of bounds`. È una differenza di messaggio da verificare per semantica e contesto, non un bypass dimostrato.

### `uninitialized variable`

Compare in `diagnostic.c` nella sola euristica `DiagnosticResolvedColumn()` (ricerca testuale per decidere se risolvere il caret di un membro), e nei call-site Tree/IR/Linear/bytecode. I call-site di errore individuati chiamano `ProgramFail()` oppure `IrbFail()` → `ProgramFail()`. La presenza doppia è quindi un accoppiamento fragile tra renderer e testo, ma non dimostra un percorso di emissione diretto. L'euristica sul contenuto del messaggio rimane un punto da eliminare in una revisione separata, preferendo una categoria diagnostica esplicita.

### `NULL pointer dereference`

Compare in `expression.c`, `ir_eval.c`, `ir_mem.c`, `ir_vm.c` e `irb.c`. I percorsi passano da `ProgramFail()` o `IrbFail()` → `ProgramFail()`. Nessun bypass testuale trovato in questi call-site.

### `division by zero` / `modulo by zero`

I call-site Tree/IR/Linear usano `ProgramFail()`; i call-site bytecode che usano `IrbFail()` delegano a `ProgramFail()`. Nessun bypass semantico diretto individuato in questi percorsi.

### `can't assign` / errori di assegnazione tipizzata

`AssignFail()` passa per `DiagnosticReportAssign()` in `diagnostic.c`. I normali errori d'assegnazione non stampano direttamente il messaggio nei motori. Il renderer specializzato usa `PlatformPrintf()` per preservare il formato `%t` dei tipi.

## Percorsi che non passano dal renderer comune

1. **Fault Unix durante chiamate intrinsic:** `PicocIntrinsicSignalHandler()` in `platform.c` scrive direttamente con `write(2)` e `_exit(1)`. È intenzionale: il signal handler deve restare async-signal-safe e non può invocare in sicurezza il renderer generale (che usa funzioni di output e accessi non async-signal-safe).
2. **Fault Windows durante intrinsic:** il gestore SEH MSVC e il signal handler MinGW in `platform.c` stampano direttamente il caret e il suffisso `SEGV`/`ILL`/`FPE`. Sono percorsi di fault di sistema, distinti dagli errori semantici del linguaggio; la forma visiva è simile, ma non sono centralizzati in `diagnostic.c`.
3. **`PRINT_SOURCE_POS` e `PrintSourceTextErrorLine()` fuori dal renderer:** gli usi trovati in `parse.c`, `variable.c`, `expression.c` sono sotto macro di debug o stampe diagnostiche di sviluppo; `debug.c` usa la funzione per il breakpoint interattivo. Non risultano essere messaggi fatali del linguaggio, quindi non vanno classificati automaticamente come bypass del catcher.
4. Le stampe `[babyc-debug]`, `[vm-rej]`, `[fp]`, `[slot]`, `[mem]` e simili sono tracce diagnostiche di sviluppo, non errori runtime per l'utente; vanno filtrate dall'audit delle diagnostiche semantiche.

## Conclusione

Nel campione esaminato, le radici semantiche principali (`out of bounds`, `uninitialized variable`, `NULL pointer dereference`, divisione/modulo per zero e assegnazioni incompatibili) entrano nel renderer comune tramite `ProgramFail*` / `AssignFail`, compreso il wrapper `IrbFail`. Non è stata dimostrata una stampa diretta di queste specifiche diagnostiche da parte di un motore.

Il bypass effettivo rimasto è circoscritto ai gestori di fault di sistema dentro le chiamate intrinsic. Non è corretto farli chiamare direttamente al renderer generale dal signal handler Unix. La prossima verifica utile è una matrice di test differenziale per ogni radice (messaggio, exit code, file/riga/colonna) in Tree, Linear, token e bytecode, senza confondere le varianti di testo con i bypass del catcher.
