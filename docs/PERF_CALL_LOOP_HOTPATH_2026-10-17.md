# Audit prestazioni call_loop — 2026-10-17

## Baseline e metodo

Baseline di partenza fornita dall'utente: `babyc(20261017-175902).zip`.
Il sorgente baseline e quello ottimizzato sono stati ricompilati entrambi con GCC `-Wall -O2` sullo stesso ambiente. Per `call_loop` sono stati eseguiti sette campioni per ciascuna revisione e modalità, con `BABYC_NO_KERNEL=1 BABYC_NO_SHAPE_FP=1 BABYC_NO_MEMO=1`; ogni esecuzione ha prodotto esattamente `2000000 0` ed exit status 0. Mediana dei sette tempi raccolti con `/usr/bin/time`.

## Risultati

| Modalità | Baseline (7 campioni, s) | Ottimizzata (7 campioni, s) | Mediana baseline | Mediana ottimizzata | Delta |
|---|---|---|---:|---:|---:|
| `BABYC_EXEC=vm` | 0.74, 0.80, 0.79, 0.80, 0.81, 0.82, 0.83 | 0.34, 0.34, 0.34, 0.40, 0.36, 0.35, 0.32 | 0.80 s | 0.34 s | **−57.5%** |
| `BABYC_EXEC=vm_strict` | 0.88, 0.87, 0.98, 0.85, 0.86, 0.81, 0.84 | 0.60, 0.59, 0.57, 0.56, 0.55, 0.54, 0.54 | 0.86 s | 0.56 s | **−34.9%** |

Benchmark runner dopo le modifiche (controllo di output incluso):

- `stress.call_loop`, 7-run median: ATS_no 0.454 s, ATS_acc 0.469 s, VM_no 0.321 s, VM_acc 0.312 s — OK.
- `micro.isort`, 5-run median: VM_no 0.318 s, VM_acc 0.007 s — OK.
- `classic.isort`, 5-run median: VM_no 0.039 s, VM_acc 0.039 s — OK.

Questi numeri sono misure locali; non sono promesse universali e non vanno confrontati con tempi assoluti raccolti su host o configurazioni diversi.

## Causa radice

`call_loop.c` non è ricorsivo: il ciclo chiama `add1(s)` due milioni di volte. Nell'esecutore VM non-strict, `add1` è eleggibile per `LightRunBody`, ma il dispatch dinamico chiamava comunque `IrFnCodeHasPtrSlots()` per ogni chiamata; tale funzione scandiva tutti i 1024 slot possibili. Inoltre ogni `FnCodeInstall()` eseguito dentro una chiamata con frame attivo scandiva tutti i 1024 `SlotName`, anche quando il callee non aveva alcun nome locale. Il costo era quindi lavoro ripetuto su tabelle vuote/non pertinenti, non solo la copia delle tabelle citata nella valutazione.

La modalità strict disabilita deliberatamente il light path e continua a usare il frame completo. Qui restano necessari snapshot/restore delle tabelle del chiamante, ma il lavoro sui nomi e sul riepilogo dei puntatori è stato ridotto. `IrStorageDescSeedFrameFromFnSlot()` è già limitata da `MetaLocalN`; per `add1` il limite è zero.

## Modifiche

1. Aggiunti `FnCodeSlot.NameLocalN` e `FnCodeSlot.HasPtrSlots`, calcolati una sola volta dopo il lowering del corpo.
2. `FnCodeInstall()` propaga i nomi soltanto nell'intervallo `[0, NameLocalN)`, anziché scandire sempre tutti i 1024 slot.
3. `IrFnCodeHasPtrSlots()` legge il riepilogo cached invece di scandire i 1024 slot a ogni chiamata. La scelta conservativa in `vm_strict` rimane invariata.
4. Rimosso il `memset` della coda inattiva `WideOn` da `FnCodeSnapshot()`: `LinTabsRestore()` legge esclusivamente `[0, WideMax)`, quindi azzerare la coda a ogni snapshot non contribuisce allo stato ripristinato.

Non sono stati introdotti nuovi motori o rami di esecuzione; la modifica riduce il lavoro dentro il percorso chiamata già esistente.

## Verifiche

- `make clean && make -j2`: PASS, GCC `-Wall -O2`, zero warning.
- Suite a tranche sul sorgente modificato: IR 973/973; TOKEN_ONLY 973/973; IR_SHADOW0 973/973; zero fail e zero timeout.
- Sottoinsieme call/frame/recursion/diagnostic: 45/45 aspettative corrette in Tree, Linear strict e VM.
- `stress.call_loop`, `micro.isort`, `classic.isort`: tutti i gate benchmark selezionati OK.

## Interpretazione della valutazione precedente

La diagnosi di un costo elevato nel percorso chiamata era fondata, ma l'attribuzione ai soli `FnCodeSnapshot`/`LinTabsRestore` non descrive tutto il costo della baseline fornita. Nel percorso VM non-strict di questa baseline, il lavoro O(1024) per chiamata nei controlli dei puntatori e nella propagazione dei nomi era un collo di bottiglia concreto e generalizzabile. La modalità strict usa invece il frame completo, perciò ha una diversa curva di costo. La dichiarazione generale di “costo zero” non è appropriata: si riportano risultati misurati e modalità precise.
