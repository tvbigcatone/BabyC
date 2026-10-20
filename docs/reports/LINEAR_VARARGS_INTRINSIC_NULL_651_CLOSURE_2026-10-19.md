# Linear STRICT — chiusura `651_sqlite_exec_null_varargs` (2026-10-19)

## Baseline e costruzione

Baseline esclusiva: `babyc_baseline_2026-10-18_loop_promotion.zip` — SHA-256 `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`. Il pacchetto è cumulativo, applicato dopo la cancellazione dei percorsi in `babyc/DELETE_FILES.txt`. Il delta cumulativo ricevuto in partenza (chiusura 058) aveva SHA-256 `e2695a8c71f3f4b6e9d2753091853b3ce0b3c3c4c74f714ffc0418ede6ceac18`.

## Diagnosi: dump prima della modifica

Il comando era Linear STRICT + dump/profiling abilitato. Il risultato significativo, conservato in `linear_varargs_651_before_dump_2026-10-19.log`, è:

```text
[vm-try] body=0x55fb69ff0570 depth=1 kind=2
[babyc-exec-profile] compile_reject=lower body_kind=2 n=25 ok=1 skipped_call=1 calls=2
[vm-rej] compile-null
651_sqlite_exec_null_varargs.c:1:21 strict: tree fallback (body)
[babyc-bytecode-vm-prod] attempts=0 runs=0
```

Il call trace precedente alla modifica (`linear_varargs_651_before_calltrace_2026-10-19.log`) mostra l'ingresso nel lowering di `sqlite3_open`, `sqlite3_exec` e `sqlite3_close`; `printf` è invece ammessa. La body lowering non falliva perché i parametri fossero troppi o perché `NULL` non potesse essere impacchettato: il contatore `skipped_call=1` indica che una chiamata è stata scartata dal gate.

## Causa radice e fix generale

Le tre API SQLite sono funzioni `Intrinsic` registrate nell'interprete. Il code path runtime comune in `IrCallHostFromRegs` consegna i parametri nell'array `Value[]` a `PicocCallIntrinsic`; il packer supporta già gli argomenti varargs e preserva spazio sufficiente per valori/puntatori. Tuttavia, `src/ir_lin_emit.c` applicava la whitelist dei nomi host anche alle Intrinsic risolte, rigettando nomi non elencati e impostando `skipped_call=1`, da cui `compile_reject=lower` e il fallback STRICT.

Ora l'emettitore permette il percorso generico per ogni funzione già risolta la cui definizione contiene un puntatore `Intrinsic` registrato. La whitelist continua a bloccare i nomi host non risolti, così il fix non apre arbitrariamente chiamate esterne sconosciute. Non c'è alcuna condizione su `sqlite3_*`, sul file 651 o sulla forma della chiamata; non è stato introdotto fallback.

## Output e regressione nuova

Il test storico `651_sqlite_exec_null_varargs` restituisce `rc=0`, uguale al suo `.expect` in Tree e Linear STRICT con ACCEL=0/1. Il dump dopo la modifica contiene un programma Linear (39 istruzioni) ed esegue con `rc=0`; non ha `compile_reject`, `vm-rej` o `strict: tree fallback`.

Nuovo `1014_intrinsic_varargs_null.c/.expect`: invoca open/exec/close e testa le code varargs con sia `0` sia `NULL`:

```text
open=0
zero=0
null=0
close=0
```

L'output è identico in Tree e Linear STRICT a entrambi i valori ACCEL. Nessun `.expect` preesistente è stato modificato. La matrice finale ha 1.006 test `.expect`.

## Build e matrice completa, dalla ricostruzione finale

Clean build Linux/GCC: 0 warning, 0 errori. Righe riepilogative reali dei quattro log eseguiti sulla ricostruzione del candidato finale:

```text
SUMMARY tree_accel0: PASSED=1006 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1006 SECONDS=4.28
FAILURES: none
SUMMARY tree_accel1: PASSED=1006 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1006 SECONDS=4.20
FAILURES: none
SUMMARY linear_accel0_STRICT: PASSED=1004 FAILED=2 TIMEOUT=0 SKIPPED=0 TOTAL=1006 SECONDS=3.60
FAILURES: 047_grep.c 126_csmith_modulo_zero.c
SUMMARY linear_accel1_STRICT: PASSED=1004 FAILED=2 TIMEOUT=0 SKIPPED=0 TOTAL=1006 SECONDS=3.61
FAILURES: 047_grep.c 126_csmith_modulo_zero.c
```

