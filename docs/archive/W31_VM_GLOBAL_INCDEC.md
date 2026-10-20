# W31 — correzione semantica `++/--` e copertura bytecode

**Data:** 2026-10-11  
**Baseline di confronto:** `babyc(3).zip`  
**Obiettivo:** correggere il mismatch degli incrementi/decrementi globali stretti, verificare l'ammissione effettiva nel bytecode compatto e riprendere il gap strict sugli array 2D misti.

## Correzioni implementate

1. **Peephole non sicuro rimosso.** Il pass di lowering eliminava `LoadGlobal g` immediatamente seguito da `IncG g` senza dimostrare che il registro caricato fosse morto. Per il postfix `x = a++`, quel registro contiene il valore osservabile dell'espressione. L'eliminazione causava valori non deterministici simili a indirizzi di memoria. Ora la trasformazione non viene eseguita senza una prova di liveness.
2. **Semantica di `IncG` nel percorso lineare.** La scrittura globale usa il tipo effettivo del valore, inclusa la conversione stretta dopo l'incremento. Non si assume che tutti gli scalari siano memorizzati nel membro `Integer`/`LongLongInteger` dell'union.
3. **Prefix nel lowering.** `++g` ora rilegge il valore dopo l'incremento, invece di materializzare zero come risultato dell'espressione.
4. **Valore prefix in Tree/IR evaluator.** La valutazione globale rilegge il valore memorizzato dopo la conversione al tipo dichiarato. Prima, `++unsigned_char` poteva restituire 256 mentre la variabile diventava 0; analogamente `++unsigned_short` poteva restituire 65536. Questo era un difetto del riferimento Tree, non un comportamento C valido.
5. **Copertura compatta mirata.** Aggiunti i test `845_vm_global_narrow_incdec` e `847_vm_compact_narrow_incdec`. Entrambi hanno `attempts=1, runs=1` in bytecode compatto e passano in Tree, VM lineare, VM strict e bytecode compatto.

## Test

- Build pulita GCC: PASS, zero warning.
- Suite standard: **841 pass, 0 fail, 23 skip**.
- `tools/test_bytecode_vm.py --babyc src/babyc`: PASS.
- Il test globale `905` confronta le somme del valore postfix/prefix con il valore globale dopo il wrap; output comune: `-1 0 -1 0`.
- Il test locale `906` esercita `signed char`, `unsigned char`, `short`, `unsigned short` nel bytecode compatto; output: `-256 0 -65536 0`.
- Il test storico `904` resta un caso complesso non ammesso dal bytecode compatto (`attempts=1, runs=0`). Non lo conto come copertura compatta; i test mirati 906 dimostrano invece che la famiglia locale stretta è eseguibile in compatto per il profilo supportato.

## Gap strict 2D misto ancora aperto

`tools/fixtures/831_vm_multidim_array.c` continua a essere eseguito nel bytecode compatto non-strict, ma `BABYC_EXEC=vm_strict` rifiuta il corpo durante il lowering (`body_kind=2, n=0, ok=0`) e termina con `tree fallback (body)`. La riduzione del fixture ha isolato la famiglia: una scrittura/lettura su un array globale 2D `int signed_grid[2][2]` da sola è sufficiente a provocare il rifiuto; la dichiarazione dell'array senza accessi non lo provoca. Il caso equivalente su `unsigned short grid[2][3]` viene ammesso. Quindi il blocco non è semplicemente dovuto alla presenza simultanea di due array o al `printf`: è una lacuna nel lowering strict per accessi a globali multidimensionali con elemento `int` signed. Non ho ancora localizzato il ramo esatto di lowering che rifiuta il caso e non dichiaro risolto questo gap. Il gate conserva esplicitamente il rifiuto; output corretto in modalità non-strict non viene conteggiato come copertura strict.

## Benchmark e regressioni

Baseline e working tree sono state ricompilate da zero con lo stesso Makefile/GCC. Per sette carichi sono stati eseguiti warm-up e cinque misure alternate per binario; si riporta la mediana wall-clock. Le misure di sessioni diverse hanno mostrato variabilità significativa; questa tabella è l'ultima serie misurata, ma non basta da sola a dimostrare l'assenza di regressioni:

| Carico | Baseline | W31 | Delta |
|---|---:|---:|---:|
| fibc | 119,22 ms | 111,62 ms | -6,37% |
| isort | 98,46 ms | 99,74 ms | +1,30% |
| structs | 135,56 ms | 118,48 ms | -12,60% |
| sieve | 175,12 ms | 170,14 ms | -2,84% |
| collatz | 115,40 ms | 114,13 ms | -1,10% |
| tri | 156,05 ms | 152,92 ms | -2,01% |
| matmul | 36,33 ms | 34,85 ms | -4,07% |

Le uscite dei benchmark sono state confrontate nei campioni controllati; non sono emerse differenze di output. Tuttavia, le variazioni di tempo (sia positive sia negative) superano il rumore minimo atteso per alcuni carichi. **Gate prestazionale: INCONCLUSIVO**, non PASS. Occorre ripetere in ambiente meno variabile e misurare separatamente il percorso VM; non promuovere un miglioramento generale sulla base di questo campione.

## Prossime priorità

1. Seguire il rifiuto strict fino al ramo di lowering che gestisce accessi globali 2D con elemento `int` signed, quindi estendere il percorso generico e aggiungere test differenziali; non allentare il gate.
2. Ridurre il caso `904` per distinguere la complessità del corpo (numero di chiamate/registri) da una reale lacuna semantica nel bytecode compatto.
3. Ripetere i benchmark con controllo del carico e includere benchmark specifici `BABYC_EXEC=vm`/bytecode prima di ogni promozione di default.
