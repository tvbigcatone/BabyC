# BabyC — avanzamento Linear STRICT (18 ottobre 2026)

## Fonte di verità e riproducibilità

- Baseline esclusiva del turno: `babyc_baseline_2026-10-18_loop_promotion.zip`.
- SHA-256 baseline ZIP: `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`.
- Build verificata su Linux/GCC: `make clean && make -j2` in `src/`, `-Wall -O2`, **zero warning**. MSVC/Windows non verificato in questa sessione.
- Matrice: 1001 sorgenti numerati accompagnati da `.expect`, output normalizzato più exit status, environment ripulito dai flag `BABYC_*` ereditati; per Linear è stato imposto `BABYC_STRICT=1`. I risultati conclusivi dopo la rinumerazione sono nei quattro log `matrix_renumbered_*_2026-10-19.log`; i log del 18 ottobre restano archivio pre-rinumerazione.
- Tree è l'oracolo. Non sono state eseguite verifiche bytecode, deliberatamente fuori ambito.

## Matrice conclusiva dopo clean build

| Configurazione | Passati | Falliti | Timeout | Tempo suite |
|---|---:|---:|---:|---:|
| Tree, ACCEL=0 | 1001/1001 | 0 | 0 | 4,89 s |
| Tree, ACCEL=1 | 1001/1001 | 0 | 0 | 4,56 s |
| Linear STRICT, ACCEL=0 | 988/1001 | 13 | 0 | 4,24 s |
| Linear STRICT, ACCEL=1 | 988/1001 | 13 | 0 | 3,89 s |

Gli stessi 13 test falliscono in entrambe le modalità Linear; nessuna differenza di correttezza tra ACCEL=0 e ACCEL=1. I sei fallimenti transitori osservati nella prima matrice dopo la promozione a registro (`591_goto_typedef_open`, `831_real_a01_sieve`, `834_real_a04_matmul`, `841_real_a08_text`, `848_real_a12_fact`, `945_sibling_for_same_name`) sono stati riprodotti con promozione disattivata e chiusi tramite correzione della causa comune: la promozione prende il tipo dal metadato autorevole del lowering e i kernel specializzati sono bloccati soltanto quando la loro specifica espressione/statement tocca un locale promosso. Il set finale non contiene regressioni nuove rispetto ai residui rimasti dopo i fix.

## Correzioni verificate in questo delta

1. **Loop-local register promotion.** Tipo del locale promosso ricavato dai metadati del lowering, esclusione dei pointer-slot senza dichiarazione affidabile (i pointer scalari sono ammessi solo quando il metadato conferma `TypePointer`), e guardia di coerenza localizzata all'effettiva interferenza tra statement e registri promossi. La scansione dei candidati è regionale: le scritture appartenenti soltanto a loop annidati non vengono promosse erroneamente dal loop esterno. Le call e gli effetti indiretti restano conservativi.
2. **874_array_member_storage — assegnamento a membro/array dentro struct annidate.** Root cause: un resolver trattava `n.item` come nome di globale `item`, perché assumeva una catena identificatore→array diretta. Il lowering ora risolve tipo e stride dall'intero percorso di membri e usa metadati runtime per l'array membro con bounds/init-map e store condivisi. Anche incrementi su elementi indicizzati di array membro vengono linearizzati correttamente. Output Tree e Linear: `10 22 30 61`.
3. **288_heap_size_classes — cast a puntatore seguito da indice.** L'espressione-indice non aveva `ElemSize`/`TypeFlags` propri; lo stride deve provenire dal nodo di cast a puntatore e dal suo pointee. Ora il load calcola l'indirizzo con lo stride del tipo puntato e usa il load pointer condiviso. Test `288_heap_size_classes` passa Linear STRICT con ACCEL=0/1.
4. **789_vm_pointer_fallback — array locale di puntatori inizializzato da espressioni e `char **` (`p[0][0]`).** Root cause del rifiuto originale: `int *a[N]` era ammissibile soltanto se zero-inizializzato. Ora l'allocazione resta nel kernel comune, mentre ciascun inizializzatore viene abbassato a Linear e scritto via store puntatore condiviso. Il kernel non rivaluta le stesse espressioni con `IrEvalExpr`. Test `789_vm_pointer_fallback` passa Linear STRICT con ACCEL=0/1 e stampa `7 31 111` come Tree.
5. **Test `1009_nested_member_multidim_array` —** aggiunto primo ID libero per accesso/incremento ad array multidimensionale dentro un membro di struct annidata. Passa Tree e Linear STRICT con entrambi i valori di ACCEL. Expect: `1 2 61`.
6. **Regressioni collegate ai percorsi di indirizzo** `260_alias_addr_member`, `498_bitfield_addr` e `923_aos_member_load_store` sono state corrette tramite tipo/stride condivisi e controllo bitfield basato sulla dichiarazione del membro; tutti passano.

La scansione del codice non trova le tracce temporanee usate durante la diagnosi (`BABYC_CAST_INDEX_TRACE`, `[cast-index]`, `BABYC_ADDR_TRACE`, `BABYC_EXPR_TRACE`, `execinfo.h` aggiunto per debug, né tracce equivalenti). La promozione ammette ora anche un locale puntatore solo se i metadati del lowering confermano `TypePointer`, lo slot non è un array posseduto/fptr e non è preso l’indirizzo; i benchmark non dimostrano comunque un vantaggio prestazionale sistematico per questa modifica.

## Residui Linear STRICT — 13 test

