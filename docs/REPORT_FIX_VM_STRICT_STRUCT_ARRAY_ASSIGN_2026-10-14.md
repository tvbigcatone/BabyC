# BabyC — correzione urgente assegnazioni di struct negli array (2026-10-14)

## Esito

Corretto il difetto riproducibile in `src/tests/546_ir_struct_array_assign.c`: `BABYC_EXEC=vm_strict` restituiva i bit dell'indirizzo del buffer di ritorno della funzione al posto dei campi della `struct`. Ora Tree, VM lineare e VM strict restituiscono tutti `1 2 3 4 1 12`.

## Causa radice

Il percorso strict per `a[i] = ptr` classificava un array come array di puntatori quando `CompLocalLeafEs[slot] == sizeof(void *)`. La dimensione non determina il tipo: sul target x86-64 `struct S { int a; int b; }` è lunga 8 byte, quanto un puntatore. Quindi `v[i] = mk(...)` entrava erroneamente nel percorso per puntatori e scriveva nella memoria dell'elemento la rappresentazione dell'indirizzo del buffer di ritorno; i due `int` della struct diventavano i due word dell'indirizzo.

Inoltre, il percorso di copia per valore non ammetteva una chiamata come RHS: gestiva solo identificatori e accessi indicizzati. Perciò non copiava i byte della struct restituita dalla funzione.

## Correzione

1. La specializzazione `array[i] = pointer` ora richiede il metadato esplicito `CompLocalElemIsPtr[slot]`; non deduce più il tipo dalla sola larghezza dell'elemento.
2. Il lowering della copia per valore ammette una chiamata come sorgente: il valore restituito dal percorso lineare è l'indirizzo del blob di ritorno e `IrLinStoreIndex` con `Tok=3` copia i byte della struct nell'elemento destinazione.
3. La copia da chiamata viene rifiutata dal lowering per aggregati oltre 512 byte, limite dell'attuale buffer stabile di ritorno. È preferibile rifiutare il percorso piuttosto che copiare da una memoria di durata non garantita.
4. Aggiunta la regressione `src/tests/889_vm_struct_array_return_copy.c` per una struct da 16 byte, oltre al test preesistente 549 che copre il caso critico in cui la struct è grande quanto un puntatore. La regressione multidimensionale 955 della revisione precedente è inclusa nel delta e integrata nell'harness.
5. Aggiornato `tools/test_bytecode_vm.py` per verificare 549, 955 e 956 anche in `vm_strict` senza fallback sull'albero.

## Verifiche effettuate

- Build pulita GCC: `make -C src clean && make -C src -j4` — OK, zero warning.
- `python3 tools/test_bytecode_vm.py` — **34/34 gruppi superati**.
- `python3 tools/test_fp_modes.py` — **28/28 superati**.
- `python3 tools/test_sparse_local_snapshot.py` — PASS.
- `python3 tools/test_string_scan_fastpath.py` — PASS.
- Test 549: Tree, VM, VM strict producono `1 2 3 4 1 12`.
- Test 956: Tree, VM, VM strict producono `10 13 20 23 10 113`.
- Test 955: Tree, VM, VM strict producono `255 65535 4294967295 18446744073709551615 -32768`.
- Fixture `832_vm_pointer_array.c`: `vm_strict` continua a produrre `10 40 30`.

## Limiti dichiarati

La modalità `BABYC_EXEC=vm` con `BABYC_BYTECODE_VM=1` continua a rifiutare/fare fallback per i casi di struct-return 549/956 (`attempts=1`, `runs=0`); non dichiaro questi casi eseguiti dal bytecode compatto. La correttezza richiesta è verificata sul percorso lineare `vm_strict`, che non segnala fallback sull'albero. MSVC e sanitizer non sono stati verificati in questa sessione.
