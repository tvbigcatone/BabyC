# BabyC — conclusione intervento su struct array e array multidimensionali

Data: 2026-10-14  
Baseline: `babyc(20261014-080706).zip`

## Esito

L'intervento sui difetti di correttezza delle assegnazioni di `struct` negli array e delle letture/scritture di array locali multidimensionali è concluso per i percorsi Tree, VM lineare e VM strict coperti dai test. La build GCC è pulita; la suite generale e i gate mirati passano. I test mirati ASan/UBSan non segnalano più l'accesso disallineato individuato durante la verifica.

## Difetti e correzioni

### 1. Assegnazioni di `struct` negli array in `vm_strict`

Il caso `src/tests/546_ir_struct_array_assign.c` falliva sulla baseline: Tree e VM lineare producevano `1 2 3 4 1 12`, mentre `vm_strict` scriveva parole dell'indirizzo del buffer di ritorno al posto dei campi della struttura.

La causa era la classificazione di un array come array di puntatori basata sulla sola uguaglianza `element_size == sizeof(void *)`. Una struttura di 8 byte può avere la stessa dimensione di un puntatore, ma non è un puntatore. La specializzazione ora richiede il metadato esplicito `CompLocalElemIsPtr`. Inoltre il lowering della copia per valore accetta una chiamata come RHS e copia i byte del valore-struct restituito nell'elemento destinazione.

Il limite attuale del buffer di ritorno resta 512 byte: il lowering non tenta copie da un buffer di durata non garantita oltre tale limite.

### 2. Tipo effettivo dell'elemento negli array multidimensionali locali

La verifica UBSan ha trovato un secondo problema concreto nel percorso comune Tree/VM lineare: per alcuni accessi al livello foglia, `IrResolvePtr` impostava `ElemBase = TypeInt` anche quando l'array dichiarato era `unsigned char` o `unsigned short`. Per esempio, la scrittura di `uc[1][2]` arrivava a una scrittura C di un `int` in un indirizzo non allineato, invece di una scrittura di un byte. L'output appariva corretto su x86, ma il comportamento era undefined e poteva sovrascrivere memoria adiacente.

Ora il tipo foglia viene ricavato dal tipo dichiarato locale (`LocalFromType` / `LocalTyp`), attraversando eventuali livelli `TypeArray`, invece di inferirlo dalla sola dimensione dell'elemento. Il fix è in `src/ir_mem.c`; la regressione riproducibile è `src/tests/887_vm_local_multidim_unsigned.c`.

### 3. Correzioni multidimensionali e regressioni mantenute

Il delta conserva anche il fix del lowering multidimensionale, il corretto trattamento signed/unsigned degli elementi e le regressioni per array 3D/4D e copia di struct restituite da funzione. I test sono collocati in `src/tests/`, senza creare directory di test parallele.

## Verifiche eseguite su questa revisione

- `make -C src clean && make -C src -j4`: PASS, zero warning/errori GCC.
- `BABYC="$PWD/src/babyc" python3 tools/run_suite.py`: **909 PASS, 0 FAIL, 23 SKIP**.
- `python3 tools/test_bytecode_vm.py`: **34/34 gruppi PASS**.
- `python3 tools/test_fp_modes.py`: **28/28 PASS**.
- `python3 tools/test_sparse_local_snapshot.py`: PASS.
- `python3 tools/test_string_scan_fastpath.py`: PASS.
- Build dedicata ASan/UBSan tramite `tools/asan_build.sh`: PASS.
- Fixture 549, 956 e 955 eseguite con ASan/UBSan in Tree, VM lineare e VM strict: output atteso, nessun `AddressSanitizer ERROR` o `runtime error` UBSan.
- Fixture 549 sulla baseline originale: `vm_strict` riproduceva la corruzione (`-1148441120 22062 ...`), mentre Tree e VM lineare restituivano `1 2 3 4 1 12`.
- Fixture multidimensionali 831–834: output Tree/VM/VM strict concorde, secondo il gate mirato.

## Limiti dichiarati

- I casi 549, 955 e 956 non sono ancora eseguiti dal bytecode compatto: il test harness conferma `attempts=1, runs=0` e accetta il fallback previsto. Non si dichiara quindi copertura bytecode compatto per queste forme.
- La compilazione MSVC di questa specifica revisione non è stata eseguita in questa sessione. La conferma MSVC 32-bit disponibile riguarda una baseline precedente.
- Non sono stati eseguiti benchmark prestazionali: questo intervento è di correttezza, non una rivendicazione di speedup.

## File inclusi nel delta

- `src/ir_lower.c`
- `src/ir_mem.c`
- `src/tests/887_vm_local_multidim_unsigned.c` e `.expect`
- `src/tests/889_vm_struct_array_return_copy.c` e `.expect`
- `tools/test_bytecode_vm.py`
- questo rapporto, `DELTA.md`, `VALIDATION.md` e patch unificata
