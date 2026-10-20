# BabyC — follow-up metadati degli array indicizzati (2026-10-17)

## Indicazione operativa

Il traguardo resta zero regressioni, ma una regressione che emerga durante un intervento architetturale va isolata e corretta: non è una ragione per evitare di collegare i punti comuni mancanti. Ogni caso non superato resta esplicito fino alla sua risoluzione.

## Collegamento dei sink `PointeeSize`

- Aggiunti `IrFrameGetPointeeSize()` e `IrFrameSetPointeeSize()` come accessori inline canonici per il dato frame.
- I percorsi runtime Tree/Linear interessati (`ir_eval.c`, `ir_mem.c`, `ir_fast.c`, `ir_exec.c`, `ir_lin_exec.c`, `ir_stmt.c`, `ir_storage.c`) usano gli accessori per leggere/scrivere lo stride del pointee.
- Il dual-read compile/frame `IrCompLocalPointeeSizeOrFrame()` delega ora la lettura frame all'accessore invece di leggere direttamente il campo.
- I residui diretti in `ir_frame.c` sono reset/inizializzazione del frame; la dichiarazione del campo e l'implementazione dell'accessore sono le sole letture/scritture grezze canoniche. Il bytecode sperimentale rimane fuori dal percorso attivo prioritario.

## Correzione del limite di stride >255

Il test `961_pointee_stride_large_struct.c` aveva reso visibile che la copertura strict non era limitata al metadato `PointeeSize`: anche `LocalElemSize` e `IdxMetaEs` erano memorizzati su 8 bit, e l'istruzione `IR_K_DECL_ARRAY` codificava lo stride in un solo byte.

- `IrCallFrame.LocalElemSize` e `IdxMetaEs` sono ora `int`, eliminando la troncatura degli stride di elemento nei percorsi Tree/Linear.
- `IR_K_DECL_ARRAY` codifica lo stride in 16 bit nei campi `A:B`; l'executor ricostruisce lo stesso valore.
- L'eligibility Linear ammette elementi di struct oltre 255 byte se il totale allocato resta entro i limiti già imposti dal percorso (4096 byte e massimo 64 elementi per array di struct).
- Eliminato anche il limite `<256` nella propagazione di `DeclPointeeSize` al frame.

Il caso da 304 byte (`char bytes[300]; int marker;`) passa ora in `BABYC_PATH=linear BABYC_STRICT=1`: non viene usato il Tree fallback.

## Verifiche

- Build GCC pulita dopo le modifiche ai layout runtime: superata, nessun warning/error nel log di build.
- Corpus mirato su struct-array, array-struct, multidimensionali, puntatori e bitfield: **56/56** in Tree, **56/56** in Linear non-strict, **56/56** in token-only.
- Caso `961_pointee_stride_large_struct.c` in Linear strict: exit 0, nessun fallback.
- Gate FP: **32/32**.
- `check_diagnostic_catalog.py`: superato.
- `test_empty_expect_exit_status.py`: superato.
- La suite completa non è dichiarata superata in questo turno: il runner completo è andato oltre il timeout di sessione in un tentativo precedente; dopo la modifica di layout è stata eseguita la batteria mirata indicata sopra. MSVC/Windows e benchmark prestazionali restano da eseguire.

## Residui classificati

- Accessi grezzi a `LocalPointeeSize` in `ir_frame.c`: reset, non letture operative.
- Accessi ai campi `LocalElemSize` in evaluator/runtime: numerosi, ma il campo ora conserva il valore intero senza troncamento. Il prossimo audit può ricondurre i punti di scrittura più centrali agli accessori comuni; non sostituire meccanicamente tutti i read hot senza verificare precedenza frame/compile.
- `ir_vm.c` mantiene ancora layout/stride specializzati per il bytecode sperimentale, lasciato intenzionalmente fuori dall'espansione prioritaria.
- `IrLinInst.A/B` sono byte generici; la loro combinazione in uno stride a 16 bit è specifica di `IR_K_DECL_ARRAY`. Gli altri opcode mantengono la semantica esistente.

## Decisione

Il limite strict individuato nel turno precedente è stato risolto alla fonte, ampliando la rappresentazione del dato invece di ripiegare sul Tree. La copertura completa strict resta un obiettivo distinto: il passaggio di `1049` non dimostra che tutti i 51 casi strict preesistenti siano risolti.
