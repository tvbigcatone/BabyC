# Verifica delle osservazioni del valutatore — stato dopo gli interventi disponibili

Data: 2026-10-17

## Perimetro e limite della comparazione

Ho verificato il sorgente e l'eseguibile correnti in `tree/babyc`, eseguendo i test mirati e confrontandoli con lo snapshot disponibile `babyc(20261017-074837).zip`, estratto e ricompilato con GCC `-O2`. Questo snapshot è il riferimento concreto disponibile per il confronto; non posso affermare che coincida con l'eventuale baseline più vecchia usata dal valutatore.

Nessun sorgente di prodotto è stato modificato durante questa verifica. Questo documento registra soltanto risultati e classificazione.

## Classificazione sintetica

| Osservazione | Stato corrente | Evidenza |
|---|---|---|
| `903_vm_multidim_struct_array`: bytecode stampa `27 27 27 27` | **Ancora presente (P0)** | Nel percorso bytecode: `attempts=1 runs=1`, output errato; Tree e Linear producono `11 27 15 42`. Riprodotto anche nello snapshot 074837. |
| `925_vm_global_multidim_struct_fields`: bytecode stampa `1 1 1 1` | **Ancora presente (P0)** | Bytecode: `attempts=1 runs=1`, output errato; Tree e Linear producono `41 9 42 11`. Riprodotto anche nello snapshot 074837. |
| `965` e `982`: `runs=0` | **Ancora presente** | Il gate bytecode fallisce perché la VM compatta non viene eseguita (`attempts=1 runs=0`), anche se l'output fallback coincide con l'expected. Non conteggiare come copertura. |
| `820` e `825`: `runs=0` | **Ancora presente** | Il gate bytecode fallisce esplicitamente perché `runs=0`. Entrambi erano già in fallback nello snapshot 074837. |
| `968`: diagnostica OOB assente in bytecode | **Ancora presente (P0 diagnostica)** | Tree segnala `array index 2 out of bounds (size 2)` e termina con rc=1. Bytecode stampa nulla, rc=0, `attempts=1 runs=1`: errore silenzioso. Token-only segnala OOB ma con caret alla colonna 10 anziché 25. Riprodotto anche nello snapshot 074837. |
| `100_array_type_mismatch`: messaggio generico e caret col. 17 | **Corretto nei percorsi verificati** | Tree, Linear e token-only danno `can't assign int[] from char[3]`, colonna 27. Il percorso richiesto come bytecode dà lo stesso messaggio/colonna ma `runs=0`, quindi la diagnostica viene dal fallback, non dimostra copertura bytecode. Lo snapshot 074837 riproduceva il messaggio generico e il caret Tree alla colonna 17. |
| `257_member_array_uninit`: NO_IR caret col. 25 → 22 | **Discrepanza ancora presente; causa temporale non dimostrata** | Il sorgente corrente con `BABYC_NO_IR=1` punta alla colonna 22, mentre `.expect.noir` conserva la colonna 25. Il medesimo output alla colonna 22 è stato osservato anche nell'eseguibile ricostruito dallo snapshot 074837. Quindi la discrepanza è reale, ma non posso attribuirne l'introduzione agli interventi successivi a quello snapshot. Il runner normalizza via caret/colonna e può non intercettarla. |
| `1038` e `1039`: Tree passa mentre VM segnala `uninitialized variable` | **Corretto nei percorsi attuali** | Tree/default, Linear e token-only restituiscono ora la diagnostica attesa (colonne 36 e 48). Nello snapshot 074837 Tree terminava con rc=0 e output vuoto. Gli `.expect` principali ora non sono vuoti. |
| Rallentamento `fib(30)` VM circa 0,45 → 0,95 s | **Non riprodotto rispetto allo snapshot disponibile; confronto storico non conclusivo** | Benchmark end-to-end su `bench/cases/stress/fib_ptr.c`, `BABYC_EXEC=vm BABYC_NO_KERNEL=1 BABYC_NO_SHAPE_FP=1`, 5 run intercalati dopo warm-up: snapshot 074837 mediana 0,8373 s; corrente 0,8359 s (rapporto 0,998). Entrambi producono `832040 2692537`. Non dimostra che il confronto 0,45→0,95 s fatto dal valutatore sia falso: quel suo punto di partenza potrebbe essere un'altra baseline. |
| Simboli globali `Step`, `CallN`, `GlobVal` | **Ancora presenti** | `nm -g --defined-only` sull'eseguibile corrente conferma tutti e tre come simboli globali; risultano globali anche nello snapshot 074837. Non è stata applicata una correzione di visibilità/prefisso. `GlobPush` è anch'esso globale. |

## Gate bytecode

Esecuzione del gate completo:

```sh
python3 tools/test_bytecode_vm.py --babyc src/babyc
```

Il processo è stato interrotto dal limite di tempo dell'esecuzione dopo aver mostrato i risultati. Prima dell'interruzione erano stati osservati sei fallimenti espliciti:

- output errato con `runs=1`: `964`, `981`;
- esecuzione compatta non provata (`runs=0`): `965`, `982`, `820`, `825`.

Ognuno dei sei è stato anche isolato con `BT_ONLY=<nome-test>.c`; il gate mirato riproduce il fallimento in ciascun caso. `968` non fa parte della lista del gate e va protetto da un test diagnostico dedicato che verifichi sia il messaggio OOB sia il codice d'uscita, oltre al fatto che il bytecode sia davvero eseguito.

`docs/P0_CLOSED.md` non deve essere interpretato come prova che questi sei casi siano risolti: la sua dichiarazione sintetica `Suite gate 960/2` non riflette l'esito del gate bytecode corrente per questi casi. Le verifiche separate in `P0_MULTIDIM_AOS_964.md` e `P0_GLOBAL_MULTIDIM_AOS_981.md` riguardano principalmente il percorso Linear/STRICT e non cancellano il difetto bytecode compatto.

## Conclusione operativa

- **P0 da riaprire:** output errato di `964` e `981` in bytecode compatto; diagnostica OOB silenziosa di `968`.
- **Copertura da non dichiarare:** `965`, `982`, `820`, `825` finché `runs=0`.
- **Correzioni confermate:** diagnostica `097` e tracking Tree di `1038`/`1039`.
- **Discrepanza diagnostica ancora da chiarire:** caret NO_IR di `252` (22 effettivo contro 25 atteso).
- **Prestazioni:** nessuna regressione misurata contro lo snapshot disponibile per questo benchmark specifico; il confronto del valutatore con una baseline precedente resta da riprodurre.
- **Visibilità simboli:** da correggere in un intervento separato, con verifica di tutti i riferimenti e del link Windows/POSIX.

Non sono stati modificati sorgenti di prodotto, test o aspettative in questa verifica.
