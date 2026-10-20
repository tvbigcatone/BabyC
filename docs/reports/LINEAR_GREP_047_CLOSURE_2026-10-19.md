# Linear STRICT closure — `047_grep` (2026-10-19)

## Baseline, delta e obiettivo

Baseline esclusiva: `babyc_baseline_2026-10-18_loop_promotion.zip`, SHA-256 `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`.
Delta cumulativo ricevuto: chiusura 651, SHA-256 `f77e4ef698b4c83df642ecf01a3e3a35f821a0ad0913e9796ba65acbb95a92e6`. Ricostruzione: estrazione baseline, applicazione di `DELETE_FILES.txt`, sovrapposizione del delta.

**Obiettivo raggiunto sul piano della correttezza:** `047_grep` passa in Linear STRICT senza fallback, con output uguale a Tree e `src/tests/047_grep.expect`, sia con ACCEL=0 sia con ACCEL=1. È stato aggiunto soltanto il test numerato `1015_param_ptrptr_index.c/.expect`; nessun `.expect` preesistente è stato modificato.

## Diagnosi dalla traccia

La traccia iniziale dava `compile-null` e il fallback `strict: tree fallback (body)`. La strumentazione diagnostica temporanea ha reso visibile il primo rifiuto:

```text
[lin-elig-reject] expr line=3099 kind=7 op=0 ident=- src=488 char=29 left=7 right=1
[lin-elig-reject] expr line=3209 kind=3 op=20 ident=- src=488 char=33 left=7 right=1
[lin-elig-reject] expr line=3210 kind=3 op=16 ident=- src=488 char=33 left=3 right=3
[lin-elig-reject] stmt line=3354 kind=3 src=488 char=5 expr=3 decl=- arr=0 ptr=0 fptr=0 from=-1
```

In `src/tests/047_grep.c:488` l'espressione è `argv[1][0]` (e subito dopo `argv[1][1]`), in cui la radice `argv` è il parametro `char **` di `main`. `IrExprIntegerOk()` rifiutava esplicitamente ogni indice annidato la cui radice fosse un parametro, pur avendo già una via tipizzata per l'accesso indiretto. Ho rimosso il veto generalizzato e lasciato che l'eleggibilità strutturale ammetta la radice identificatore; l'emettitore sceglie poi il percorso di indice tipizzato appropriato.

Questo ha esposto il secondo ostacolo, non una regressione nuova: durante il lowering si analizzano entrambi i lati delle condizioni runtime. Il ramo `if (argc == 2 && argv[1][0] == '?' ...)` richiama `help(documentation)`, ma la definizione `documentation` nel file è esclusa da `#if 0`. La prima correzione dell'indicizzazione faceva quindi fallire prematuramente il lowering con `'documentation' is undefined`, anche se nel test senza argomenti quel ramo è irraggiungibile.

`GlobPush()` in `src/ir_lin_emit.c` usava un lookup globale rigido durante il lowering. Ora usa `IrGlobalValSoft()` per creare un riferimento globale differito. `IrLinLoadGlobal`, nel momento in cui l'istruzione viene realmente eseguita, effettua ancora il lookup rigido con `IrGlobalVal()`: un identificatore non dichiarato in un ramo effettivamente eseguito non viene trasformato silenziosamente in un valore valido.

Dopo queste due correzioni, `main` veniva emesso ma il verificatore `FnCodeIndexedPointerParamsSafe()` rifiutava il corpo come `ptr-indexed`. La traccia temporanea indicava `p=1 i=161 op=2`: la base del parametro viene usata nell'aritmetica indirizzo+offset emessa prima del load indiretto tipizzato. Ho esteso la prova strutturale a riconoscere soltanto la forma dell'emettitore `IrLinBin` con `TokenPlus` e `IR_LIN_F_WIDE`; gli altri usi aritmetici di un parametro puntatore restano soggetti al controllo esistente. Il dump finale mostra `function=main ... instructions=254` e nessun `vm-rej`, `compile_reject`, `lower_reject` o fallback per 047.

Nessuna condizione verifica il nome `047`, il nome `grep` o la forma esatta del programma. Il nuovo test 1015 copre il caso generale attivo di indicizzazione annidata via parametro puntatore:

