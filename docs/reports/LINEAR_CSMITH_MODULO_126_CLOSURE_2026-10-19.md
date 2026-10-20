# Linear STRICT — chiusura `126_csmith_modulo_zero` (19/10/2026)

## Baseline e perimetro

- Baseline esclusiva: `babyc_baseline_2026-10-18_loop_promotion.zip`, SHA-256 `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`.
- Delta cumulativo in ingresso: chiusura `047_grep`, SHA-256 `386572a89e5f77aee3de3e801166a709b9305b4745199130791171c87e5c45d8`.
- Ricostruzione: estrazione della baseline, applicazione dei 2.023 path di `babyc/DELETE_FILES.txt` (nessuno mancante), quindi sovrapposizione del delta cumulativo.
- Obiettivo del turno: eliminare il solo residuo Linear STRICT `126_csmith_modulo_zero`; mantenere tutti gli `.expect` preesistenti byte-identici e aggiungere soltanto il test 1016.

## Diagnosi dalla traccia

Con `BABYC_DUMP_LIN=1 BABYC_VM_PROFILE=1` il primo rifiuto non era causato dall'operatore `%`: il lowering di `main` rifiutava la formazione dell'indirizzo dell'elemento di un array globale tridimensionale:

```c
g_274 = &g_126[1][3][2];
```

Il dump riportava `lower_reject` sulla riga 123 e, a cascata, `compile_reject=lower` / `vm-rej: compile-null`. Dopo aver corretto quel punto in modo generale, il compilatore raggiungeva altri costrutti usati dal corpo Csmith e li rifiutava a loro volta. La diagnosi completa ha separato quattro lacune generali:

1. **Indirizzo di array globale multidimensionale.** `LinEmitAddressOf` non calcolava il passo row-major di ogni livello per `&g[i][j][k]`. Ora appiattisce la catena degli indici nell'ordine sorgente e scala ogni indice per la dimensione dell'oggetto/subarray al livello corrispondente, con addizione indirizzo larga. Le catene oltre il limite dichiarato vengono rifiutate senza troncamento silenzioso.
2. **Inizializzatori di array di puntatori.** Il lowering rifiutava l'inizializzazione quando `DeclInitVals` era NULL anche se erano presenti espressioni in `DeclInitExprs`; inoltre tratteneva registri temporanei per tutti gli elementi fino alla fine della dichiarazione. Ora la guardia ammette il vettore di espressioni e, poiché ogni elemento viene valutato e memorizzato prima del successivo, riutilizza i temporanei fra elementi e invalida il CSE fra valutazioni. Questo evita di esaurire il budget di registri con array di puntatori ordinari.
3. **Operatore virgola C.** `TokenComma` viene ora abbassato come sequenziamento: valuta il lato sinistro per gli effetti, invalida le informazioni CSE/cache potenzialmente stantie e restituisce il valore del lato destro. I gate dei tipi FP/puntatore non trattano più la virgola come un operatore numerico ordinario.
4. **Dereferenziazione scalare come argomento per valore.** La validazione delle chiamate non respinge più in blocco `f(*p)` quando la dereferenziazione produce uno scalare o un puntatore. I puntatori ad aggregati (array/struct/union) restano soggetti al percorso ABI aggregato separato.

Questi interventi sono basati sul tipo e sulla struttura dell'espressione, senza nomi del test, forme specifiche del programma Csmith o nuovo fallback. La causa non è condivisa con il precedente `047_grep`: 047 era un problema di eleggibilità di `argv[i][j]` via parametro `char **`, risoluzione differita di un globale irraggiungibile e prova di sicurezza del pointer-plus-offset. `126` riguarda invece indirizzi multidimensionali globali, inizializzatori pointer-array, sequenziamento con virgola e dereferenziazione come argomento.

## Output atteso

Dopo il fix, Tree e Linear STRICT eseguono il programma fino allo stesso errore didattico di runtime. Il processo termina con return code 1 per l'errore previsto; l'output normalizzato corrisponde esattamente al `.expect`:

```text
126_csmith_modulo_zero.c:93:357 modulo by zero
```

Il dump post-fix in `linear_126_dump_after_fix_2026-10-19.log` mostra `main` compilato (87 istruzioni) e il diagnostico di modulo per zero, senza `lower_reject`, `compile_reject`, `vm-rej` o `strict: tree fallback`.

## Nuovo test e `.expect`

Aggiunto `src/tests/1016_global_multidim_addr_comma_ptr_init.c/.expect`. Unisce in un test indipendente: indirizzamento `&cube[i][j][k]` in un globale 3D, inizializzatori non-costanti per 25 slot puntatore (riuso dei registri), dereferenziazione dei valori memorizzati, effetti dell'operatore virgola e uso del valore destro.

