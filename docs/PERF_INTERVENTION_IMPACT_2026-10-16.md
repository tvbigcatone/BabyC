# BabyC — impatto misurato degli interventi prestazionali

Data: 2026-10-16

## Metodo

- GCC `-O2`, build pulita per ogni variante; nessun `-pg` e nessuna strumentazione di profiling durante i tempi.
- CPU fissata con `taskset -c 0`; 1 warm-up e 9 esecuzioni misurate per variante, ordine intercalato.
- Ambiente identico: `BABYC_EXEC=vm BABYC_NO_KERNEL=1 BABYC_NO_SHAPE_FP=1`.
- Tempo wall-clock end-to-end con `time.perf_counter`; output verificato a ogni esecuzione.
- Questi tempi includono avvio del processo e sono utili per rilevare soltanto effetti abbastanza grandi. I campioni anomali e la sovrapposizione degli intervalli impediscono di attribuire variazioni piccole con certezza.

Varianti:
- **A — diagnostic**: baseline di confronto, baseline fissa + delta diagnostico, prima delle due ottimizzazioni misurate.
- **B — wrapper**: A + sostituzione dei richiami `IrUninitChecksOn()` con chiamate dirette a `IrSafeChecksOn()` nei call-site modificati.
- **C — snapshot**: B + rimozione dell’azzeramento della coda inattiva `S->WideOn[WideMax..IR_LIN_STMT_MAX)` da `FnCodeSnapshot()`.

## Risultati

| Caso | Variante | Mediana (s) | Min–max (s) |
|---|---:|---:|---:|
| i01_collatz | A — diagnostic | 0.524796 | 0.496917–0.543879 |
| i01_collatz | B — wrapper | 0.515470 | 0.505457–0.636625 |
| i01_collatz | C — snapshot | 0.537504 | 0.519055–0.622832 |
| i03_primes | A — diagnostic | 0.362253 | 0.352881–0.396311 |
| i03_primes | B — wrapper | 0.367398 | 0.357726–0.380190 |
| i03_primes | C — snapshot | 0.362441 | 0.351345–0.403381 |

## Variazione delle mediane

| Caso | B rispetto ad A (wrapper) | C rispetto a B (snapshot) | C rispetto ad A (cumulativo) |
|---|---:|---:|---:|
| i01_collatz | -1.78% | +4.27% | +2.42% |
| i03_primes | +1.42% | -1.35% | +0.05% |

## Interpretazione onesta

- **Rimozione wrapper:** nessun guadagno misurabile con affidabilità in questi due test. Le mediane cambiano di −1.78% su i01 e +1.42% su i03, con intervalli sovrapposti. L’intervento elimina un livello di chiamata nel codice ma l’effetto temporale è sotto la risoluzione affidabile di questa prova.
- **Coda dello snapshot:** risultato misto: +4.27% (più lento) su i01 e −1.35% (più veloce) su i03 rispetto a B. Gli intervalli si sovrappongono; non è prova né di regressione né di beneficio. La scrittura rimossa è reale, ma il beneficio end-to-end non è dimostrato.
- **Impatto cumulativo:** +2.42% su i01 e +0.05% su i03 rispetto ad A; con la variabilità osservata non è possibile attribuire questi scarti al codice.
- Output verificati invariati: i01 `52527 339 6134916`; i03 `17984 1709600813` per tutte le esecuzioni.
- Build GCC riuscita senza warning/errori nei log per A, B e C. Questa tranche non certifica suite completa, sanitizer o MSVC.

## Decisione

Nessun nuovo ramo di esecuzione è stato introdotto. Per ora non dichiaro alcuna delle due modifiche come accelerazione dimostrata. La rimozione del wrapper resta una semplificazione a costo semantico nullo; la rimozione dell’azzeramento resta un’eliminazione di scrittura ridondante, ma va rivalutata su workload più stabili prima di attribuirle un guadagno. I prossimi interventi dovranno includere questa stessa misura A/B/C e un controllo di regressione sui workload non mirati.
