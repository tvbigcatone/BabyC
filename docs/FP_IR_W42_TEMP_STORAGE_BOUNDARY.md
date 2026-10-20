# W42 — stabilizzazione del confine memoria/temporanei FP

## Risultato

L’esperimento locale successivo a W41 che attivava `FloatType` nel frontend ha mostrato una regressione reale nel percorso token: `881_fp_user_func_return.c` stampa `fl=1.00` invece di `fl=3.50`. Il gate scende a 27/28. Non è accettabile promuovere questa separazione del tipo prima che parametri e ritorni siano coerenti.

W42 mantiene quindi disattivata l’associazione prematura `TokenFloatType -> FloatType`: `float` e `double` continuano temporaneamente a condividere il tipo storico `FPType` nel frontend. Il descrittore `FloatType` resta predisposto per il lavoro successivo, ma non è ancora un tipo semantico attivo.

Inoltre `VariableAllocValueFromType()` distingue la dimensione dello slot di esecuzione (double per un rvalue binary32) dalla dimensione di un oggetto/lvalue. Il descrittore `FloatType` usa `sizeof(float)` per la dimensione dell'oggetto.

## Verifiche W42

- Build pulita GCC: PASS, zero warning/error.
- `tools/test_fp_modes.py --babyc src/babyc`: 28/28 PASS.
- `tools/audit_fp_ir_arch.py --babyc src/babyc`: 0/4 modalità corrispondenti a GCC; problema architetturale non risolto.
- Nessun expected è stato modificato.

## Limite dichiarato

Questo delta ripristina la baseline di correttezza, ma non dichiara completato il supporto binary32. Per attivare `FloatType` servono ancora conversioni e round-trip coerenti per parametri, valori di ritorno, variabili, accessi indiretti e IR/VM.