Questo è zero nuove regressioni rispetto al delta 058 validato in ingresso: il residuo 651 è chiuso e gli unici residui Linear STRICT sono 047 e 126. La loro causa non è quella del gate che rifiutava le Intrinsic registrate, pertanto non sono stati modificati opportunisticamente in questo turno.

I log completi sono accanto a questo report come `matrix_linear_varargs_651_*.log`.

## A/B alternato (delta 058 vs fix 651)

Sono stati confrontati binari buildati dallo stesso albero, uno con `ir_lin_emit.c` precedente al fix 651 e uno con il fix isolato. I test benchmark girano con `BABYC_PATH=linear` e ACCEL=0/1, senza STRICT perché alcuni workload (in particolare `string_scan`) fanno fallback proprio in STRICT. Tutte le misure hanno identico codice sorgente e gli hash stdout sono identici fra i due binari.

Ogni configurazione ha almeno 21 coppie AB/BA. Per cinque combinazioni con mediane grezze iniziali sfavorevoli sono state raccolte 15 coppie addizionali (36 totali): `string_scan` ACCEL=1, `string_scan_bounded` ACCEL=0/1, `struct_score` ACCEL=1 e `dfs_rec` ACCEL=0. I numeri non vanno interpretati come benchmark deterministico: i tempi ACCEL brevi hanno outlier e ampia dispersione. Dati riepilogati (percentuali positive = candidato più lento):

| Benchmark | ACCEL | Coppie | Mediana 058 | Mediana 651 | Delta grezzo | Mediana delta appaiato | IQR delta appaiato | Range delta |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `string_scan` | 0 | 21 | 0.309943s | 0.306944s | -0.97% | +0.06% | -3.81%..+2.29% | -9.24%..+10.87% |
| `string_scan` | 1 | 36 | 0.005402s | 0.005485s | +1.54% | +1.28% | -6.35%..+9.58% | -75.74%..+64.77% |
| `string_scan_bounded` | 0 | 36 | 0.387807s | 0.386417s | -0.36% | +0.42% | -3.21%..+5.94% | -31.21%..+19.50% |
| `string_scan_bounded` | 1 | 36 | 0.006783s | 0.007025s | +3.58% | +0.67% | -7.41%..+13.69% | -39.44%..+67.08% |
| `struct_score` | 0 | 21 | 0.067862s | 0.067597s | -0.39% | -0.20% | -2.32%..+4.39% | -9.12%..+8.29% |
| `struct_score` | 1 | 36 | 0.001769s | 0.001688s | -4.57% | +1.66% | -12.02%..+9.03% | -60.21%..+53.45% |
| `dfs_rec` | 0 | 36 | 0.024936s | 0.025303s | +1.47% | +0.18% | -3.41%..+4.07% | -44.10%..+38.99% |
| `dfs_rec` | 1 | 21 | 0.009642s | 0.009461s | -1.89% | -2.55% | -8.91%..+0.88% | -32.81%..+15.86% |


Le mediane dei delta appaiati vanno da −2,55% a +1,66%, e tutti gli IQR elencati attraversano zero. I dati non sostengono una regressione persistente attribuibile alla modifica; allo stesso modo, rumore e ampiezza dei range non provano matematicamente overhead zero. Non sono state eliminate misure sfavorevoli. CSV integrale (243 coppie, cioè 21 per le tre configurazioni non estese e 36 per le altre cinque): `linear_varargs_651_perf_ab_samples_2026-10-19.csv`.

## Portabilità e limiti

- Il fix C è una condizione su puntatori/definizioni già esistenti e non aggiunge header POSIX, built-in o estensioni GCC.
- Build MSVC/Windows non eseguita in questo turno; quindi la portabilità è progettata, non certificata da una build locale.
- Tree resta l'oracolo e default. Linear STRICT ora copre 1.004/1.006; restano `047_grep` e `126_csmith_modulo_zero`.
- Bytecode compatto fuori ambito.
