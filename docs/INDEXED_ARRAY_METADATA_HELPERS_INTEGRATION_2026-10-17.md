# BabyC — integrazione dei punti comuni dei metadati indicizzati (2026-10-17)

## Obiettivo del turno

Collegare i sink compile-time e frame per `PointeeSize` e ricondurre gli offset dei membri ai decoder condivisi, evitando che helper presenti ma non alimentati o bypassati lascino percorsi paralleli. La baseline di confronto resta `babyc(20261016-120858).zip`.

## Modifiche

### 1. `PointeeSize`: sorgente compile-time e frame

- Aggiunto `CompLocalPointeeSize[]` accanto agli altri metadati compile-local.
- Aggiunto `IrCompLocalPointeeSizeOrFrame()`, con precedenza compile-local e fallback sul frame attivo, coerente con gli altri accessori dual-read usati durante il lowering.
- Inseriti il dual-write nel percorso `IrStorageDescStampCompLocal()` e il frame stamp nel percorso condiviso `IrStorageDescApplyFrame()`.
- Aggiunto il valore ai salvataggi/ripristini `CompileFramePush/Pop()` e al reset per una nuova compilazione. Questo evita che una compilazione annidata lasci o consumi metadati appartenenti alla funzione chiamante.
- `IrStorageDescFromType()` ora ricava `PointeeSize` per un tipo puntatore conosciuto, quando il tipo puntato è disponibile.
- `LocalPointeeSize` nel frame è passato da 8 a 16 bit: valori validi fino a 65535 byte non vengono più troncati a 8 bit o sostituiti con 4. `IrFramePointeeSizeValue()` converte esplicitamente i valori oltre tale limite in “non disponibile” (zero), affinché il chiamante possa risolverli dal tipo anziché usare un valore corrotto.

### 2. Offset dei membri: resolver comune nel layer storage

- Spostato `IrResolveMemberLayout()` da `ir_lin_ptr.c` a `ir_storage.c`, perché viene usato da Tree e Linear e non è una responsabilità esclusiva del lowering Linear.
- Aggiunti gli accessori inline `IrResolveMemberOffsetValue()` e `IrResolveMemberValueLayout()` in `ir_storage.h`. Il primo serve quando il chiamante ha già il `Value` del membro e necessita solo dell'offset; il secondo decodifica anche size, base e classificazione pointer.
- Il resolver completo riusa il decoder del `Value` invece di duplicarne la logica.
- I percorsi Linear per `array[i].member`, membri array-decay e indirizzi di membri indicizzati ora passano dal decoder comune. L'inizializzazione di array di struct ordina e scrive i membri usando gli offset risolti, non rileggendo separatamente `Val->Integer`.
- I percorsi di indirizzamento Tree in `ir_mem.c`, il layout inference e il fast path di `ir_fast.c` usano l'accessore offset condiviso quando hanno già risolto il `Value` del membro. Restano letture grezze solo dentro il decoder canonico stesso e nei campi `Value->Val->Integer` che rappresentano dati/valori, non offset di membri.
- Per evitare lookup `TableGet` doppi nei percorsi che già possiedono il `Value` del membro, l'accesso offset-only è inline e minimale; il resolver completo viene usato quando serve il layout completo.

### 3. Regressione mirata

Aggiunto `src/tests/961_pointee_stride_large_struct.c` con expected vuoto: due elementi di struct con un membro-array da 300 byte vengono indicizzati tramite `struct LargeStrideItem *`. Il test verifica il risultato attraverso il codice d'uscita, quindi rileva anche un errore silenzioso senza output.

- Tree: pass.
- Linear non-strict: pass.
- Linear strict: attualmente segnala `strict: tree fallback (body)`. Questo non è un errore di output nei motori supportati: è una lacuna di copertura strict per questo caso di struct grande, resa esplicita dal test. Non viene mascherata né classificata come pass.

## Verifiche

- Build GCC dopo `make clean`: superata, senza warning emessi.
- `tools/test_fp_modes.py`: **32/32**.
- `tools/check_diagnostic_catalog.py`: superato, 10 stringhe canoniche confinate in `src/diagnostic.c`.
- `tools/test_empty_expect_exit_status.py`: superato.
- Suite corpus 960 test:
  - Tree/IR: **960/960**.
  - Linear non-strict: **960/960**.
  - Token-only: **960/960**.
  - IR shadow disattivato: **960/960**.
- Linear strict: **908/960**, 0 timeout, 52 fail. Il conteggio precedente era 908/959; il nuovo test 1049 rende visibile una lacuna strict aggiuntiva, non una regressione di test preesistenti. L'elenco dei 51 casi già fallenti resta invariato.
- Bench end-to-end, 24 campioni interleaved, `BABYC_PATH=linear`, accel/kernel/shape disattivati; confronto con la versione immediatamente precedente a questo delta cumulativo:
  - `isort.c`: circa **+2.9%**.
  - `structs.c`: circa **+0.2%**.
  - `sieve.c`: circa **+1.9%**.
  I risultati includono startup e lowering e hanno variabilità di run; non dimostrano un miglioramento. Il decoder offset-only inline è stato mantenuto per evitare una chiamata out-of-line per accesso a membro. Nessun benchmark è interpretato come prova di zero costo assoluto.
- MSVC/Windows non verificato in questo turno.

## Residui classificati

- `LocalPointeeSize` letto direttamente in `ir_lin_exec.c` nei kernel runtime frame-first resta intenzionale: il frame attivo è la fonte autorevole durante l'esecuzione; non deve essere sostituito indiscriminatamente con la precedenza compile-local.
- Il percorso bytecode sperimentale resta fuori dal lavoro, in accordo con la priorità corrente di non ampliarlo.
- `IrResolveMemberValueLayout()` è il decoder canonico del layout da un `Value`; l'accesso diretto a `MV->Val->Integer` al suo interno è la singola sorgente grezza dell'offset, non un bypass.

## Decisione

I due gap architetturali pianificati sono stati collegati: `PointeeSize` ha ora sink compile-time/frame e accessore dual-read; gli offset dei membri nei percorsi Tree/Linear interessati passano dal decoder condiviso. Le suite non-strict sono verdi; la copertura strict non è completa e il nuovo caso di struct grande è lasciato visibile come lacuna da affrontare separatamente.