```c
static int nested_char(char **text) { return text[0][1]; }
```

Output Tree/Linear STRICT: `98` a entrambi i valori ACCEL.

## Matrice completa della ricostruzione

Le righe SUMMARY/FAILURES emesse dai gate completi, 1.007 `.expect` numerate:

```text
SUMMARY tree_accel0: PASSED=1007 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1007 SECONDS=18.19
FAILURES: none
SUMMARY tree_accel1: PASSED=1007 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1007 SECONDS=28.92
FAILURES: none
SUMMARY linear_accel0_STRICT: PASSED=1006 FAILED=1 TIMEOUT=0 SKIPPED=0 TOTAL=1007 SECONDS=16.57
FAILURES: 126_csmith_modulo_zero.c
SUMMARY linear_accel1_STRICT: PASSED=1006 FAILED=1 TIMEOUT=0 SKIPPED=0 TOTAL=1007 SECONDS=15.24
FAILURES: 126_csmith_modulo_zero.c
```

Tree passa 1007/1007 a entrambi i livelli ACCEL. Linear STRICT passa 1006/1007 a entrambi: rimane soltanto `126_csmith_modulo_zero`. I log completi sono `matrix_047_tree_a0.log`, `matrix_047_tree_a1.log`, `matrix_047_linear_strict_a0.log` e `matrix_047_linear_strict_a1.log`.

**Nota sulla riproducibilità del test preesistente 535:** durante un singolo rilancio integrale sulla ricostruzione finale, `535_main_continue_after_extract` ha fallito quando il programma ha stampato `Tempo esecuzione : 1 ms`, mentre il suo `.expect` fissa `0 ms`. Il sorgente misura un intervallo reale con `clock()`, quindi quell'asserzione d'output è sensibile alla schedulazione. L'ho ripetuto 12 volte sul candidato e 20 volte sul binario 651 pre-fix: in entrambi i binari il valore fluttua tra 0 e valori positivi (sul binario pre-fix ho osservato anche 3, 5 e 8 ms). È dunque una flakiness già presente prima del fix 047, non una regressione introdotta qui. Un ulteriore rilancio completo sulla ricostruzione finale è passato su 535 e ha prodotto le quattro SUMMARY riportate sopra, con solo 126 residuo. Nessun `.expect` è stato modificato e non è stato introdotto alcun filtro nel runner per nascondere questa variabilità.

Il residuo 126 **non ha causa comune dimostrata con 047**. La sua traccia distinta è:

```text
[vm-try] body=... depth=1 kind=2
[babyc-exec-profile] lower_reject stmt_kind=1 line=123 expr_kind=4 expr_op=0
[babyc-exec-profile] compile_reject=lower body_kind=2 n=0 ok=0 skipped_call=0 calls=0
[vm-rej] compile-null
```

È una reiezione di un'espressione binaria in un corpo Csmith con `%`/divisione e valutazioni laterali (test di modulo per zero); 047 invece falliva per indicizzazione `char **`, lookup anticipato di un identificatore in ramo non eseguito e relativa prova di sicurezza del puntatore. I percorsi/ragioni sono diversi. Non ho alterato 126 nel tentativo di chiudere 047.

## Build e regressioni

Build Linux/GCC dal clean state completata con `-Wall -O2`, senza warning/errori. Non ci sono header POSIX o estensioni GCC nelle modifiche. MSVC/Windows non è stato eseguito in questo ambiente, quindi la compilazione MSVC resta non verificata.

Il test 047 e il nuovo 1015 passano in Tree e Linear STRICT con ACCEL=0/1. L'intero corpus Tree non mostra regressioni; Linear STRICT chiude il residuo 047 e ne conserva soltanto uno, 126. Non sono stati cambiati gli `.expect` precedenti.

## A/B prestazioni (binario 651 vs candidato 047)

