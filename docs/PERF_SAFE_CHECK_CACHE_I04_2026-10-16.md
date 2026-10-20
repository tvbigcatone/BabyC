# BabyC performance tranche I04 — cache locale del profilo safe-check

Data: 2026-10-16

## Intervento

In `src/ir_lower.c`, `IrExecLinLocals()` chiamava `IrSafeChecksOn()` nei tre punti caldi del ciclo di esecuzione, oltre al controllo preliminare che ripulisce `LocalInit`. La funzione sottostante mantiene il risultato in una variabile statica dopo la prima lettura delle variabili d'ambiente; il valore non cambia nel corso del processo. La funzione esecutrice ora legge tale valore una sola volta all'ingresso e riutilizza `SafeChecks` nei punti già esistenti.

Non sono stati introdotti rami di esecuzione, modalità, fast path o controlli di sicurezza nuovi. Le condizioni preesistenti restano nello stesso ordine; cambia soltanto la frequenza delle chiamate alla funzione di configurazione. `BABYC_SAFE_FORCE_OFF` e `BABYC_SAFE_FORCE_ON` continuano a passare attraverso `IrSafeChecksOn()`.

## Build e correttezza

- Build GCC `-O2` di controllo e candidato: riuscite; zero warning nel log di ciascuna build.
- 7 test mirati × 4 modalità (Tree, Linear, Bytecode, Strict) × 2 varianti: 56/56 confronti riusciti. Casi: `141_bp_win64`, `216_ir_local_recurse`, `221_ir_nested_calls_locals`, `253_array_struct_member_uninit`, `545_ir_struct_array_uninit`, `870_vm_recursive_helper_effects`, `879_vm_mutual_pointer_recursion`.
- Output e codici di uscita verificati, incluse le diagnostiche di variabile non inizializzata.
- `i01_collatz`, `i03_primes` e `gcd`: 15 misure per variante ciascuno; output verificato a ogni esecuzione.
- Suite completa, MSVC e sanitizer: non eseguiti in questa tranche.

## Misure end-to-end

Build GCC `-O2`, `BABYC_EXEC=vm`, `BABYC_NO_KERNEL=1`, `BABYC_NO_SHAPE_FP=1`; 15 esecuzioni per variante, ordine intercalato F/G, un warm-up per variante e caso. Tempo di processo end-to-end; mediana e MAD (deviazione assoluta mediana) calcolate sui campioni. Le percentuali negative indicano minore durata nel candidato.

| Caso | Prima: mediana | Dopo: mediana | Variazione | MAD prima | MAD dopo |
|---|---:|---:|---:|---:|---:|
| `i01_collatz` | 0,528347 s | 0,460713 s | −12,80% | 0,009992 s | 0,007284 s |
| `i03_primes` | 0,369532 s | 0,308815 s | −16,43% | 0,016717 s | 0,012659 s |
| `gcd` | 0,052458 s | 0,045176 s | −13,88% | 0,002893 s | 0,001484 s |

Risultato promettente e coerente sui due carichi, ma per ora limitato ai due benchmark misurati: non è una garanzia di accelerazione generale. Un tentativo di ampliare la misura con altri quattro casi è terminato per timeout dell'ambiente prima di produrre un insieme completo di dati; tali casi non sono usati per sostenere il risultato.

## Riproducibilità

- Controllo: snapshot sorgente precedente, `F_safecheck`.
- Candidato: identico al controllo eccetto la cache locale descritta sopra, `G_safecheck_local`.
- Script di misura: `perfmeasure/measure_FG.py` nel workspace di sviluppo (non incluso nel delta di produzione).
- Dati grezzi delle 15 ripetizioni conservati nel rapporto di lavoro del turno, non inclusi nel pacchetto runtime.

## Decisione

Mantenere la modifica: elimina chiamate ripetute nel ciclo caldo, mantiene invariata la semantica configurata dei controlli e mostra un miglioramento misurabile sui tre carichi misurati. Verificare in una tranche successiva su ulteriori carichi e piattaforma MSVC.
