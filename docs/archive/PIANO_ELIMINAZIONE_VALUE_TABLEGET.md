# BabyC — Piano di eliminazione di `VariableGet` / `TableGet` / `Value` dal runtime Body-IR

**Progetto:** BabyC (fork Body-IR di PicoC)  
**Data:** 2026-09-30  
**Stato:** piano operativo (estende `DESIGN_IR_DECOUPLE_B1_B2.md`)  
**Vincolo non negoziabile sulla suite:** copertura Body-IR invariata sui test già IR.

---

## 1. Principio architetturale: due path permanenti

BabyC **non** evolve verso “solo IR”. Restano **due esecutori di prima classe**:

| Path | Ruolo permanente |
|------|------------------|
| **Token** (interprete PicoC-style) | Debug, parità semantica, **micro-build a footprint minimo**, didattica “passo-passo”, fallback esplicito (`BABYC_NO_IR` / `BABYC_NO_IRB`) |
| **Body-IR** | Esecuzione ottimizzata, fast-path, kernel, diagnostica avanzata su programmi supportati |

Il path token **non** è un degrado temporaneo da spegnere a fine migrazione. È un prodotto:

- **Debug:** stesso programma, confronto IR vs token; isolamento bug di modello dati.
- **Micro footprint:** build senza `ir_exec.o` / `ir_compile.o` (o con stub) per ambienti ridotti, embedded didattico, CI leggera.
- **Futuro step micro:** subset C + solo token → binario piccolo; IR opzionale come feature.

```
                    ┌─────────────────────┐
   sorgente C  ───► │  parse / type       │
                    └─────────┬───────────┘
                              │
              ┌───────────────┴───────────────┐
              ▼                               ▼
     ┌─────────────────┐             ┌─────────────────┐
     │  Path TOKEN     │             │  Path BODY-IR   │
     │  (sempre)       │             │  (opt-in/default│
     │  Value + Table  │             │   sui body ok)  │
     │  footprint min. │             │  frame + slot   │
     └─────────────────┘             └─────────────────┘
              │                               │
              └───────────┬───────────────────┘
                          ▼
                    host / cstdlib
```

**Conseguenza sul piano “elimina Value”:**  
si elimina (o si relega al bordo) `VariableGet`/`Value` dal **hot path Body-IR**, non dal progetto. Il path token continua a usare il modello PicoC classico.

---

## 2. Obiettivo della migrazione (scope IR)

### 2.1 Obiettivo intermedio (B.1 + B.2)

Per ogni identificatore con `LocalSlot >= 0` o `ParamSlot >= 0` nel frame corrente:

> **Nessuna** funzione in `ir_exec.c` deve chiamare `VariableGet(nome)`.

Valore, indirizzo, tipo, init-map, const, size → solo `IrCallFrame` e annotazioni IR di compile-time.

### 2.2 Obiettivo lungo (IR “pulito” al centro)

- Body-IR esegue su **frame + slot + tipi annotati**.
- `Value*` solo ai **bordi**:
  - marshalling verso Intrinsic host (`printf` / `scanf` / …);
  - call IR → body ancora token;
  - resolve di simboli globali / funzioni non ancora in cache.
- Path token: invariato nel modello dati.

### 2.3 Fuori scope esplicito

- Riscrivere il path token senza `Value`.
- SSA / registri virtuali / codegen nativo (eventuale roadmap separata).
- Promesse numeriche di speedup (“+2–5×”) non misurate.

---

## 3. Situazione attuale (sintesi)

L’executor Body-IR ha già:

- `IrCallFrame` (`LocalVal[]`, `ParamVal[]`, `LocalFromType`, `LocalInitMap`, size, flags, …)
- slot su `IrExpr` / `IrStmt` (`LocalSlot`, `ParamSlot`, `DeclFromType`, …)
- helper frame-first (`IrFrameGetByName`, ordine newest-first)

Restano però:

| Problema | Effetto |
|----------|---------|
| Doppio libro (slot + shadow `LocalTable`) | Ogni `IrStDecl` fa `VariableDefine` perché altri path fanno ancora Get per nome |
| Tipo spesso da `Value*` | Get “solo per `Typ` / `FromType` / `ArraySize`” |
| Accoppiamento a `interpreter.h` | Table / StackFrame / Variable* nel cuore di `ir_exec.c` |
| Intrinsic ABI | Pack ripetuto in `Value*` per stdio |