Output del nuovo test in Tree e Linear STRICT, ACCEL 0 e 1:

```text
73 73 41 3 41
```

Ho confrontato i `.expect` presenti nel delta 047 con la ricostruzione: **zero file differenti, zero assenti**. L'unico `.expect` nuovo è quello del test 1016; nessun `.expect` esistente è stato modificato.

## Build e matrice completa

Clean build Linux/GCC: **0 warning, 0 errori**. Log salvato in `build_clean_126_2026-10-19.log`.

Riepiloghi reali della matrice eseguita sul candidato, 1008 test numerati con `.expect`:

```text
SUMMARY tree_accel0: PASSED=1008 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1008 SECONDS=4.52
FAILURES: none
SUMMARY tree_accel1: PASSED=1008 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1008 SECONDS=4.47
FAILURES: none
SUMMARY linear_accel0_STRICT: PASSED=1008 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1008 SECONDS=3.99
FAILURES: none
SUMMARY linear_accel1_STRICT: PASSED=1008 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1008 SECONDS=4.34
FAILURES: none
```

I log integrali sono `matrix_126_tree_a0.log`, `matrix_126_tree_a1.log`, `matrix_126_linear_strict_a0.log` e `matrix_126_linear_strict_a1.log`. `047`, `058`, `062`, `651`, `1015` e `1016` passano nelle quattro combinazioni; non restano residui nella matrice corrente.

## A/B prestazionale contro il binario 047

A è un binario ricostruito dal codice 047 precedente al fix 126; B è il candidato. Benchmark eseguiti con `BABYC_PATH=linear BABYC_STRICT=0`, processi alternati AB/BA, output/return code verificati. Ogni combinazione ha almeno 7 coppie; `dfs_rec` ACCEL 1 è stato esteso a 21 coppie dopo una mediana iniziale sfavorevole. I campioni individuali sono in `linear_126_ab_samples_2026-10-19.csv`.

| Benchmark | ACCEL | Coppie | Mediana A (ms) | Mediana B (ms) | Delta mediane | Mediana delta appaiato | Range appaiato |
|---|---:|---:|---:|---:|---:|---:|---:|
| `string_scan` | 0 | 7 | 292.333 | 296.518 | +1.43% | +0.07% | −6.83%…+2.16% |
| `string_scan` | 1 | 7 | 5.190 | 4.873 | −6.10% | −2.45% | −7.62%…+26.84% |
| `string_scan_bounded` | 0 | 7 | 417.231 | 400.525 | −4.00% | +0.60% | −8.29%…+7.83% |
| `string_scan_bounded` | 1 | 7 | 8.906 | 6.648 | −25.36% | −20.97% | −55.30%…+19.03% |
| `struct_score` | 0 | 7 | 70.131 | 65.926 | −6.00% | −1.60% | −18.23%…+9.20% |
| `struct_score` | 1 | 7 | 1.691 | 1.701 | +0.62% | +0.45% | −6.87%…+9.22% |
| `dfs_rec` | 0 | 7 | 28.610 | 28.853 | +0.85% | +1.22% | −27.43%…+15.74% |
| `dfs_rec` | 1 | 21 | 11.628 | 11.671 | +0.36% | −5.35% | −43.54%…+64.47% |

Gli stdout hash coincidono per tutte le esecuzioni di ogni benchmark/configurazione. Ho inoltre comparato i dump Linear con `BABYC_DUMP_LIN=1`: dopo la normalizzazione del solo indirizzo `function_ptr`, stdout e dump IR coincidono esattamente per tutti gli otto casi, inclusi conteggi e sequenza d'istruzioni. File di confronto: `linear_126_ir_comparison_2026-10-19.json`.

Le misure accelerate molto brevi sono rumorose: per `dfs_rec` ACCEL 1 il delta grezzo rimane +0.36% dopo 21 coppie e la dispersione appaiata è ampia; la mediana appaiata è −5.35%. Non emerge una variazione dell'IR dei benchmark, ma questo non è una prova matematica di overhead nullo. Riporto i valori grezzi nel CSV anziché dichiarare un costo zero assoluto.

## Compatibilità e punti aperti

Il codice aggiunto usa C portabile e non introduce header POSIX o estensioni GCC. **MSVC non è stato eseguito in questo ambiente**, quindi la build Windows resta da verificare. Sul candidato Linux, build, output, matrice e confronto IR sono completi; non resta un fallimento Linear STRICT noto in questa matrice.
