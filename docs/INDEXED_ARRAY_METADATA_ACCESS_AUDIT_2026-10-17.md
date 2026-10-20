# BabyC — audit ampio dei punti comuni di accesso ai metadati indicizzati (2026-10-17)

## Obiettivo

Verificare che i metadati usati per stride degli elementi, dimensioni multidimensionali, classificazione array-di-puntatori e layout dei membri passino dagli helper condivisi quando questi rappresentano la sorgente comune. La sola presenza di un helper non basta: occorre verificare chiamanti reali e cercare percorsi paralleli che ricostruiscono gli stessi dati.

## Risultati principali

### 1. `IrResolveDeclElemSize()` esisteva, ma un percorso lo aggirava

`IrResolveDeclElemSize()` era usato nel lowering e nell'esecuzione `IR_K_DECL_ARRAY`, ma `ir_lin_compile.c` manteneva una cascata locale `DeclElemSize -> DeclFromType->FromType->Sizeof -> sizeof(int)`. La cascata poteva bypassare il formato di storage FP e duplicava la decisione sullo stride.

**Correzione:** i due rami di dichiarazione array in `ir_lin_compile.c` usano ora `IrResolveDeclElemSize()` direttamente. Anche il fallback per array statici in `ir_lin_exec.c` e la registrazione dello stride degli aggregati locali in `ir_stmt.c` passano dall'helper, mantenendo il caso esplicito di array di puntatori (stride `sizeof(void *)`). Il fallback dell'inizializzatore di array FP usa il metadato di storage prima di ricorrere alla rappresentazione di esecuzione legacy.

### 2. L'helper del descrittore non rispettava pienamente il tipo foglia

`IrStorageDescElemSizeFromType()` leggeva `FT->Sizeof` direttamente e non eliminava tutti i livelli array. Questo ignorava `FPFormat` se `Sizeof` era stale e poteva restituire la dimensione di un array anziché lo stride della foglia.

**Correzione:** il tipo puntato viene risolto, i livelli array vengono rimossi con `IrTypePeelArray()`, e il tipo FP usa `TypeFPStorageSize()`.

### 3. Il layout comune dei membri aveva un caso FP mancante

`IrResolveMemberLayout()` (usato dal wrapper `IrLowerMemberOffEs()`) gestiva char, short, pointer e tipi larghi, ma un membro `TypeFP` cadeva nel ramo generico da 4 byte. Quindi l'helper comune esisteva, ma non descriveva correttamente tutti i tipi che doveva servire.

**Correzione:** il ramo FP ricava lo stride da `TypeFPStorageSize()` e restituisce `TypeFP` come base.

### 4. Gli accessori dual-read coprivano solo parte dei metadati

Gli accessori P0.25 erano presenti per `FromType`, `LeafEs` e `NDims`, ma non per una singola dimensione `Dims[slot][d]` né per `ElemIsPtr`. Alcuni lowering indicizzati ripetevano quindi fallback manuali o non consultavano il frame attivo se il metadato non era nel tipo.

**Correzione:** aggiunti `IrCompLocalDimOrFrame()` e `IrCompLocalElemIsPtrOrFrame()`. Sono stati collegati ai percorsi di lowering per array multidimensionali, array di struct e accessi membro attraverso array di puntatori. Alcuni fallback manuali di `FromType`, `NDims` e `LeafEs` sono stati sostituiti dagli accessori esistenti.

## Inventario residuo: cosa non va sostituito alla cieca

La scansione statica ha trovato numerose letture dirette di `CompLocal*`, `Local*` e `ValueType->Sizeof`. Non sono tutte bypass equivalenti:

- **Compile-only:** alcuni accessi avvengono mentre si compila `FnCode`, quando il frame live non è ancora quello della funzione; in questi punti `CompileSlot`/`CompLocal` è la fonte corretta.
- **Frame-first intenzionale:** alcuni lowering runtime privilegiano il frame attivo e usano `CompLocal` solo se la dimensione non è nota. Sostituirli con l'accessore compile-first cambierebbe la precedenza e può leggere metadati stantii.
- **Metadati di valore, non solo di layout:** diversi percorsi richiedono anche `MV->Typ`, bitfield e flag unsigned; `IrResolveMemberLayout()` fornisce offset/size/base ma non sostituisce la lettura del tipo completa.
- **PointeeSize non ha ancora un mirror compile-time:** `LocalPointeeSize[]` è presente nel frame ma non esiste un corrispondente `CompLocalPointeeSize[]` né un accessor dual-read. `ir_lin_emit.c` contiene ancora più letture dirette di `Fr->LocalPointeeSize` per ricavare lo stride del puntatore-puntato; se la compilazione deve decidere prima che il frame sia disponibile, alcuni percorsi finiscono su fallback conservativi (per esempio 4). Questo è un gap architetturale reale da affrontare nel prossimo passaggio, non un caso da nascondere con un'altra euristica.
- **Member layout nel runtime:** i percorsi attivi Linear che trattano `a[i].member` spesso leggono direttamente `MV->Val->Integer` e `MV->Typ`. La lettura di `MV->Typ` resta necessaria per unsigned e bitfield, ma l'offset deve essere ricondotto al layout comune in tutti i punti in cui non serve una rappresentazione speciale. L'audit ha corretto la gestione FP del resolver, ma non dichiara che ogni offset runtime sia già passato dal resolver.
- **Bytecode sperimentale:** `ir_vm.c` mantiene lookup specializzati per i member load/store. Non sono stati ampliati in questo turno perché il percorso bytecode resta sperimentale e temporaneamente fuori dalla priorità di sviluppo; va mantenuto esplicitamente nell'inventario dei bypass da rivalutare quando si riaprirà quel percorso.

Quindi il criterio corretto non è “nessuna lettura diretta”, ma “ogni lettura diretta ha una ragione semantica verificabile; ogni fallback fra le stesse sorgenti usa l'accessore con la precedenza giusta”. La scansione ha trovato circa 142 riferimenti grezzi a `CompLocal*` nei file di lowering/emissione; questo conteggio include accessi di scrittura e percorsi compile-only, quindi non equivale a 142 bypass. Il prossimo audit deve classificare i residui uno per uno e introdurre un mirror/accessor per `PointeeSize` prima di poter dichiarare completa la centralizzazione.

## Verifica statica e test

- Build GCC pulita dopo le modifiche.
- `tools/test_fp_modes.py`: **32/32 pass**.
- Suite corpus (959 test, senza timeout):
  - Tree/IR: **959/959**.
  - Linear non-strict: **959/959**.
  - Token-only: **959/959**.
  - IR shadow disattivato: **959/959**.
- Sottoinsieme mirato a array, puntatori, array di struct/bitfield, funzioni e alias: **14/14** in Linear non-strict.
- Linear strict: **908/959**, con **51 fail** ancora presenti. Non viene dichiarato PASS strict: la lista completa è quella stampata da `run_suite_tranches.py` (include casi di fallback strict e gap di copertura/semantica). I risultati non-strict non devono essere usati per dedurre copertura strict.
- Nessun benchmark prestazionale è stato eseguito in questo turno; le modifiche sono principalmente nei percorsi di risoluzione/stamping dei metadati e nei fallback di dichiarazione, ma l'impatto non è certificato da una misura.
- MSVC/Windows non verificato.

## File toccati

- `src/ir_runtime.h`: risoluzione stride FP/array per dichiarazioni.
- `src/ir_storage.c`: stride da tipo foglia tramite metadati FP.
- `src/ir_storage.h`: accessori dual-read per dimensione e flag array-di-puntatori.
- `src/ir_lin_ptr.c`: layout membro FP corretto.
- `src/ir_lin_compile.c`, `src/ir_lin_exec.c`, `src/ir_stmt.c`: rimozione di fallback paralleli per stride degli array.
- `src/ir_lin_emit.c`: collegamento degli accessori nei percorsi multidimensionali e array-di-puntatori.

## Decisione

La verifica ha trovato e corretto bypass reali, non soltanto aggiunto commenti. La centralizzazione è migliorata nei percorsi attivi Tree/Linear, ma non è dichiarata totale: restano accessi diretti intenzionali da classificare individualmente e il percorso bytecode sperimentale rimane fuori dall'intervento. Il prossimo passaggio deve usare i fail strict residui per individuare quali riguardano ancora metadati indicizzati, senza estendere il bytecode né trasformare i fallback in successi apparenti.
