# BabyC W65 — store FP attraverso alias che attraversano frame di chiamata

## Baseline e ambito

- Baseline immutabile: `babyc(20261015-093958).zip`.
- Questo delta è cumulativo rispetto alla baseline e include l'intervento W64 necessario a riprodurre il difetto FP iniziale, oltre alla correzione W65.
- Sorgente modificato: `src/ir_mem.c`.
- Regressione aggiunta: `src/tests/open_fp_alias_cross_frame.c` e relativo `.expect`.

## Difetto riprodotto

Un callee riceveva un puntatore a una variabile `float` del caller e la modificava attraverso un alias a più livelli (`float **`). Il percorso IR predefinito lasciava il valore precedente, mentre il percorso token aggiornava il valore corretto. Il riproduttore combina inoltre `double`, `int` e una chiamata ricorsiva con effetto collaterale, per controllare che la correzione non alteri gli altri locali o il ripristino dei frame.

Prima della correzione, W64 produceva:

```text
f=2.25 d=3.75 i=17
nested=6.25
```

L'output atteso e quello dopo W65 sono:

```text
f=4.50 d=3.75 i=17
nested=6.25
```

## Causa e correzione

`IrStorePtrElem()` riconosceva gli slot FP canonici (`LocalVal[]`, rappresentazione a 64 bit) soltanto nel frame attualmente in cima allo stack. Un puntatore può però riferirsi legittimamente a una variabile del caller: quando il callee scriveva attraverso un alias annidato, lo slot FP del caller non veniva riconosciuto e si ricadeva nello store nativo della larghezza sbagliata.

La correzione cerca lo slot esatto in tutti i frame attivi e aggiorna la rappresentazione FP canonica nel frame proprietario, applicando l'arrotondamento binary32 se il tipo dichiarato è `float`. La verifica rapida di storage locale/arena è stata allineata allo stesso modello multi-frame, evitando di trattare la memoria di un caller attivo come memoria heap/stringa.

Non sono state aggiunte fast path legate a benchmark né fallback generalizzati; la correzione opera sulla rappresentazione e sulla proprietà effettiva dello slot.

## Validazione

- Build GCC pulita (`make clean && make -j2`): PASS; nessun `warning:` o `error:` nel log.
- `tools/test_fp_modes.py`: PASS, 28/28.
- 9 regressioni su 4 modalità (default, token, `BABYC_EXEC=vm`, kernel/shape disabilitati): PASS, 36/36. Include gli alias FP W64, indirizzi locali, doppio puntatore, dereferenziazione/store e `883_fp_ptr_param_index`.
- Nuovo riproduttore `open_fp_alias_cross_frame.c`: PASS in tutte e 4 le modalità; output confrontato byte-per-byte con `.expect`.
- Controllo contro il sorgente W64 precedente: il nuovo riproduttore riproduce il difetto (`f=2.25` invece di `f=4.50`); dopo W65 produce l'output atteso.
- `tools/test_bytecode_vm.py` e suite completa non dichiarati PASS in questo turno; MSVC non verificato.

## Applicazione

Dalla directory che contiene la cartella `babyc/` estratta dalla baseline:

```sh
patch --dry-run -p1 < DELTA.patch
patch -p1 < DELTA.patch
```

Il pacchetto non include binari né oggetti compilati.
