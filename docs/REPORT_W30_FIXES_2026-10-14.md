# W30 — correzioni semantiche e consolidamento della consegna

Data: 2026-10-14  
Baseline: `babyc(20261014-151857).zip`  
Ambiente verificato: Linux/GCC. MSVC/Windows non verificato.

## Interventi

### W30-F1 — array 2D passati come parametri

La dichiarazione `int a[][4]` viene aggiustata a puntatore a riga, ma il percorso di indicizzazione usava una dimensione pointer-sized o la dimensione dell'elemento scalare in alcuni punti. In `src/ir_mem.c` lo stride del primo indice viene ora ricostruito dal bound dell'array di riga e dalla dimensione del tipo foglia; in `src/expression.c` il puntatore a una riga avanza usando `FromType->Sizeof`, invece di chiamare `TypeSize(array, 0, ...)` (il cui secondo argomento è un conteggio, non il bound della riga). `src/type.c` riconosce inoltre `int (*a)[N]` come puntatore ad array, invece di instradarlo nel ramo dei puntatori a funzione.

### W30-F2 — promozioni intere e signedness

`unsigned char` e `unsigned short` promuovono a `int` sull'ABI supportata. La classificazione per le usual arithmetic conversions ora distingue signedness di memorizzazione dalla signedness dopo promozione (`IrExprIsUnsignedPromoted`). Il percorso token non forza più confronti signed a unsigned solo perché un operando è `unsigned char`/`unsigned short`. Lo shift mantiene invece la signedness del solo operando sinistro.

### W30-F3 — cast a unsigned short nella VM lineare/compatta

Il lowering non elimina più il cast intero come se fosse un semplice wrapper. Emette un'istruzione cast tipizzata che restringe il valore alla base dichiarata. Il verifier e gli executor della VM compatta accettano ed eseguono questa forma. `(unsigned short)-1` produce `65535` invece di `4294967295`.

### Test runner e documentazione

- Aggiunto `src/tests/920_null_param_member_index.expect.noir` per la differenza di caret in modalità token, senza alterare l'aspettativa standard.
- Il timeout del solo `283_bigblock_bodyir.c` è 35 secondi; la misura locale token è stata 27,48 s.
- `tools/run_suite.py` avvia il test string-scan con `BABYC_NO_KERNEL` e `BABYC_NO_SHAPE_FP` rimossi dall'ambiente ereditato; il test autonomo applica la stessa regola.
- Riscritti `VERIFY.txt` e `docs/CURRENT_STATUS.md` per rimuovere i numeri storici fuorvianti e documentare lo stato W30 e i limiti di piattaforma.
- Rigenerato `MANIFEST_SHA256.txt` dopo le modifiche.

## Test di regressione aggiunti

| Test | Aspettativa | Tree | VM lineare | both | Token | VM compatta |
|---|---|---:|---:|---:|---:|---:|
| `927_w30_2d_param_stride.c` | righe corrette, somma 35 e puntatore `int (*)[4]` | PASS | PASS | PASS | PASS | output corretto; `runs=0` (fallback) |
| `928_w30_signed_unsigned_char.c` | `1 0` | PASS | PASS | PASS | PASS | PASS, `runs=1` |
| `929_w30_unsigned_short_cast.c` | `65535` | PASS | PASS | PASS | PASS | PASS, `runs=1` |

In aggiunta, `064_typedef.c` è stato verificato in modalità VM: l'output coincide con l'aspettativa dopo aver escluso la riga di diagnostica `[babyc]`, come fa il runner.

## Verifiche eseguite

- Compilazione GCC dei file C modificati con `-Wall -O2`: PASS; relink `make babyc`: PASS.
- Scansione indipendente parallela del corpus con aspettative: Tree **921 pass, 0 fail, 28 skip**; VM lineare **921 pass, 0 fail, 28 skip**. I tre test nuovi sono inclusi; i 28 skip sono i file senza `.expect` in questa baseline. La scansione non include i gate strutturali aggiuntivi di `run_suite.py`.
- `tools/test_fp_modes.py`: **28/28 PASS**.
- `tools/test_string_scan_fastpath.py` con `BABYC_NO_KERNEL=1 BABYC_NO_SHAPE_FP=1` ereditati: PASS in una verifica dedicata precedente alla ricompilazione finale dei file O2; il runner ora neutralizza tali flag.
- Test token `920_null_param_member_index.c`: PASS contro `.expect.noir`.
- Test token `283_bigblock_bodyir.c`: PASS, 27,48 s, entro il timeout dedicato di 35 s.
- I tre test nuovi passano in tutte e cinque le modalità richieste.
- `tools/test_bytecode_vm.py` non è terminato entro 300 secondi in questa sessione; non viene dichiarato superato dopo queste modifiche. La suite token completa non è stata completata entro il timeout della scansione parallela. ASan/UBSan, fuzzing e MSVC non sono stati rieseguiti.
- La suite standard seriale `tools/run_suite.py src` non è terminata entro 300 secondi; i risultati di corpus sopra sono della scansione indipendente.

## Esito

I tre casi semantici W30 sono corretti e coperti da test riproducibili su Tree, VM lineare, `both`, token e modalità compatta (per la matrice, con fallback dichiarato). I limiti non verificati sono esplicitati: non si dichiara il gate bytecode 47/47 come ripetuto dopo le nuove modifiche, né si dichiara MSVC o sanitizer verificato.
