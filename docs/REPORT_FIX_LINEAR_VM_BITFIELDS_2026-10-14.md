# BabyC — correzione VM lineare: bitfield e aritmetica dei puntatori

Data: 2026-10-14

## Problemi corretti

La baseline precedente falliva tre test del corpus lineare:

- `520_bitfield_struct_array_loop.c`: `108 / 32`, atteso `140 / 64`.
- `529_bitfield_ptr_walk.c`: `26`, atteso `58` (la seconda riga era già corretta).
- `536_storm_bf_arr_ptr.c`: `49`, atteso `65`.

L'indagine ha individuato due cause correlate:

1. Nei percorsi di lowering manuale per `array[i].campo`, `BitSigned` veniva forzato a `1`, anche per campi dichiarati `unsigned`. Gli aggiornamenti composti di campi unsigned venivano quindi estratti con estensione del segno. Ora la signedness è ricavata dal tipo dichiarato del membro.
2. Nei cicli che percorrono un array di struct tramite puntatore (`p++`), il tipo cache del pointee può avere `Sizeof` assente/stale; il lowering usava allora `ElemSize` pari alla dimensione del puntatore (8 byte) invece della stride della struct (4 byte nei casi riprodotti), saltando gli elementi dispari. Il percorso usa ora la dimensione del tipo aggregato quando disponibile e, solo se manca, la inferisce dalla tabella dei membri o dalla stride-leaf già nota per lo stesso tipo. La correzione è limitata al caso di aritmetica su puntatori e non cambia l'incremento dei normali scalari.

## Verifica

- Build GCC Linux: **PASS** (`make -C src -j2`).
- Corpus con aspettative, VM lineare (`BABYC_EXEC=vm`): **918/918 PASS**.
- Corpus con aspettative, Tree: **918/918 PASS**.
- Sottoinsieme bitfield/bitfield-pointer: **49/49 PASS**.
- I tre test di regressione: output ora conforme agli `.expect`.
- Applicazione del `DELTA.patch` contro il file `src/ir_lower.c` del delta precedente: **dry-run PASS**.

I log del corpus sono stati prodotti da un runner parallelo che esegue tutti i 918 test con aspettative e confronta stdout/stderr normalizzati secondo i filtri del runner ufficiale. Il runner integrato `tools/run_suite.py` non è arrivato alla fine entro 300 secondi (rimaneva nella fase aggiuntiva dei test strutturali); per questo non dichiaro quel comando integrato come completato. Il risultato 918/918 deriva dal confronto indipendente completo del corpus, non da un conteggio stimato.

## File

- `src/ir_lower.c`: sorgente completa aggiornata, basata sulla baseline del delta globale multidimensionale precedente.
- `DELTA.patch`: patch incrementale rispetto a `BABYC_DELTA_GLOBAL_MULTIDIM_STRUCT_COPY_COMPACT_VM_2026-10-14.zip`.
- `logs/`: build, esiti corpus Tree/VM, subset bitfield e prova di applicabilità della patch.

## Limiti

La verifica MSVC non è stata eseguita in questo ambiente Linux. Non sono state introdotte nuove cartelle di test: i test di regressione esistenti restano in `src/tests/`.
