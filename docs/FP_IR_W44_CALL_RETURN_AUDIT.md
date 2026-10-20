# W44 — audit chiamate e ritorni FP

## Baseline
Ricostruzione locale W40 + delta W41 + W42 + W43. Il codice sorgente non è stato modificato in W44: l'obiettivo del turno è stato verificare i percorsi di chiamata e raccogliere evidenze prima di applicare una correzione semantica.

## Verifiche
- Build pulita GCC: PASS; zero righe `warning:` o `error:`.
- `tools/test_fp_modes.py --babyc src/babyc`: 28/28 PASS.
- Test mirati `881_fp_user_func_return`, `885_fp_int_return_conversion`, `886_fp_ptr_deref_return`: output coerente in Tree, VM lineare, VM compatta e token (12 esecuzioni).
- `tools/audit_fp_ir_arch.py --babyc src/babyc`: FAIL, 0/4 modalità conformi al riferimento GCC.
- Suite completa `tools/run_suite.py src`: non completata entro il limite del turno; il log disponibile può essere parziale e non viene usato per dichiarare un totale.

## Evidenza architetturale
Il probe GCC richiede `size 4 8 12 24`, arrotondamento `round 1 0 1 0` e layout aggregato `24`. BabyC riporta `size 8 8 8 8` nei percorsi Tree/VM/compact e `size 8 8 24 24` nel percorso token; l'arrotondamento binary32 fallisce in tutti i percorsi e il layout risulta 32.

## Decisione tecnica
Non è stato applicato un fix speculativo al packing del ritorno. I test correnti di ritorno FP passano perché `float` e `double` sono ancora associati allo stesso tipo semantico; cambiare solo il packing non risolve dimensione degli oggetti, conversioni e layout. Il prossimo fix deve attivare `FloatType` solo insieme a conversioni coerenti nei parametri, nei ritorni, nei lvalue e nell'accesso alla memoria, quindi aggiungere test differenziali in `src/tests/`.

Nessun `.expect` è stato modificato.