- **Eligibility / lowering di programmi legacy o espressioni complesse:** `047_grep`, `058_macro_bug`, `062_initializers`, `126_csmith_modulo_zero`. Per `047_grep`, `BABYC_DUMP_LIN=1` non emette record `[lin-ir]` per il corpo che cade in fallback; `BABYC_VM_PROFILE=1` continua a mostrare `strict: tree fallback (body)` senza un `lower_reject`, quindi il rifiuto avviene prima di produrre un dump di funzione utile. Il predicato di eligibility preciso resta aperto. `062_initializers` riguarda inizializzatori globali/aggregati; `126_csmith_modulo_zero` un'espressione generata complessa con modulo/side effects. Non è stata dimostrata una singola causa comune e il primo residuo (`047_grep`) è solo stato triagiato, non corretto.
- **Call indirette, function pointer, callback e ordine degli effetti:** `454_complex_dispatch`, `606_storm_inc_dec_member_call`, `609_storm_enum_union_fptr`, `618_inc_member_call_arg`, `947_qsort_bsearch`, `948_qsort_stable`. Il dump/trace conferma i rifiuti nei corpi con call indirette o callback; manca ancora l'identificazione e correzione completa del punto radice per l'intero cluster. I due casi che condividevano il vecchio prefisso `379` — `395_array_fn_loop_local_global` e `396_complex_inventory` — non falliscono e non compaiono nella matrice finale.
- **Varargs/intrinsic host SQLite:** `651_sqlite_exec_null_varargs`; lowering del corpo non accetta ancora il caso `sqlite3_exec` con argomenti nulli variadici.
- **Qualificatori/alignment in aggregate:** `873_alignment_extended`; il dump rifiuta un'espressione unaria/member nel percorso con membro `volatile` e layout esteso. La causa esatta del rifiuto resta aperta.
- **Ritorno struct con effetti e store nell'array:** `893_vm_struct_return_effect_fallback`; il corpo viene rifiutato nel percorso di store aggregato dopo una chiamata che restituisce struct.

Prima del cambio di default a `BABYC_PATH=linear` serve portare a zero questi 13 fail in STRICT. Il bytecode resta escluso; la compatibilità MSVC è lasciata alla verifica locale dell'utente.

## Benchmark A/B — candidato contro binario della baseline

Tutte le otto combinazioni richieste (`string_scan`, `string_scan_bounded`, `struct_score`, `dfs_rec` × ACCEL=0/1) hanno almeno **7 coppie alternate baseline/candidato**, output ed exit status identici. Sul binario finale sono state aggiunte altre **21 coppie alternate** per `string_scan` ACCEL=0, `string_scan_bounded` ACCEL=1, `struct_score` ACCEL=1 e `dfs_rec` ACCEL=1. I CSV grezzi sono `benchmark_ab_2026-10-18.csv` e `benchmark_repeatcheck_negative_2026-10-18.csv`.

La metrica sintetica è la mediana dei rapporti appaiati `(candidato/baseline - 1)`. Valori negativi indicano il candidato più veloce. I tempi singoli sono molto variabili (fino a rapporti fuori da ±40% e, sui workload di pochi millisecondi, oltre ±100%); per questo la metrica va interpretata con cautela e i CSV restano la fonte dei grezzi.

| Benchmark | ACCEL | Coppie | Mediana delta appaiato | Range osservato | Nota |
|---|---:|---:|---:|---:|---|
| `string_scan` | 0 | 28 | +5,81% | −3,8%…+13,7% | Mediana sfavorevole anche nel controllo esteso |
| `string_scan` | 1 | 7 | +10,12% | −48,7%…+130,4% | Campione rumoroso, non certifica una regressione causale |
| `string_scan_bounded` | 0 | 7 | +7,38% | −26,5%…+16,0% | Campione breve e disperso |
| `string_scan_bounded` | 1 | 28 | +4,37% | −49,6%…+141,9% | Mediana sfavorevole, rumore elevato |
| `struct_score` | 0 | 7 | +7,06% | −22,8%…+47,6% | Campione disperso |
| `struct_score` | 1 | 28 | −8,77% | −50,6%…+148,0% | Mediana appaiata favorevole, ma instabile/outlier-dipendente |
| `dfs_rec` | 0 | 7 | +7,53% | −11,1%…+11,8% | Campione disperso |
| `dfs_rec` | 1 | 28 | +2,10% | −27,0%…+39,2% | Mediana lievemente sfavorevole |

**Gate prestazioni: NON PASSATO / non certificabile come “zero peggioramenti”.** In più combinazioni la mediana appaiata è sfavorevole; `string_scan` ACCEL=0 mantiene circa +5,8% nel campione di 28 coppie. Gli altri workload presentano dispersione troppo alta per attribuire con sicurezza la differenza a una regressione architetturale, ma questo non è un motivo per dichiarare PASS. È stata verificata l'ipotesi che la sola promozione a registro spieghi i tempi scan: attivarla/disattivarla nel binario candidato ha dato variazioni piccole e discordanti, quindi la causa prestazionale non è ancora isolata. Nessun fast path specifico di benchmark è stato introdotto.

## Ancora aperto

- 13 residui Linear STRICT elencati sopra; il default non è stato cambiato.
- Gate prestazionale “nessun peggioramento” resta NON PASSATO: oltre alla conferma meno rumorosa sui workload brevi occorre isolare la causa dei delta sfavorevoli osservati, in particolare `string_scan` ACCEL=0.
- Nessun test Windows/MSVC, sanitizer completo, fuzzing lungo o gate bytecode in questo turno.
