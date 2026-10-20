# BabyC W64 — correzione alias FP locale nel percorso IR

## Baseline e ambito

- Baseline immutabile: `babyc(20261015-093958).zip`.
- Il delta incluso in questo pacchetto è cumulativo e applicabile direttamente a tale baseline.
- Intervento sul codice sorgente: `src/ir_mem.c`; aggiunti gli `.expect` dei due riproduttori già presenti in `src/tests/`.

## Difetto riprodotto

Con il percorso IR predefinito, `float *p = &x; *p = 2.25f;` lasciava `x` a `1.00`; anche il secondo store lasciava `x` a `1.00`. Il percorso token (`BABYC_NO_IR=1`) produceva correttamente `2.25` e `3.75`.

La causa individuata era la risoluzione di un identificatore locale usato come puntatore: `IrResolvePtr()` poteva trattare il locale come una cella scalare e restituire l'indirizzo della cella di `p`, invece del valore-puntatore contenuto in `p`, quando il metadato ausiliario `LocalIsPtr` non era impostato. La classificazione ora considera anche il tipo/flag del nodo IR.

C'era inoltre una discrepanza di rappresentazione per gli store indiretti verso una cella scalare FP del frame: il valore FP canonico di BabyC è contenuto in uno slot `LocalVal` a 64 bit, perciò uno store nativo di 4 byte non aggiorna correttamente quel valore. `IrStorePtrElem()` riconosce l'indirizzo esatto di una cella scalare FP e aggiorna lo slot nella rappresentazione canonica, con arrotondamento binary32 quando il formato FP disponibile lo specifica.

## Modifiche

- `src/ir_mem.c`: classificazione del puntatore locale basata anche sul tipo IR; gestione degli store indiretti verso slot FP scalari del frame.
- `src/tests/open_fp_alias_minimal.expect`: output atteso del riproduttore minimo.
- `src/tests/open_fp_alias_copy.expect`: output atteso del riproduttore con alias multipli.

Non sono state introdotte fast path specifiche per benchmark né fallback generalizzati.

## Validazione eseguita

- Build GCC pulita: PASS (`make clean && make -j2`); nessun `warning:` o `error:` nel log.
- `tools/test_fp_modes.py`: PASS, 28/28.
- `open_fp_alias_minimal.c`: PASS in `default`, `BABYC_EXEC=vm`, `BABYC_NO_KERNEL=1 BABYC_NO_SHAPE_FP=1` e `BABYC_NO_IR=1`; tutti producono `x=2.25`, poi `x=3.75`.
- `open_fp_alias_copy.c`: PASS; output atteso in modalità default e token.
- `259_alias_scalar_addr.c` e `260_alias_addr_member.c`: output uguale agli `.expect`.
- `tools/test_bytecode_vm.py`: non completato entro il limite di sessione; ha riportato anche controlli di percorso bytecode non esercitato per casi bitfield. Non viene dichiarato PASS.
- Suite completa `tools/run_suite.py src`: non completata entro il limite di sessione. MSVC non verificato in questo turno.

## Applicazione

Dalla directory che contiene la cartella `babyc/` estratta dalla baseline:

```sh
patch --dry-run -p1 < DELTA.patch
patch -p1 < DELTA.patch
```

Il pacchetto contiene il patch cumulativo, i file sorgente/test modificati, questo rapporto e gli hash. Non contiene binari né oggetti compilati.
