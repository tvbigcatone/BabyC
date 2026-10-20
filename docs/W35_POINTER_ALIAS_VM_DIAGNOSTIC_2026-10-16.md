# W35 — pointer-alias VM diagnostic follow-up (2026-10-16)

## Correzione alla validazione W34

Le dichiarazioni di chiusura e di suite completa nel report W34 non sono riproducibili nella workspace ricostruita dalla baseline fissa `babyc(20261016-120858).zip` più il delta W34. Questa nota le supersede: il problema non va considerato chiuso.

## Modifica effettiva

In `src/ir_lower.c`, il salto del body VM per funzioni molto piccole (`S->N <= 8`) viene ora applicato solo quando i controlli di inizializzazione non sono attivi. Con i controlli attivi, quel percorso poteva marcare il body come saltato e lasciare che la chiamata terminasse senza eseguire la diagnostica. La modifica mantiene il percorso di esecuzione esistente e rimuove il bypass solo nel profilo in cui la correttezza della diagnostica è richiesta.

Inoltre, la diagnostica dell'accesso indicizzato nel controllo alias del Linear VM è stata allineata alla posizione Tree (caret dopo la parentesi quadra di chiusura).

## Risultati riprodotti

Build GCC `-O2`: riuscita.

Con `BABYC_SAFE=1`:

| Test | Tree (`BABYC_NO_IR=1`) | Linear VM | Bytecode VM | `vm_strict` |
|---|---:|---:|---:|---:|
| `951_direct_pointer_param_store_marks_init.c` | exit 0 | exit 0 | exit 0 | exit 0 |
| `952_tree_pointer_param_read_uninit.c` | exit 1, col. 36 | exit 1, col. 36 | exit 1, col. 36 | exit 1, col. 21 |
| `953_tree_pointer_alias_partial_init.c` | exit 1, col. 48 | exit 1, col. 48 | exit 1, col. 48 | exit 1, col. 21 |

I due repro non vengono più silenziati nei profili VM ordinari e bytecode. Le scritture attraverso il parametro continuano a marcare l'elemento inizializzato (1037).

## Limiti aperti

- `vm_strict` segnala ancora il nome del parametro nella dichiarazione invece dell'accesso `p[i]`: semantica dell'errore corretta, posizione del caret non ancora coerente.
- Il profilo IR predefinito continua a non emettere diagnostica per 1038/1039; i test `.expect` vuoti riflettono il comportamento attuale e non sono prova di correttezza. Il percorso Tree è stato verificato con `BABYC_NO_IR=1`.
- La suite completa è stata avviata, ma il comando non è terminato entro 120 secondi; nessun conteggio completo viene dichiarato. I test 1035–1039 sono stati verificati singolarmente nelle modalità IR/Tree e i tre test 1037–1039 nelle modalità VM indicate.
- Non è stato misurato l'impatto prestazionale: la guardia cambia l'ammissibilità del salto tiny-body solo quando i controlli uninitialized sono attivi. Serve un benchmark comparativo prima di concludere che l'impatto sia nullo.

## Stato

**Progresso parziale, non chiuso.** Il bypass che permetteva ai due repro di terminare con exit 0 nelle modalità VM ordinarie è corretto. Restano da risolvere la posizione del caret in `vm_strict`, la diagnostica nel profilo IR predefinito e la validazione completa della suite/performance.
