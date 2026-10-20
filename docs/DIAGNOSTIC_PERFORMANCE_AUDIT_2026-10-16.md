# BabyC — diagnostica compatta: correzione e audit prestazionale

## Correzione della lettura indiretta

Il test `253_array_struct_member_uninit.c` isolava un bug specifico della VM compatta: per le istruzioni `Tok=2` (`*p` / `p->m`) il risultato negativo di `VmProdMemberInitialized()` veniva ignorato. La lettura proseguiva anche quando l'indirizzo apparteneva a un oggetto locale tracciato e i byte richiesti non erano inizializzati. Ora la VM compatta abortisce transazionalmente in quel caso, permettendo alla diagnostica canonica di essere emessa senza commettere gli effetti della transazione.

Inoltre, `IrFrameIsByteMap()` ora riconosce gli array il cui tipo elemento finale è `struct` o `union`, anche quando il tipo conservato nel frame è un `TypeArray`. `IrFrameInitMapLen()` calcola quindi la lunghezza in byte dell'intero array aggregato; un singolo store di membro non marca più come inizializzato l'intero elemento.

La mappa già esistente viene riutilizzata. Non sono stati aggiunti campi alle istruzioni, byte per opcode o lavoro nel dispatch. La VM compatta verifica già l'inizializzazione nei load indiretti; il fix corregge l'uso del risultato.

## Verifiche funzionali

Tutti questi test sono stati eseguiti in Tree, VM lineare e VM compatta:

- `253_array_struct_member_uninit.c`: caret `7:25`, diagnostica `uninitialized variable`.
- `545_ir_struct_array_uninit.c`: caret `8:23`.
- `263_nested_struct_array_uninit.c`: caret `10:27`.
- `922_nested_struct_array_uninit.c`: caret `9:27`.
- `141_bp_win64.c`: output `10000`.
- `221_ir_nested_calls_locals.c`: output `11`.

Il gate bytecode struct-array (`549`, `956`, `958`, `960`, `961`, `962`, `963`) è passato con codice di uscita 0; gli ulteriori casi `964`, `965`, `970`, `980`, `981`, `982`, `821`, `842`, `843` hanno anch'essi terminato con codice 0.

## Prestazioni

Confronto A/B delle due modifiche di questa tranche, dieci esecuzioni alternate per versione sul test `849_vm_compact_loop_bench.c`:

- controllo (prima del fix): mediana `15.141 ms`;
- candidato (dopo il fix): mediana `15.174 ms`;
- differenza delle mediane: `+0.22%`.

Le misure individuali sono variabili e il campione è piccolo; non dimostra costo esattamente zero. Non è emersa una regressione misurabile in questo confronto. Il fix interessa solo i controlli d'inizializzazione già presenti e le mappe aggregate; il loop di dispatch e la struttura `IrLinInst` non sono cambiati.

## Limiti

Build GCC riuscita. La suite completa e MSVC/Windows non sono state eseguite in questa tranche. La verifica dei casi elencati non equivale a una certificazione dell'intero corpus.
