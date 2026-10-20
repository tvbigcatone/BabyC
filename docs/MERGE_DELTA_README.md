# BabyCG 10 — baseline consolidata

Questa directory deriva da `babyc(9).zip`. I file originali non interessati dal merge sono mantenuti invariati; i file sorgente aggiornati integrano il fork `babyc_wide_imm_vm_fix`, le ottimizzazioni prestazionali e le correzioni descritte qui sotto.

## Modifiche integrate

### Correttezza e funzionalità
- Wide immediates per funzioni lineari/VM, con snapshot per funzione e ripristino durante le chiamate VM→VM.
- Gestione coerente di alcuni shift unsigned/wide.
- Test di regressione `776_wide_imm_mod` e `778_wide_imm_vm_caller`.
- La risoluzione dei nomi funzione evita scansioni ripetute della GlobalTable.

### Performance
- Percorso rapido `CachedVal` prima del lookup nella tabella dei simboli.
- Implementazione interna inline di `IrApplyBinOpFlags`.
- Guardia su `TplReady` per evitare inizializzazioni ripetute dei metadati.
- Cache di `MonoElig`: l'analisi strutturale di eleggibilità mono viene eseguita una volta per funzione, non a ogni chiamata.
- In `BABYC_EXEC=vm`, passaggio rapido fra slot di funzioni lineari quando non sono coinvolte cache di indicizzazione: evita snapshot completi di `FnCodeSlot` ad ogni chiamata.
- Ripristino dei flag wide-immediate limitato alla coda effettivamente usata, evitando di azzerare 2048 byte a ogni cambio funzione.
- Risoluzione del falso rifiuto di `printf` con argomenti scalari locali: durante il lowering, il tipo dichiarato scalare prevale su flag runtime `LocalIsPtr` residui di un frame precedente.
- Per i piccoli corpi non ricorsivi chiamati dal percorso ad albero, è memorizzata la scelta di evitare il tentativo VM ripetuto; le chiamate annidate nel percorso lineare possono comunque usare il codice compilato.

## Profilazione

Abilitare i contatori senza cambiare il comportamento normale:

```sh
BABYC_EXEC=vm BABYC_COV=1 BABYC_VM_PROFILE=1 ./src/babyc bench/noscorciabili/call_loop.c
```

- `lin_inst`: istruzioni del percorso lineare eseguite.
- `tree_stmt` / `tree_expr`: nodi statement/expression eseguiti dall'albero.
- `fncode_body` / `tree_body`: corpi funzione eseguiti da FnCode o dall'albero.
- `babyc-exec-profile`: tentativi, corpi eseguiti, rifiuti per parametri, fallimenti di compilazione e fallback al percorso ad albero.

**Precisazione:** `BABYC_EXEC=vm` attualmente seleziona il percorso di interpretazione delle istruzioni lineari. Non è ancora l'esecutore bytecode compatto di `ir_vm.c`, il cui `IrVmExecProd` resta uno stub. I contatori sono quindi etichettati `lin_inst` e non vengono presentati come conteggio di bytecode VM.

## Verifiche effettuate (Linux/GCC -O2)

- Build GCC: completata.
- Suite standard: `passed=775 failed=0 skipped=21`.
- Test `776_wide_imm_mod` e `778_wide_imm_vm_caller`: entrambi passano anche con `BABYC_EXEC=vm`.
- Gate comparativo: output identico tra IR e modalità lineare VM per tutti i sette benchmark.

| Benchmark | IR / VM | Istruzioni lineari | Nodi tree |
|---|---:|---:|---:|
| `rec_fibc` | 8,16× | 2.185.071 | 0 |
| `rec_fib_ptr` | 9,90× | 24.232.839 | 0 |
| `call_loop` | 2,15× | 34.000.015 | 0 |
| `fib_global` | 9,36× | 24.232.839 | 0 |
| `param_assign` | 3,71× | 12.400.018 | 0 |
| `param_read` | 2,61× | 16.000.019 | 0 |
| `leaf_call` | 2,01× | 38.000.015 | 0 |

I rapporti sono mediane misurate nel gate locale; non sono garanzie di prestazione su altri sistemi. I test saltati non sono conteggiati come superati.

## Esecuzione del gate

```sh
python3 bench/s3/run_s3_gate.py
```

Il gate usa soglie distinte per workload ricorsivi, aritmetici e call-heavy; ogni caso controlla anche che stdout IR e modalità lineare VM coincidano.

## Artefatti precompilati

`src/babyc.exe` e gli oggetti precompilati presenti nel pacchetto originale sono stati conservati invariati per rispettare il vincolo di delta sorgente. L'eseguibile Windows non è stato ricompilato in questa sessione Linux: ricostruire su Windows prima di usarlo, altrimenti si rischia di eseguire il binario precedente.
