# BabyC — audit architetturale FP dell'IR (2026-10-14)

## Scopo

Riprendere la segnalazione «IR FP parziale per architettura» distinguendo i difetti dimostrati dalle aree che richiedono ulteriore verifica. Questa è una baseline characterization, non una dichiarazione di completamento né una correzione semantica.

## Risultato principale: `float` e `double` collassano nello stesso tipo interno

L'ispezione del codice conferma un limite architetturale concreto:

- `src/type.c`, in `TypeParseFront`, mappa sia `TokenFloatType` sia `TokenDoubleType` a `pc->FPType`.
- `src/type.c` crea `FPType` con `sizeof(double)`.
- `src/ir.h` espone `IR_TF_FP` come unico flag di tipo FP: non conserva precisione o larghezza (`float` vs `double`).
- `src/ir_compile.c` annota costanti FP con `ValWidth = IR_VW_I64`.
- Il lowering/esecutore lineare in `src/ir_lower.c` usa valori IEEE `double` a 64 bit per l'aritmetica FP (`IR_LIN_F_FP`).
- I percorsi di memoria e chiamata trattano `TypeFP` come `sizeof(double)` e impacchettano i valori FP come bit IEEE `double`.

Questi elementi non dimostrano che ogni operazione `double` sia errata; dimostrano invece che l'IR e i percorsi collegati non possono rappresentare fedelmente la distinzione semantica/storage tra `float` e `double`.

## Riproduzione indipendente

Il probe riproducibile è `tools/audit_fp_ir_arch.py`. Compila un programma C nativo con GCC e lo confronta con BabyC in modalità Tree/default, VM lineare, token e VM compatta abilitata.

Riferimento GCC:

```text
size 4 8 12 24
round 1 0 1 0
arrays 1.25 2.50 4.00 / 1.25 2.50 4.00
```

Risultato osservato in tutte le modalità BabyC:

```text
size 8 8 ...
round 0 0 0 0
arrays 1.25 2.50 4.00 / 1.25 2.50 4.00
```

Il probe segnala **0/4 modalità conformi**. La differenza è semantica, non di formattazione: `sizeof(float)` è 8 invece di 4 e `16777216.0f + 1.0f` non arrotonda alla precisione binary32. Inoltre la dimensione dell'array locale `float[3]` diverge fra modalità (ulteriore problema di dimensionamento da isolare; non va attribuito automaticamente al solo tipo FP).

Comando:

```sh
python3 tools/audit_fp_ir_arch.py --babyc src/babyc
```

Il comando restituisce exit code 1 finché il problema resta presente. È intenzionale che questo probe non sia integrato nella suite standard verde prima che il comportamento sia corretto.

## Gate esistente e cosa prova davvero

`python3 tools/test_fp_modes.py --babyc src/babyc` restituisce `fp-modes: 28/28 passed`. È una regressione utile per le semantiche FP già coperte (soprattutto double), ma non copre la distinzione `float`/`double`; quindi non contraddice il risultato del nuovo probe.

La ricompilazione GCC completa è riuscita e il log non contiene warning.

## Mappa per area

| Area | Evidenza attuale | Stato |
|---|---|---|
| Tipi | `float` e `double` condividono `FPType`; un solo `IR_TF_FP` | **Gap dimostrato** |
| Operazioni | Backend lineare FP calcola con `double`; manca la precisione sorgente | **Gap dimostrato per `float`** |
| Chiamate | Frame/argomenti FP impacchettati come `double` | **Gap architetturale da correggere e testare** |
| Memoria | `TypeFP` ha dimensione `sizeof(double)`; gli array `float` sono sovradimensionati | **Gap dimostrato** |
| Trasformazioni | Il flag FP è booleano, non conserva la precisione del nodo | **Informazione non rappresentabile; impatti da mappare** |
| Backend | Valori/operazioni FP della VM lineare sono bit IEEE binary64 | **Gap dimostrato per `float`** |

## Piano di completamento, senza workaround locali