Ordine di grandezza storico: ~70+ `VariableGet` in `ir_exec.c`, concentrati su `IrEvalExpr`, member resolve, decl shadow.

---

## 4. Famiglie di dipendenza

1. **Shadow obbligatorio (IrStDecl)**  
   Local IR → `VariableDefine` shadow per far funzionare Get su member/uninit/call.

2. **Global / non-frame**  
   Ident senza slot → Get legittimo finché non esiste global-bind IR.

3. **Metadata da `Value*`**  
   Indirizzo dallo slot, tipo ancora da Get.

4. **Bordo Intrinsic / token**  
   `Value*` richiesto dall’ABI storica; non eliminabile in un colpo senza adapter.

---

## 5. Fasi operative

### Fase B.1 — Frame come fonte di verità (locali / param)

**Azione**

- Load/store/ident con slot valido → solo frame.
- Rimuovere fallback `VariableGet(Ident)` quando `LocalSlot`/`ParamSlot >= 0`.
- Shadow `VariableDefine` **resta** (ancora scritto), ma il hot path IR **non lo legge**.

**Done when**

- `VariableGet` in `ir_exec.c` solo per globali, funzioni, o commenti di confine.
- Suite IR invariata; test member / multi-decl / uninit / OOB verdi.

**Impatto:** alto su bug di modello; basso–medio su perf.

---

### Fase B.2 — Tipo e metadata sullo slot

**Azione**

Completare e usare in modo sistematico:

| Campo | Ruolo |
|-------|--------|
| `LocalFromType` / `DeclFromType` | pointee, member, sizeof |
| `LocalElemSize`, `LocalArraySize`, `LocalNDims` | index / store / bounds |
| `LocalIsPtr`, `LocalIsFptr`, `LocalIsConst` | già parzialmente usati |
| Annotazioni `IrExMember` / `IrExIndex` | offset, bitfield, elem size a compile-time |

Regola: se c’è slot e serve un tipo in exec → **mai** `V->Typ` da Get.

**Done when**

- `IrResolveMember` / `IrAddrOf` / store index non fanno Get su root locali.
- Casi come `struct N *p, *q` restano corretti senza affidarsi allo shadow type.

---

### Fase B.3 — Intrinsic: bordo `Value*` minimo

**Azione**

- Classificare args a compile-time (string const, `&x`, int, FP) — già in parte presente.
- Un pack per argomento al call Intrinsic; evitare alloc ripetitivi nel loop.
- Lista Intrinsic “IR-safe” vs fallback esplicito al path token **solo per quella chiamata**, non per l’intero programma.

**Done when**

- Path printf/scanf nei test non fanno TableGet per gli argomenti.
- `Value*` nasce al confine Intrinsic, non per ogni sottoespressione.

**Nota:** finché l’host vuole `void*` variadici, un sottile strato `Value` o `IrHostArg` resta. Obiettivo = **un marshalling**, non metafisica zero-Value.

---

### Fase C — Global bind IR

**Azione**

- In compile: riferimenti globali usati nel body → tabella / `CachedVal`.
- In exec: niente `TableGet` per iterazione su `G[i]`.
- Generalizzare la cache già usata dai kernel.

**Done when**

- Loop su array globali senza Get per elemento.
- Get solo al primo resolve o su eventi di scope.

---

### Fase D — Shadow opzionale

**Azione**

1. Flag `BABYC_IR_NO_SHADOW=1`: non `VariableDefine` per locali di funzioni 100% IR.
2. Suite IR verde con flag on.
3. Default off → on quando stabile.
4. Call IR → token-body: marshalling **esplicito** frame → `Value*` solo al boundary (save/restore frame già presenti).

**Done when**

- Funzioni pure IR senza entry in `LocalTable`.
- Misto IR/token ancora corretto.

---

### Fase E — (Opzionale) `IrHostArg` al bordo host

Solo dopo B–D stabili:

```c
struct IrHostArg {
    enum { HA_I64, HA_F64, HA_PTR, HA_BUF } kind;
    union { long long i; double d; void *p; } u;
    int size;
};
```

- Intrinsic ricevono `IrHostArg[]`.
- Un adapter verso API C.
- Path token continua con `Value`.

---

## 6. Path token: requisiti permanenti

