# BabyC — unificazione della posizione sorgente diagnostica

## Stato aggiornato al 2026-10-16

La discrepanza dei caret nei casi `248`, `548`, `258` e `979` è corretta nei motori Tree, VM lineare e VM compatta (inclusa VM strict). Il fix compatto corregge anche la lettura indiretta che prima ignorava il risultato del controllo di inizializzazione. Il fix è centralizzato in `diagnostic.c`: i motori consegnano l'errore attraverso l'ingresso comune e il modulo diagnostico normalizza, solo in fase di stampa, il membro evidenziato per gli errori di variabile non inizializzata.

## Perché non si è ampliata la VM compatta

Non sono stati aggiunti source span a `IrLinInst` e non sono stati aggiunti opcode o passaggi di bookkeeping. Il resolver analizza la riga sorgente solo quando sta per essere emessa una diagnostica. Il percorso di esecuzione riuscito non chiama il resolver.

## Coordinate verificate

- `253_array_struct_member_uninit.c`: `7:25` in Tree, VM, VM strict.
- `545_ir_struct_array_uninit.c`: `8:23` in Tree, VM, VM strict.
- `263_nested_struct_array_uninit.c`: `10:27` in Tree, VM, VM strict.
- `922_nested_struct_array_uninit.c`: `9:27` in Tree, VM, VM strict.

## Altre regressioni controllate

- Gate bytecode struct-array: 7/7 superati con compact `runs > 0`.
- Gate supplementare `BT_ONLY=extras`: superato.
- `141_bp_win64.c`: output `10000` in Tree/VM/VM strict.
- `221_ir_nested_calls_locals.c`: output `11` in Tree/VM/VM strict.
- `885_fp_int_return_conversion.c`: output atteso `3.000000 1.000000 2.500000 3.000000 8.000000` in Tree/VM/VM strict.

## Build e performance

`make clean && make -j4` è riuscito senza warning o errori del compilatore. Il benchmark compatto da cinque round è molto disperso e non consente una conclusione quantitativa affidabile. La modifica non altera la dimensione delle istruzioni compatte; il nuovo resolver è chiamato esclusivamente quando si stampa un errore.

La suite completa e MSVC/Windows non sono stati verificati in questa tranche. La tranche successiva ha aggiunto il fix di rilevamento nel percorso compatto; misure e dettagli sono in `DIAGNOSTIC_PERFORMANCE_AUDIT_2026-10-16.md`. Non si dichiara completata la migrazione universale di ogni categoria diagnostica: questa tranche elimina la divergenza nei casi riprodotti e stabilisce il punto di ingresso comune su cui migrare gli altri call site.
