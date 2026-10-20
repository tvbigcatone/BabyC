# BabyC — consolidamento del gate bytecode e dei test strutturali

Data: 2026-10-17

## Perimetro

Verifica sul tree consolidato in `babyc_consolidation/babyc`, che comprende la baseline `babyc(20261017-074837).zip` e il delta cumulativo `BABYC_DELTA_CONSOLIDATION_VALIDATION_2026-10-17.zip`.

Questa tornata non modifica il runtime: aggiunge evidenza di test e chiarisce la differenza tra i gate eseguiti singolarmente e il runner aggregato che non termina entro il timeout.

## Gate bytecode compatto: esecuzione isolata

I 53 gruppi definiti da `tools/test_bytecode_vm.py` sono stati eseguiti singolarmente tramite `BT_ONLY`, con timeout individuale di 12–15 secondi. Esito: **53/53 gruppi PASS**.

Il conteggio comprende:

- 40 gruppi iniziali, fino a `818_vm_nested_pure_scalar_call.c`;
- `819_vm_branchy_call_side_effect.c`;
- i gruppi 901, 905, 906, 907, 931 e 932;
- le cinque fixture 830–834;
- il gruppo supplementare `extras`, comprendente i gate strict/OOB e gli edge case narrow increment/decrement.

Sono stati controllati output, exit status e contatori del percorso compatto. I casi che devono evitare l'esecuzione compatta continuano a verificare `runs=0`; non sono conteggiati come fallimenti quando il fallback è esplicitamente atteso. I casi nested 842, 843 e 931 hanno verificato l'esecuzione compatta annidata. I gate 901, 830, 831, 833 e 834 hanno superato anche le rispettive verifiche strict indicate dal test.

**Limite:** l'esecuzione aggregata completa di `tools/test_bytecode_vm.py` non è stata portata a termine entro il timeout globale di 900 secondi in questa sessione. Poiché tutti i 53 gruppi sono invece passati in isolamento, la copertura funzionale dei gruppi elencati è verificata; resta aperta la riproducibilità/diagnosi del problema di durata o blocco del runner aggregato. Non si dichiara PASS dell'invocazione aggregata.

## Altri gate ripetuti

- `tools/test_fp_modes.py --babyc src/babyc`: **32/32 PASS**.
- `tools/test_sparse_local_snapshot.py src/babyc`: PASS; esecuzione compatta confermata.
- `tools/test_string_scan_fastpath.py --babyc src/babyc`: PASS; controllo nativo, hit del prodotto e controllo `NO_SHAPE`.
- `tools/test_empty_expect_exit_status.py --babyc src/babyc`: PASS.
- `tools/check_diagnostic_catalog.py`: PASS; 10 literal canonici confinati a `src/diagnostic.c`.

## Suite generale

`python3 tools/run_suite.py src` è stata lasciata proseguire per 900 secondi ma non ha prodotto il riepilogo finale. Non sono disponibili un conteggio finale valido né un elenco completo di pass/fail: **suite generale ancora inconclusiva**. Non attribuiamo automaticamente il timeout a un difetto del prodotto, ma non lo ignoriamo.

## Build e performance

Questo consolidamento non ha cambiato sorgenti runtime e non introduce un nuovo ramo di esecuzione. Restano valide le misure A/B e la build pulita documentate in `CONSOLIDATION_VALIDATION_2026-10-17.md`; in questo turno non è stata eseguita una nuova misura prestazionale, quindi non viene rivendicato alcun ulteriore guadagno.

## Prossimo passo

Individuare perché i runner aggregati non completano nonostante i gruppi isolati siano rapidi e verdi: strumentare il progresso per test e isolare l'eventuale caso che blocca il runner, senza modificare i criteri di correttezza o nascondere timeout. Successivamente completare la suite generale e ripetere il confronto prestazionale dopo ogni modifica al runtime.
