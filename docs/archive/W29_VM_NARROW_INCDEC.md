# W29 — ampliamento VM: incrementi/decrementi scalari stretti

**Data:** 2026-10-11  
**Baseline:** archivio `babyc(3).zip` ricevuto per questo turno.

## Intervento

Abilitato il lowering di `++`/`--` su variabili locali `char`, `unsigned char`, `short` e `unsigned short`, senza rifiutare l'intero corpo della funzione.

La soluzione conserva la semantica in più executor: l'istruzione `IrLinAddLocal` porta ora il tipo base stretto quando serve; i percorsi lineare/light e VM normalizzano il valore aggiornato a quel tipo. Il valore precedente usato da postfix resta distinto dal valore aggiornato.

## Causa profonda

La guardia preesistente in `IrLowerExpr` rifiutava intenzionalmente i tipi stretti perché il fast path non poteva garantire il wrap al tipo dichiarato. Rimuovere soltanto la guardia è scorretto: nei frame compatti i puntatori `ValueType` possono essere assenti, e dedurre il tipo dal solo slot non è affidabile. Inoltre, il campo `I32` degli identificatori locali non conservava in modo affidabile il tipo base per questo caso.

La correzione annota il tipo base **solo sugli operandi locali di `++`/`--` stretti**, codifica l'informazione nell'istruzione e la usa per normalizzare il risultato. Questo evita di aggiungere lavoro a ogni identificatore del programma e non cambia la classificazione dei puntatori.

## Test di regressione

Aggiunto `src/tests/843_vm_narrow_incdec_edges.c` con aspettativa GCC `-O0 -fwrapv`, coprendo postfix e prefix attorno ai limiti di `signed char`, `unsigned char`, `short` e `unsigned short`.

Verifiche mirate:
- `835_vm_narrow_incdec.c` con `BABYC_EXEC=vm_strict`: PASS, output uguale all'aspettativa;
- `843_vm_narrow_incdec_edges.c` con `BABYC_EXEC=vm_strict`: PASS, output uguale all'aspettativa;
- `727_narrow_incr_loop.c` con `BABYC_EXEC=vm_strict`: PASS, output `44` uguale all'aspettativa.

## Gate complessivi

| Gate | Baseline | Dopo | Esito |
|---|---:|---:|---|
| Build pulita GCC | PASS, 1 warning | PASS, 0 warning | Migliorato |
| Suite standard | 838 pass, 0 fail, 23 skip | 839 pass, 0 fail, 23 skip | PASS |
| Suite `BABYC_EXEC=vm_strict` | 617 pass, 221 fail, 23 skip | 621 pass, 218 fail, 23 skip | 3 fallimenti storici risolti; nessun nuovo fallimento nel confronto delle liste |
| Gate duale `run_suite_dual.sh` | — | timeout dopo 600 s, log vuoto | NON VERIFICATO |

La suite `vm_strict` resta intenzionalmente segnalata come non verde: i 218 residui non sono mascherati né dichiarati supportati.

## Benchmark

Confronto alternato sulla stessa macchina, 9 esecuzioni per binario e benchmark, mediana wall-clock; baseline e versione modificata compilate dalla stessa sorgente d'archivio e con lo stesso Makefile. I tempi includono l'avvio e l'interpretazione del programma. Questi sono i risultati dell'ultima misura sul codice finale.

| Benchmark | Baseline | Modificato | Delta |
|---|---:|---:|---:|
| fibc | 0,1241 s | 0,1183 s | -4,7% |
| isort | 0,1018 s | 0,0999 s | -1,8% |
| structs | 0,1333 s | 0,1248 s | -6,4% |
| sieve | 0,1746 s | 0,1652 s | -5,4% |
| collatz | 0,1136 s | 0,1154 s | +1,5% |
| tri | 0,1707 s | 0,1642 s | -3,8% |
| matmul | 0,0370 s | 0,0375 s | +1,3% |

Non emerge una regressione prestazionale sistematica: 5 benchmark migliorano, 2 peggiorano lievemente (+1,5% e +1,3%). I risultati sono misure brevi e non dimostrano un guadagno generale; confermare in un gate più lungo prima di affermare un miglioramento prestazionale.

## Altre correzioni

Aggiunta una dichiarazione anticipata di `struct ParseState` in `ir_call.c`, eliminando il warning GCC presente nella build della baseline.
