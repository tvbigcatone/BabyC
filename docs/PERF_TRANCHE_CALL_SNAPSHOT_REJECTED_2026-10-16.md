# BabyC — tranche prestazioni: copie degli argomenti e invalidazione cache

Data: 2026-10-16

## Obiettivo e vincoli

Analizzare i costi ripetuti nei percorsi esistenti di `LinFramePush` / `LinTabsRestore`, senza introdurre modalità alternative di esecuzione. Le modifiche candidate sono state provate in copie di lavoro separate e **non sono state mantenute** perché non hanno mostrato un guadagno temporale affidabile.

## Candidati provati

### D — copiare solo gli argomenti attivi negli snapshot

Nei tre cicli di `FnCodeInstall`, `FnCodeSnapshot` e `LinTabsRestore`, la copia delle cinque matrici di metadati argomento è stata limitata a `CallArgN[i]` elementi, con limite massimo `IR_LIN_CALL_ARGS`. Il codice compilava con GCC `-O2`; output di benchmark controllati corretto.

Con 15 esecuzioni intercalate per variante (C = sorgente cumulativa attuale; D = candidato), senza pinning CPU:

| Test | Mediana C | Mediana D | Variazione D vs C | Intervallo C | Intervallo D |
|---|---:|---:|---:|---:|---:|
| `i01_collatz` | 0,559778 s | 0,557457 s | −0,41% | 0,509475–0,621887 s | 0,501591–0,606627 s |
| `i03_primes` | 0,374898 s | 0,376689 s | +0,48% | 0,352278–0,593843 s | 0,342590–0,418936 s |

Il cambiamento del numero di elementi copiati è reale, ma l'effetto end-to-end non è distinguibile dal rumore e i risultati vanno in direzioni opposte. **Candidato scartato.**

### E — evitare la pulizia della cache Index quando non ci sono indici attivi

È stato provato un ritorno anticipato in `IdxRunClearCache()` quando `IdxN <= 0 && IdxDirty == 0`, per evitare 32 scritture su percorsi senza operazioni Index. Build GCC `-O2` pulita riuscita senza warning/errori; output dei test di misura verificato.

Con 15 esecuzioni intercalate per variante, senza pinning CPU:

| Test | Mediana C | Mediana E | Variazione E vs C | Intervallo C | Intervallo E |
|---|---:|---:|---:|---:|---:|
| `i01_collatz` | 0,562117 s | 0,564890 s | +0,49% | 0,527134–0,596059 s | 0,538954–0,615484 s |
| `i03_primes` | 0,391632 s | 0,384143 s | −1,91% | 0,361801–0,459442 s | 0,351975–0,405750 s |

Anche qui gli intervalli si sovrappongono e non emerge un miglioramento coerente. Il pinning e una ripetizione più stabile non sono stati completati in questa tranche (il tentativo di misurazione pinned ha superato il timeout dell'ambiente). **Candidato scartato**, perché aggiunge una condizione senza prova di beneficio.

## Decisione

Nessuna delle due modifiche sperimentali è stata applicata al sorgente cumulativo. Si conserva il codice già consegnato, senza introdurre nuovi rami. È preferibile non aggiungere complessità per differenze inferiori alla variabilità osservata.

## Limiti e prossima analisi

- Le misure sono wall-clock end-to-end e includono l'avvio del processo.
- I due benchmark controllano i percorsi VM di `i01_collatz` e `i03_primes`; non rappresentano l'intero progetto.
- Sono stati verificati gli output di questi benchmark, non la suite completa, MSVC o sanitizer per i candidati scartati.
- La prossima tranche dovrebbe approfondire il costo dominante di `IrExecLinLocals` / `Step` sul percorso esistente e cercare una riduzione di lavoro con un effetto misurabile, non aggiungere ulteriori micro-condizioni.