1. **Modello dei tipi:** introdurre una distinzione esplicita tra float binary32 e double binary64 nella rappresentazione dei tipi, preservando l'ABI host prevista da BabyC.
2. **IR tipizzato:** sostituire il flag FP booleano come unica fonte di verità con una classificazione FP precisa per nodi, risultati, parametri, return e accessi memoria. `IR_TF_FP` può rimanere come predicato derivato di comodità, non come rappresentazione completa.
3. **Semantica delle operazioni:** implementare promozioni/conversioni C e arrotondamento binary32 ai confini corretti (assegnazioni, parametri, return, cast e store), mantenendo l'aritmetica `double` binary64.
4. **Memoria e layout:** correggere `sizeof`, stride degli array, struct/union, pointer arithmetic, accessi indiretti e inizializzatori per `float`.
5. **Chiamate e backend:** preservare il tipo FP attraverso frame, ABI interna, ritorni e VM compatta/lineare.
6. **Trasformazioni e fast path:** ogni kernel FP deve dichiarare la precisione che supporta; il preflight deve rifiutare stream non rappresentabili, non reinterpretarli come double.
7. **Differenziali:** aggiungere regressioni in `src/tests/` per `sizeof`, precisione, mixed float/double, conversioni, array/puntatori, struct, chiamate annidate, return e valori limite; confrontare GCC con Tree, VM lineare, token e compact direct (`runs > 0`).
8. **Gate di chiusura:** suite standard e FP, corpus differenziale, VM compatta, ASan/UBSan e benchmark prima/dopo. Nessun guadagno prestazionale è accettabile se cambia il risultato C.

## Stato conclusivo

Il tema è riaperto con una riproduzione concreta e un inventario delle responsabilità architetturali. **L'IR FP non è ancora completo**: il primo gap non è una mera mancanza di test, ma l'assenza di distinzione `float`/`double` nel tipo interno. Questo pacchetto non pretende di aver risolto il gap; stabilisce il punto di partenza verificabile per una correzione end-to-end.

## Estensione del probe: chiamate e layout aggregati

Il probe è stato esteso con funzioni `float`/`double` che ricevono e restituiscono valori FP e con una struct mista `char`/`float`/`char`/`double`. Il confronto osservato è:

```text
GCC:       size 4 8 12 24; round 1 0 1 0; calls 1.25 1.25 3.00 3.00; layout 24 1.50 3.50
Tree/VM:   size 8 8 8 8;  round 0 0 0 0; calls 1.25 1.25 3.00 3.00; layout 32 1.50 3.50
Token:     size 8 8 24 24; round 0 0 0 0; calls 1.25 1.25 3.00 3.00; layout 32 1.50 3.50
Compact:   size 8 8 8 8;  round 0 0 0 0; calls 1.25 1.25 3.00 3.00; layout 32 1.50 3.50
```

Questo affina la diagnosi: il semplice passaggio di valori `float`/`double` nelle chiamate campionate produce l'output numerico atteso, ma ciò **non** prova la correttezza generale dell'ABI interna; la dimensione dei tipi, degli array e della struct resta errata e il confine di precisione `float` non è rispettato. Il bug di dimensionamento degli array locali differisce inoltre fra IR e token (`8` contro `24` per `float[3]`), per cui dovrà essere tracciato separatamente dal tipo base e dalla stride degli elementi.

Log riproducibile: `logs/FP_IR_ARCH_PROBE_EXTENDED.log`. Il probe continua intenzionalmente a terminare con exit code 1 finché almeno una modalità non coincide con GCC.

## W32 implementation experiment: separate source `float` type (rejected prototype)

A deliberately small prototype added a distinct `TypeFloat`/`FloatType` and mapped `TokenFloatType` to it, while leaving the rest of the execution pipeline unchanged. It compiled, but the experiment was **not retained** because it proved that a front-end-only split is unsafe:

- FP modes regressed from the prior 28/28 baseline to 17/28.
- Function returns/assignments produced corrupted binary32 values interpreted as binary64 (e.g. `4608308318706860032.00`).
- The token interpreter rejected a float assignment.
- The extended GCC differential probe remained 0/4 modes matching; array sizes and float rounding remained wrong.
- The prototype build introduced a `-Wswitch` warning for the new enum in `clibrary.c`.

The source tree was restored to the prior implementation after recording these results. No `TypeFloat` patch is included in the deliverable. This is a useful negative result: introducing a distinct base type must be atomic with updates to value representation, all type predicates and switches, conversions, lvalue load/store width, array stride/layout, IR type flags/serialization, calls/returns, and the token interpreter. Changing only the parser/type table is explicitly disallowed by the regression evidence.

Log files:
- `logs/FP_BINARY32_TYPE_PROTOTYPE_BUILD.log`
- `logs/FP_BINARY32_TYPE_PROTOTYPE_PROBE.log`
- `logs/FP_BINARY32_TYPE_PROTOTYPE_FP_GATE.log`
