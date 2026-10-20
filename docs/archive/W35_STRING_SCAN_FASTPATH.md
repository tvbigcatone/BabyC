# W35 — fast path per scansione di stringhe e buffer

## Problema

`bench/string_proc.c` usava `while (*p)` ed era già riconosciuto da `IrTryFastPtrScanPred`. La variante equivalente per il buffer noto, `bench/string_proc_v2.c`, usa invece `while (p < end)` e finiva nel percorso generale: il contatore `ptrscan_pred_hit` restava a zero.

## Intervento

Esteso `IrTryFastPtrScanPred` per riconoscere anche la condizione limitata `p < end`, esclusivamente quando entrambi gli identificatori sono locali di tipo puntatore a `char`, distinti tra loro. Il kernel conserva il predicato già verificato (`*p == c` e catene OR fino a quattro caratteri), l'incremento del puntatore e l'accumulatore. Il ciclo limitato confronta `p` con `end` e non si ferma a un NUL interno, mantenendo così la semantica di `while (p < end)`; il ciclo NUL-terminato continua invece a fermarsi al primo `\0`.

Le forme non riconosciute continuano a ricadere nell'esecutore ordinario. Se il puntatore iniziale o il limite è nullo, il fast path rifiuta il pattern e lascia l'esecuzione al percorso generale.

## Test anti-regressione

- Aggiunto `src/tests/784_vm_string_scan_pred.c` con fixture ed expected output per scan limitata e NUL-terminata.
- Aggiunto `tools/test_string_scan_fastpath.py`, integrato in `tools/run_suite.py`. Confronta entrambi i benchmark con GCC, verifica l'output con fast path attivo/disattivo e richiede un contatore di hit positivo solo nella modalità attiva.
- Aggiunto `tools/bench_string_scan_fastpath.py` e il dump riproducibile `docs/W35_STRING_SCAN_BENCH.json`. I tempi sono informativi e non sono una soglia CI instabile.

## Misure

Benchmark end-to-end alternato, 8 campioni per modalità (incluso il campione iniziale), output nativo GCC `380000`:

| Sorgente | Fast path attivo | Fast path disattivo | Rapporto disattivo/attivo | Hit kernel |
|---|---:|---:|---:|---:|
| `string_proc.c` | 20,45 ms | 506,33 ms | 24,76x | 20.000 |
| `string_proc_v2.c` | 18,75 ms | 625,05 ms | 33,33x | 20.000 |

La macchina ha mostrato jitter, soprattutto nel controllo disattivo di `string_proc_v2.c`; le mediane sono descrittive, non una promessa prestazionale universale. L'esito di correttezza è il gate: output identico al riferimento GCC in tutte le modalità e hit counter verificato.

## Validazione

- `make clean && make -j2`: PASS, nessun warning GCC.
- `python3 tools/run_suite.py`: **844 pass, 0 fail, 23 skip**. Include il test C embedded-NUL e la verifica differenziale dedicata del fast path.
- `python3 tools/test_bytecode_vm.py`: PASS, inclusi i test strict/bytecode e i controlli di fallback/diagnostica.
- `python3 tools/bench_string_scan_fastpath.py --runs 7`: PASS, entrambi i benchmark hanno output identico a GCC e i contatori attesi.

Il gate prestazionale globale resta aperto.