| Requisito | Dettaglio |
|-----------|-----------|
| **Sempre compilabile** | `#ifdef` / target `babyc-micro` senza oggetti IR, oppure stub che forzano token |
| **Sempre selezionabile a runtime** | `BABYC_NO_IR`, `BABYC_NO_IRB` (e equivalenti) restano supportati |
| **Parità diagnostica ragionevole** | Stessi errori “didattici” principali (OOB, uninit, div0, literal write) dove già implementati; gap documentati |
| **Debug** | Confronto output IR vs token su stessa suite; non rimuovere il path “per semplificare” |
| **Footprint** | Documentare size binario *full* vs *token-only* come metrica di release |

Il path token **può** restare più lento e meno completo sui fast-path: non è un bug, è il trade-off footprint/semplicità.

---

## 7. Ordine, effort, rischio

| Fase | Effort | Rischio suite | Perf IR | Bug modello | Priorità |
|------|--------|---------------|---------|-------------|----------|
| B.1 Get slot morti | M | Medio | Basso–medio | **Alto** | **1** |
| B.2 tipi su slot | M | Medio | Basso | **Alto** | **2** |
| C global bind | S–M | Basso | Medio (global loops) | Medio | **3** |
| B.3 Intrinsic pack | M | Medio (stdio) | Medio | Medio | **4** |
| D no-shadow | M–L | Alto | Basso | Alto (pulizia) | **5** |
| E IrHostArg | L | Alto | Medio | Architetturale | dopo |

Sequenza consigliata: **B.1 → B.2 → C → B.3 → D** → (eventuale) **E**.

---

## 8. Metriche di progresso (ogni delta/zip)

1. `grep -c VariableGet src/ir_exec.c` (trend decrescente).
2. Contatore opzionale runtime: `gets_local` / `gets_global` sotto `BABYC_IR_STATS`.
3. Suite: 100% dei test già Body-IR restano IR; zero regressioni expect.
4. Bench onesti (fib, matmul, string_proc, …): no regressione IR > ~5%.
5. Binario token-only (se target attivo): size e smoke test documentati.
6. Diagnostica: nessun falso positivo nuovo; OOB / uninit / literal RO invariati o migliorati.

---

## 9. Anti-pattern

| Da evitare | Motivo |
|------------|--------|
| Spegnere il path token | Perde debug, micro-build, dual-run |
| Big-bang “tutto IrValue SSA” | Mesi di rischio, poco allineato alla didattica |
| Togliere shadow prima di B.1/B.2 | Member/uninit tornano al Get e rompono la suite |
| Ottimizzare solo fast-path con Get locali ancora vivi | Guadagni fragili, bug ibridi |
| Dichiarare “Value eliminato” mentre Intrinsic e token lo usano | Confusione di obiettivo |

---

## 10. Prima mossa concreta (settimana 1 — B.1)

1. Inventario aggiornato: ogni sito `VariableGet` in `ir_exec.c` classificato in  
   `local-slot` | `param-slot` | `global` | `function` | `intrinsic-edge`.
2. Batch 1: load/store ident con `LocalSlot >= 0` → solo frame.
3. Test anti-regressione già in suite: struct member, multi-decl ptr, AOS, uninit map, OOB store, string literal write.
4. Zip delta + nota in HANDOFF con conteggio `VariableGet` before/after.

Non serve un nuovo design parallelo: **eseguire B.1** sul codice corrente, con token path intatto.

---

## 11. Relazione con documenti esistenti

| Documento | Rapporto |
|-----------|----------|
| `DESIGN_IR_DECOUPLE_B1_B2.md` | Base tecnica B.1/B.2; questo piano la estende con token permanente, fasi C–E, metriche |
| `STRATEGIA_GAP_PTR_SCAN_CALL.md` | Performance gap su bench; **ortogonale** — si affronta meglio a modello dati stabilizzato |
| HANDOFF / delta zip | Tracciamento operativo fix e conteggi |

---

## 12. TL;DR

```
Path TOKEN  ──► sempre (debug, micro footprint, parità)
Path BODY-IR ──► frame-first; Value solo al bordo

B.1  smetti di LEGGERE lo shadow per i locali con slot
B.2  tipo/metadata sullo slot
C    global bind
B.3  Intrinsic pack minimo
D    shadow opzionale
E    (poi) IrHostArg
```

**Eliminare TableGet/Value dal centro IR** non significa eliminare Value dal progetto.  
Significa: il motore Body-IR non dipende più dal dizionario PicoC per i locali; il path token resta il riferimento semplice e a basso footprint.

---

*Fine documento — BabyC, 2026-09-30*