I confronti usano Linear, `BABYC_STRICT=0` come nei benchmark standard, con output e return code controllati. A = binario ricostruito dal delta 651 prima del fix; B = candidato. Le coppie alternano AB/BA. Tutti gli stdout hash coincidono per ciascun benchmark/configurazione. Oltre alla serie wall-time sono state raccolte 7 coppie addizionali misurando anche il CPU time del child (`getrusage`); dettagli grezzi in `linear_047_ab_samples_2026-10-19.csv` e `linear_047_ab_cpu_samples_2026-10-19.csv`.

| Benchmark | ACCEL | Coppie wall | Mediana A | Mediana B | Delta mediane grezze | Mediana delta appaiato | IQR delta appaiato |
|---|---:|---:|---:|---:|---:|---:|---:|
| `string_scan` | 0 | 36 | 788,24 ms | 829,91 ms | +5,29% | +2,04% | −11,28% .. +30,96% |
| `string_scan` | 1 | 7 | 15,79 ms | 15,34 ms | −2,84% | −0,58% | −2,36% .. +14,04% |
| `string_scan_bounded` | 0 | 36 | 987,67 ms | 1.015,41 ms | +2,81% | −0,79% | −13,82% .. +25,15% |
| `string_scan_bounded` | 1 | 36 | 23,29 ms | 26,36 ms | +13,18% | +11,00% | −26,33% .. +31,61% |
| `struct_score` | 0 | 7 | 171,85 ms | 167,27 ms | −2,66% | −2,66% | −11,87% .. +10,70% |
| `struct_score` | 1 | 7 | 11,13 ms | 8,00 ms | −28,14% | −24,44% | −33,19% .. +13,88% |
| `dfs_rec` | 0 | 36 | 69,93 ms | 74,42 ms | +6,43% | +0,77% | −14,50% .. +24,20% |
| `dfs_rec` | 1 | 36 | 30,91 ms | 30,55 ms | −1,17% | −1,97% | −16,73% .. +32,04% |

Per i primi risultati sfavorevoli sono state aggiunte coppie fino a 36. I dati restano molto rumorosi; in particolare `string_scan_bounded` ACCEL=1 mostra +13,18% sulle mediane grezze e +11,00% sulla mediana dei rapporti appaiati, ma l'IQR è molto largo e attraversa zero. Non nascondo questo segnale.

Come controllo del rumore, sette coppie **A/A con lo stesso identico binario candidato** hanno prodotto variazioni wall-time run2/run1 tra −57,4% e +60,3% per `string_scan` ACCEL=0 e tra −21,4% e +52,9% per `string_scan_bounded` ACCEL=1. Questo livello di variabilità della macchina spiega perché le misure di breve durata non consentano di attribuire causalmente i delta grezzi al cambiamento di codice.

La verifica strutturale dei dump `BABYC_DUMP_LIN=1` mostra inoltre che per tutti gli otto abbinamenti benchmark/ACCEL l'IR emesso è identico dopo aver rimosso solo l'indirizzo `function_ptr` dalla stampa: stessa sequenza di istruzioni e stessi conteggi. I dettagli sono in `linear_047_ir_dump_comparison.txt`. Quindi non è cambiato il percorso/instruction stream runtime dei quattro workload. Tuttavia, la condizione di prestazione “nessuna perdita” **non può essere certificata in modo assoluto** da queste esecuzioni contaminate dal rumore: non dichiaro costo zero provato né nascondo il dato sfavorevole di `string_scan_bounded` ACCEL=1. Non è emersa una differenza di IR che spieghi quel delta; per una conclusione più stretta serve una nuova misura sotto carico controllato.

Il controllo A/A è `linear_047_aa_noise_control_2026-10-19.csv`.

## Artefatti di diagnosi

- `linear_047_dump_before_fallback.log`: dump iniziale.
- `linear_047_dump_eligibility_reject.log`: ragioni di eleggibilità rilevate temporaneamente.
- `linear_047_dump_pointer_gate_reject.log`: rifiuto del verificatore puntatori dopo la prima correzione.
- `linear_047_dump_after_strict_accel0.log`: dump dopo la correzione, senza rifiuto/fallback 047.
- `linear_126_residual_dump.log`: prova che 126 resta un residuo separato.

Le strumentazioni diagnostiche temporanee sono state rimosse dal codice consegnato.
