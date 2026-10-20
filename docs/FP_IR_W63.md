# BabyC W63 — indagine aliasing FP

Baseline fissa: `babyc(20261015-093958).zip`.

## Riproduzione

Comandi eseguiti da `src/`:

- `./babyc tests/open_fp_alias_minimal.c` → `x=1.00` / `x=1.00` (errato)
- `BABYC_NO_IR=1 ./babyc tests/open_fp_alias_minimal.c` → `x=2.25` / `x=3.75` (corretto)
- `./babyc tests/open_fp_alias_copy.c` → `x=1.00` anche dopo gli store indiretti.

## Evidenza

La traccia dell'operatore address-of mostra `&x` che restituisce l'indirizzo di `LocalVal[0]`. La traccia dello store mostra invece `IrStorePtrElem()` che riceve `LocalVal[1]`, cioè la posizione adiacente. Questo è compatibile con un livello di dereferenziazione extra o con un valore del puntatore locale corrotto tra inizializzazione e store.

Sono state provate modifiche temporanee in `IrResolvePtr()` e nel ramo di assegnazione indiretta. Il riproduttore non è cambiato; entrambe le modifiche sono state scartate e non sono incluse nella patch. La causa è quindi circoscritta, ma la correzione non è ancora validata.

## Verifiche

- Build GCC pulita: PASS.
- Riproduttore alias minimo in default: FAIL (x resta 1.00).
- Riproduttore alias minimo in token: PASS (2.25, 3.75).
- Suite completa, audit architetturale FP e MSVC: non eseguiti in questo turno.

## Prossimo passo tecnico

Seguire il valore `LocalVal[1]` nel percorso di inizializzazione di `p` e individuare la prima istruzione che lo rende diverso dall'indirizzo di `LocalVal[0]`. Evitare una correzione solo in `IrStorePtrElem`: lo store riceve già un indirizzo errato.
