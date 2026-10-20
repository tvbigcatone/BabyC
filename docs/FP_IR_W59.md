# BabyC — W59: traccia dell'indirizzo FP

Delta cumulativo rispetto a `babyc(20261015-071830).zip`. La traccia temporanea conferma che `&x` in `IrEvalExpr` risolve all'indirizzo dello slot locale di `x` e che la lettura iniziale di `p` contiene quell'indirizzo. Il precedente conteggio diagnostico dello slot di destinazione non è conclusivo: l'indirizzo effettivo va correlato al frame attivo e al contenuto degli slot prima e dopo lo store. Nessuna instrumentazione temporanea è inclusa nel sorgente consegnato.
