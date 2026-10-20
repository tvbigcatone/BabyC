# W30 — audit di copertura VM e gate semantico

**Data:** 2026-10-11  
**Baseline di confronto:** `babyc(3).zip`  
**Obiettivo:** consolidare i gate W29, verificare la copertura effettiva e rendere riproducibile un mismatch che impedisce di ammettere in VM altre varianti di `++/--` globali.

## Interventi di questo turno

- Esteso `tools/test_bytecode_vm.py` con un controllo esplicito per `843_vm_narrow_incdec_edges.c`: il caso è verificato in `vm_strict` sul percorso lineare e il test segnala separatamente che il bytecode compatto è ancora non ammesso (`attempts=1, runs=0`). Non viene contato come copertura bytecode.
- Aggiunto `tools/repro/global_narrow_incdec.c`, riproduttore per incrementi/decrementi postfix/prefix su globali `signed char`, `unsigned char`, `short`, `unsigned short`.
- Nessuna nuova ammissione del bytecode è stata fatta in questo turno: il repro mostra un mismatch semantico nel percorso VM per i valori dell'espressione incrementata. Promuovere il caso a test verde o rimuovere una guardia senza risolvere il mismatch sarebbe scorretto.

## Problema bloccante rilevato

Con `Tree`, il repro stampa:

```text
127 127 -128
256 0
32767 -32768
65536 0
```

Con `BABYC_EXEC=vm` e `BABYC_EXEC=vm_strict`, i valori assegnati a `x` per alcune espressioni `a++`, `++b`, `c++`, `++d` non corrispondono a Tree, pur osservando il wrap dei globali. È quindi un problema di valore prodotto dall'espressione/liveness dei registri e/o del percorso `IncG`, non semplicemente di scrittura del valore globale.

Il tentativo di sostituire `IncG` con `LoadGlobal/AddI/StoreGlobal` non ha superato il confronto differenziale: è stato ritirato. La sorgente consegnata non contiene quel tentativo. Il repro è fuori da `src/tests` apposta, così non altera artificialmente il conteggio dei test verdi. La famiglia resta bloccata fino alla correzione e a test Tree/VM/bytecode concordi.

## Verifiche

- Build GCC (`make -j2`): PASS.
- Suite standard `python3 tools/run_suite.py`: **839 pass, 0 fail, 23 skip**.
- `python3 tools/test_bytecode_vm.py --babyc src/babyc`: PASS, inclusi i gate strict esistenti e il controllo W29.
- `python3 bench/run_bench.py micro`: timeout a 180 s; il risultato aggregato non è utilizzabile e non viene presentato come PASS.

## Benchmark mirato e ripetibile

Per evitare di usare binari precompilati non omogenei, ho ricompilato da zero sia la baseline sia la working tree con lo stesso Makefile/GCC. Per ciascun carico: un warm-up, 5 misure per binario, ordine alternato baseline/working tree, mediana wall-clock. I programmi sono eseguiti in modalità predefinita (Tree/IR), quindi la misura è un controllo di regressione generale, non una misura di throughput del bytecode VM.

| Carico | Baseline | Working tree | Delta |
|---|---:|---:|---:|
| fibc | 164,75 ms | 164,48 ms | -0,2% |
| isort | 114,45 ms | 114,81 ms | +0,3% |
| structs | 165,71 ms | 164,51 ms | -0,7% |
| sieve | 214,71 ms | 214,88 ms | +0,1% |
| collatz | 164,83 ms | 164,50 ms | -0,2% |
| tri | 164,99 ms | 165,40 ms | +0,2% |
| matmul | 64,09 ms | 64,18 ms | +0,1% |

I delta sono tutti entro ±0,7% e compatibili con rumore di misura; non emerge una regressione prestazionale misurabile in questo campione. Non è una prova di assenza assoluta di regressioni né un benchmark di prestazioni bytecode.

## Priorità successive

1. Risolvere il valore postfix/prefix di `IrLinIncG` per globali con tipi stretti, includendo la corretta semantica del valore dell'espressione, non solo la mutazione globale.
2. Solo dopo confronto differenziale, aggiungere il repro alla suite con aspettativa comune e abilitare la famiglia in strict/bytecode.
3. Estendere la codifica/validazione bytecode per il caso W29 locale: l'esecuzione lineare è corretta, ma il contatore `runs=0` dimostra che il bytecode compatto non è ancora stato esercitato.
4. Mantenere esplicito il gap strict del fixture 2D misto `831_vm_multidim_array.c`: output compatto non equivale ad ammissione strict.
