# Linear VM: evitare scansioni dei frame senza locali posseduti

## Intervento

`IrFrameSetLocalOwned()` è il punto condiviso di aggiornamento del flag `LocalOwned` e mantiene anche `NumOwnedLocals`. `IrLinCheckOwnedPointerAccess()` continua a controllare prima l'intervallo delle celle scalari (`LocalVal`), ma salta la scansione degli slot di un frame quando il conteggio condiviso è zero. La verifica non dipende dalla forma del benchmark e non disattiva i controlli quando esistono allocazioni possedute.

Il conteggio viene aggiornato solo quando il flag cambia davvero; il reset dei locali usa lo stesso helper. Il frame reset riporta il conteggio a zero dopo la pulizia degli slot usati.

## Esperimento scartato

È stata provata una lista densa degli slot posseduti per visitare soltanto quelli. La prova ha fatto fallire `951_direct_pointer_param_store_marks_init` (lettura successiva erroneamente diagnosticata come non inizializzata). L'intervento è stato quindi rimosso; non va considerato parte della soluzione. Si preferisce mantenere la scansione conservativa degli slot per i frame che hanno almeno un locale posseduto.

## Verifiche

- Build GCC pulita (`make clean && make -s -j2`): OK.
- Benchmark `run_bench.py --only structs --runs 5`: `classic.structs` Linear VM 56 ms senza acceleratori / 55 ms con acceleratori; `variant.structs` 38 / 37 ms. Output equivalente, 2/2 casi OK.
- Test Linear strict: `951_direct_pointer_param_store_marks_init` (rc 0, output `5`), `877_vm_recursive_pointer_alias`, `879_vm_mutual_pointer_recursion`, `859_vm_nested_call_params_alias` (rc 0); `952_tree_pointer_param_read_uninit`, `953_tree_pointer_alias_partial_init`, `768_lin_uninit_diag`, `244_array_elem_uninit`, `545_ir_struct_array_uninit` continuano a fallire con diagnostica attesa.
- Suite completa Linear strict: tentata ma il processo ha superato 300 secondi; esito complessivo non determinato.

## Limite

Il miglior tempo osservato in questo turno per `classic.structs` è 55 ms: è un miglioramento rispetto alle misure recenti di circa 59–67 ms, ma non raggiunge l'obiettivo di 40 ms. Le misure sono soggette a rumore e non dimostrano da sole una regressione o un guadagno definitivo. La prossima profilazione deve individuare il costo residuo dentro i frame con locali posseduti.
