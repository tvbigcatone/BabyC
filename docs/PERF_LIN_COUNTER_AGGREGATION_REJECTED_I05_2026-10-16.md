# BabyC performance tranche I05 — aggregazione del contatore Linear rifiutata

Data: 2026-10-16

## Ipotesi e intervento sperimentale

`IrExecLinLocals()` incrementa `BabycLinInstExec` a ogni istruzione anche quando `BABYC_COV` è disattivato. Il contatore viene mostrato da `IrPrintLowerStats()` solo se `BABYC_COV` è attivo. Ho confrontato due varianti sperimentali, senza conservarle nel codice di produzione:

1. controllare `BABYC_COV` all'ingresso e fare un `if` per ogni istruzione prima dell'incremento;
2. controllare `BABYC_COV` all'ingresso e sommare `steps` una sola volta dopo il ciclo.

La seconda variante mantiene il conteggio totale quando la copertura è attiva, ma cambia il momento in cui il contatore globale viene aggiornato. Entrambe sono state compilate con GCC `-O2`; la build della seconda variante è terminata senza warning o errori.

## Misure end-to-end

Confronto tra G (codice di produzione corrente) e H (variante sperimentale), `BABYC_EXEC=vm`, `BABYC_NO_KERNEL=1`, `BABYC_NO_SHAPE_FP=1`, output normalizzato e codice di uscita verificati. Le misure sono tempi di processo, quindi includono avvio e parsing. Le esecuzioni sono state intercalate; i risultati sono sensibili al rumore della macchina.

### Variante 1: ramo per istruzione, 15 ripetizioni

| Caso | G mediana | H mediana | H vs G |
|---|---:|---:|---:|
| `i01_collatz` | 0,499625 s | 0,525034 s | +5,09% |
| `i03_primes` | 0,332371 s | 0,361935 s | +8,89% |
| `gcd` | 0,053170 s | 0,056829 s | +6,88% |

La variante rallenta tutti e tre i casi misurati; viene scartata.

### Variante 2: aggregazione dopo il ciclo, 7 ripetizioni

| Caso | G mediana | H mediana | H vs G |
|---|---:|---:|---:|
| `i01_collatz` | 0,486184 s | 0,441507 s | −9,19% |
| `i03_primes` | 0,328275 s | 0,325348 s | −0,89% |
| `gcd` | 0,048867 s | 0,052656 s | +7,75% |

Il risultato è misto: un caso migliora, uno è quasi invariato e uno peggiora. La dispersione è abbastanza ampia da non giustificare l'accettazione, soprattutto per il peggioramento di `gcd`. Un tentativo da 15 ripetizioni è scaduto per timeout prima di produrre un dataset completo; non lo considero prova.

## Decisione

**Nessuna modifica al codice di produzione.** Ho ripristinato `src/ir_lower.c` alla versione G, che include la cache locale di `IrSafeChecksOn()` già validata nella tranche I04. Non accetto un cambiamento basato su una singola misura favorevole quando gli altri carichi mostrano regressione o nessun beneficio. Il prossimo intervento deve partire da un hotspot misurato e dimostrare un miglioramento ripetibile senza regressioni, oltre a preservare i contatori `BABYC_COV`.

## Limiti

Non sono state eseguite in questa tranche la suite completa, MSVC o i sanitizer. Le prove sono benchmark mirati e non sostituiscono la convalida completa prima di un rilascio.
