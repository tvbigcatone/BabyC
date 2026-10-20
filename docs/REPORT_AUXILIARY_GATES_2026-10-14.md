# BabyC W30 — chiusura gate ausiliari (2026-10-14)

## Sintesi

Sono stati completati i gate Linux/GCC riproducibili in questo ambiente. La build include anche l'estensione di `VmTerminalCallSafe`: il preflight della VM compatta ora ammette operazioni scalari pure (`IrLinBin`, `IrLinUnary`, `IrLinAddI`) tra chiamate terminali supportate. La verifica preventiva dell'intero stream resta obbligatoria prima dell'esecuzione, evitando fallback dopo effetti collaterali già emessi.

## Risultati

| Gate | Esito | Evidenza |
|---|---|---|
| Build GCC forzata completa | PASS, nessun warning osservato | `logs/GCC_FULL_REBUILD.log` |
| Suite integrata `tools/run_suite.py src` | **927 pass, 0 fail, 28 skip** | `logs/FULL_SUITE_FINAL.log` |
| Bytecode VM compatta | **53/53 gruppi PASS** | `logs/COMPACT_VM_53_GROUPS.log` |
| Modalità FP | **28/28 PASS** | suite integrata |
| Differenziale IR vs token su 923 test con `.expect` | 906 identici, 15 differenze solo caret/colonna normalizzate, **2 differenze reali** | `logs/TOKEN_IR_DIFF_FULL.log` |
| String-scan fast-path con ambiente controllato | PASS | suite integrata |
| Sparse-local-snapshot, output linear/compact/strict | PASS | suite integrata |
| ASan + UBSan, matrice mirata | **44/44 esecuzioni PASS** | `logs/ASAN_UBSAN_REGRESSION_MATRIX.log` |
| Differential fuzz GCC/IR/NO_IR, loop seeds 0–9 | **10/10 concordi**, 0 crash/timeout/mismatch | `logs/DIFFERENTIAL_FUZZ_SEEDS_0_9.log` |
| Test 977 token con aspettativa `.expect.noir` | PASS nella suite | suite integrata |
| Test 278 (caso lento) | PASS nella suite; timeout dedicato 35 s | suite integrata / `tools/run_suite.py` |
| Manifest SHA-256 | rigenerato e verificato | `MANIFEST_SHA256.txt`, `logs/MANIFEST_VERIFY.log` |
| MSVC/Windows | **NON ESEGUITO** | richiede toolchain Windows non disponibile in questo ambiente Linux |

Gli skip sono file senza aspettativa `.expect`, non fallimenti. La matrice ASan/UBSan copre 11 programmi di regressione in Tree, VM lineare, VM compatta e token; non è una scansione ASan dell'intero corpus. Il fuzzing copre 10 semi, quindi è un controllo differenziale mirato, non una campagna prolungata.

## Correzioni emerse durante i gate

- `tools/test_bytecode_vm.py` separa ora il gruppo `extras` dai test corpus, evitando di rieseguire tutto il corpus una seconda volta.
- La verifica compatta per i test 503/507 non usa più un falso assunto di fallback: la build aggiornata li esegue direttamente (`attempts=1, runs=1`).
- Aggiunti `930_vm_bitfield_pack_direct.c` e `931_vm_bitfield_comma_direct.c`: test focalizzati che dimostrano l'esecuzione diretta dei bitfield senza confondere il controllo con `sizeof`.
- `VmTerminalCallSafe` ammette calcoli scalari puri tra chiamate terminali supportate. Questo consente al caso `843_vm_narrow_incdec_edges.c` di passare direttamente nella VM compatta (`attempts=1, runs=1`) mantenendo l'output atteso.

## Differenze reali nel confronto IR/token

La scansione completa di 923 test ha trovato due differenze reali, non normalizzabili come sole colonne del caret:

- `553_ir_struct_array_brace.c`: IR accetta l'inizializzazione aggregata con brace elision dell'array di struct e produce `21` / `10 20 30 40`; il percorso token segnala errore d'inizializzazione. È un gap di compatibilità del percorso token.
- `580_stack_host_and_virtual.c`: IR rileva il superamento del budget dello stack durante la ricorsione, mentre token arriva a stampare `should_not_reach`. Va indagata la differenza nel controllo del budget di stack/call frame.

Non ho mascherato queste differenze né cambiato le aspettative. La suite integrata è verde, ma il gate di equivalenza completa IR/token **non è verde** fino alla risoluzione di questi due casi.

## Limiti residui

Non si dichiara verificata la build MSVC/Windows, né una campagna di fuzzing lunga o ASan/UBSan sull'intero corpus. Questi richiedono un ambiente Windows e/o una sessione di test dedicata più lunga. Non sono stati aggiunti nuovi alberi di test: i due test nuovi risiedono in `src/tests/`.
