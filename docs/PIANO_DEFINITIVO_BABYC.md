# BabyC — Piano definitivo: IR lineare, VM a registri, debug sulla VM, micro a token

**Data:** 2026-10-03 (rev.2 — allineato a tree post-D1 / post-repro-v5)  
**Stato:** piano esecutivo unico. Sostituisce `PIANO_DEBUG_IR_E_MICRO.md`, `PIANO_TOKEN_PATH_V2.md` e, per le parti in conflitto, `PIANO_REFACTOR_IR_PER_BYTECODE.md` (2026-10-01).

Dal piano di refactor riprende i debiti strutturali e le fasi di linearizzazione; cambia le scelte su tempi del bytecode, stack contro registri, ruolo del path token e Irb.

---

## 1. Decisioni prese

1. **Full, esecuzione non-debug:** bytecode proprio **a registri**, interprete proprio. Il WASM è rimandato (potrà diventare un secondo back-end dallo stesso IR).
2. **Il bytecode è la direzione**, con un prototipo come gate (Fase 1). Questo supera il vincolo «bytecode = non ora» di `HANDOFF §0/§2.4`: l'handoff si aggiorna in Fase 0.
3. **La VM è un backend del path Body-IR**, non un terzo path utente. L'executor ad albero resta **temporaneamente** come oracolo e viene ritirato a VM stabile (Fase 8): a regime i motori sono due — token (micro e fallback) e Body-IR con backend VM.
4. **Debug in Full:** direttamente sulla VM, con istruzioni `LINE` emesse solo nella compilazione debug. Si decide all'avvio (`--debug`). In debug le prestazioni non contano. Il debugger su IR ad albero si salta, salvo urgenza (fase opzionale).
5. **Micro = pochi KB, motore token, compila il C sul device, senza IR, senza analyze, senza debugger.** Il bytecode riguarda solo Full. Irb è già ritirato.
6. **Analyze (solo Full):** diagnostica anche gli errori negli `else` saltati; in execute no.
7. **Le feature da togliere dalla micro si decidono una per una**, più avanti (Fase 9).
8. **Nessuna promessa di velocità non misurata.** Ogni fase che tocca il percorso caldo riporta i tempi.
9. **Forma lineare:** si adotta la struttura a tre indirizzi (`IrLinInst`: destinazione, due operandi, immediato, operatore, riga), perché è già quasi un bytecode a registri.
10. **Split di `ir_exec.c`:** il tree post-D1 ha già estratto frame/ops/mem/call/fast/eval/stmt/shadow/token_loop. **Non si fa rollback.** La Fase 4 consolida il **runtime condiviso** (`ops`/`mem` + API stabili verso la VM); non si ri-spezza `eval`/`stmt` in vista del ritiro dell'albero.

---

## 2. Stato misurato (baseline da aggiornare in F0)

| Voce | Valore (snapshot 2026-10-03 post-D1 + repro-v5) |
|---|---|
| Suite dual A-IR / B-NO_IR | **719/0 ×2** (verificato su tree con fix signedness/narrow) |
| Suite precedente documentata | 715/0 ×3 (A-IR, B-NO_IR, C-SHADOW0) — superseduta |
| Copertura Body-IR | ~99%+ delle funzioni della suite |
| TOKEN_PATH_V2 | **CHIUSO**; `irb` rimosso |
| D1 split | **CHIUSO**: `ir_frame`, `ir_ops`, `ir_mem`, `ir_call`, `ir_fast`, `ir_eval`, `ir_stmt`, `ir_shadow`, `ir_token_loop`; `ir_exec` residuo ~1,5k LOC glue |
| B.1–B.3 / lookup locali | **CHIUSO** — zero `VariableGet` sui path slotizzati |
| Fix signedness (repro v5) | 4/4 + 17/17 repro PASS; test 720–723 |
| Ricorsione `fib(30)` (misura precedente) | IR ~0,82 s (ptr counter) vs Python ~0,16 s — da riconfermare in F0 |
| Prototipo VM (interi, no check) `fib(30)` | registri **0,020 s**; stack 0,053 s |
| Prototipo VM `array_sum` | ~0,009 s (con/senza bounds) vs IR+kernel ~0,053 s |
| Build micro | text ~199 KB, bss ~109 KB (x86-64 -O2) — lontana da «pochi KB» |
| Build Full (ordine di grandezza) | text ~384 KB; bss dominato dal pool frame |

**F0 obbliga** a riscrivere questa tabella con: suite corrente, `size` per `.o`, gprof su `fib`, bench non scorciabili, censimento residui `IrVarTableGet` / `ExpressionParse` sul tree attuale (non sui numeri del 2 ottobre).

---

## 3. Architettura target (Full)

```
parse.c / expression.c (token path)
        │ BodyStart / BodyEnd
        ▼
ir_compile.c        front-end: albero IR tipizzato, slot, tipi, flag
        ▼
ir_lower.c          middle-end: forma lineare IrLinInst (blocchi, etichette, temporanei tipizzati)
        ├──────────────► executor IR ad albero  (oracolo, ritirato in Fase 8)
        ▼
ir_emit.c + ir_vm.c  emettitore e VM a registri  (backend Full)
        ▼
runtime condiviso: ir_ops.c (operazioni, cast, narrow), ir_mem.c (puntatori, membri, load/store, init-map, heap)
        ▼
ir_bridge.h         bordo stretto verso libreria standard, Intrinsic, globali
```

- **Registri:** finestra per funzione nello stack di valori; argomenti = primi registri. `IrCallFrame` (~10 KB) sparisce nella VM.
- **Tipi a compile-time:** opcode tipizzati (int/long/float/double/puntatore).
- **Memoria:** puntatori nativi + tracciamento heap esistente (da verificare; casi `714`/`719`/`724` restano delicati).
- **Diagnostiche:** opcode checked (uninit, OOB, null) e unchecked.
- **Debug:** `LINE` solo in compilazione debug; debug-info per funzione (nome, tipo, registro locale).
- **Regola d'oro:** ciò che serve sia all'albero sia alla VM vive nel runtime condiviso, non nello switch di `IrEvalExpr`.

---

## 4. Gate comuni

- **Correttezza:** suite dual A-IR e B-NO_IR a zero fail (target corrente **719/0**; aggiornare il numero in F0 se la suite cresce). Micro non peggiora rispetto al baseline F0.
- **Prestazioni:** ogni fase sul percorso caldo riporta tempi prima/dopo sui bench **non scorciabili** (5–10 run alternati, rumore dichiarato).
- **Micro:** codice solo-Full in file esclusi da `Makefile.minimal`; micro linka senza oggetti `ir_*` (oggi via `ir_stubs.c`); `size` per oggetto a ogni fase; ogni crescita di oggetti condivisi (`type.c`, `variable.c`, `heap.c`, `parse.c`, `lex.c`) va motivata.
- **Nessun cambio di semantica** (uninit, const, scala puntatori) per facilitare il lowering.

---

## 5. Fasi

### Fase 0 — Inventario, bench non scorciabili, baseline, pulizia

**Nessun cambio di comportamento.**

- Tabella file → responsabilità e inventario residui (`VariableGet`/`TableGet`, Intrinsic entry points) nel nuovo `HANDOFF`.
- **Aggiornare `HANDOFF §0/§2.4`**: bytecode = direzione; rimuovere riferimenti a Irb.
- Bench non scorciabili: `fib` ricorsivo puro (contatore globale e per puntatore), ricorsione multi-arg, loop con chiamate; `run_bench.py` vs Python.
- Baseline tempi (Full) e `size` per modulo (Full e micro).
- Test differenziale IR vs `NO_IR` che **ignora la colonna del caret** e elenca solo gap sostanziali.
- Allineare doc Point 1–4 (rimuovere «tree ibrido» / SEGV `printf` non riproducibili).
- Fix minori: `make token_v2` (quoting `-DVER`); rinominare `280_irb_*` se ancora presente; rimuovere `IrStDelegate` se 0 occorrenze.
- **Allineamento D1:** documentare che F4 = consolidamento, non estrazione ex novo.

**Deliverable F0:** `docs/HANDOFF.md` aggiornato, tabella baseline, script bench non scorciabili, elenco gap IR↔NO_IR sostanziali.

### Fase 1 — Prototipo esteso della VM (GATE; ramo separato; parallelo a F2–F5)

Estendere oltre `fib` intero: `long`/`float`/`double`, array, host `printf`, load/store **checked** e **unchecked**, superistruzioni (cmp+branch), **heap**: `malloc`/`free`, store a offset (`p[i]`, `*(p+k)`), free invalida e double-free (riprodurre `714`, `719`, `724`). Misurare su ricorsione, `array_sum`, `isort`, `string_proc`, `dfs_rec`.

**Soglie a bande** (riferimento Python 3.12 sullo stesso host; `fib(30)` ptr-counter ≈ 0,16 s):

| Banda | Senza controlli | Con controlli | Azione |
|---|---|---|---|
| **Go** | ≤ 0,05 s | ≤ ~0,12–0,15 s | si procede con l'emettitore |
| **Hold** | — | 0,15–0,25 s | rivedere formato / densità opcode checked |
| **Stop** | — | ≥ 1,5× Python senza profilo chiaro | fermare la direzione |

Il prototipo 0,020 s (solo interi, no check/host/heap) **non** vale da solo per la banda: servono tutte le voci sopra. Non entra nel trunk finché il gate non passa.

### Fase 2 — Completamento della tipizzazione dell'IR

Non è un refactor estetico.

- Chiudere la propagazione tipi dove manca: array owned, cast, nodi fuse, kernel `Index`.
- Gate regressione signedness/narrow: `718_uchar_wrap_loop`, `720`–`723`, e i **17 repro** di `babyc_repro_fuzz_bench_v5` (IR + `NO_IR`).
- Eliminare controlli runtime ridondanti dove i flag bastano: `IrExprIsFP`, `IrPtrElemSizeOf`, `IrMemberPtrElemSize`, `IrAssignTargetIsConst`, marcatura array-decay (conteggio siti da rifare sul tree attuale in F0).
- **`IrVerify`** (build test): ogni corpo compilato ha tipo, larghezza, `ElemSize` coerenti; nessun nodo richiede decisione di tipo a runtime. Eseguire su tutte le funzioni IR della suite.
- Gate: suite dual verde, `IrVerify` senza errori, tempi prima/dopo.

### Fase 3 — Staccare l'IR dal token path e dalla ricerca per nome

- Globali a compile-time (indice o indirizzo) al posto dei residui `IrVarTableGet` sul hot path.
- Rimuovere le chiamate residue a `ExpressionParse`/`ExpressionCoerce` da exec: censire e sostituire con operazioni tipizzate.
- Controllo string-literal su assegnazione a compile-time quando il tipo del target è noto.
- Bridge stretto: pack verso native in un solo punto (`IrPack*`); evitare `interpreter.h` in exec dove possibile.
- Gate: hot path IR non raggiunge il motore espressioni token, salvo ponte native.

### Fase 4 — Runtime condiviso (consolidamento, non estrazione ex novo)

Il tree post-D1 ha già `ir_ops.c` e `ir_mem.c`. Questa fase:

- Stabilizza le API pubbliche in `ir_runtime.h` / bridge usabili da **albero e futura VM**.
- Completa eventuali residui ancora in `ir_exec` glue che appartengono a ops/mem.
- Non ri-spezza `ir_eval` / `ir_stmt` (ritirati in F8).
- Makefile / MSVC allineati; micro continua a escludere oggetti `ir_*`.
- Gate: suite identica, zero cambio semantico, nessuna regressione tempi.

### Fase 5 — Linearizzazione (`ir_lower`)

- `ir_lin.h` / `ir_lower.c`: `IrLinInst` con 8–12 op iniziali (load/store slot, immediato, binop/unary, branch cond/uncond, call, ret, label); estese solo su percorso reale.
- Espressioni pure e controllo di flusso (`if`, cicli, cortocircuito, `switch`, `goto`) → basic block + label.
- **Policy default:** lowering dietro flag (`BABYC_LOWER=1`) fino a shadow test stabile; poi on di default in Full test, off in micro.
- **Shadow test:** `IrEvalLin` in parallelo a `IrEvalExpr` su sottoinsieme crescente; confronto e ricaduta sull'albero se differisce.
- Fast-path (fuse, tight, kernel) restano ottimizzazioni dell'executor ad albero; non si replicano nell'emettitore senza misura. Vietato aggiungere `IrExprKind` solo per ottimizzare.
- Gate: suite verde con shadow test su sottoinsieme crescente.

### Fase 6 — Emettitore e VM, affiancati all'oracolo

- Opcode e formato istruzione (decisione aperta: 32-bit fissi vs lunghezza variabile).
- Emit da `IrLinInst` → bytecode a registri; VM in modulo proprio; host call verso libreria BabyC.
- `BABYC_EXEC=ir|vm|both` (both: confronto output e diagnostica). Preferire both su sottoinsieme + nightly completo.
- **Gate minimo merge:** suite verde sulla VM + nessuna differenza sostanziale vs oracolo in `both`.
- **Obiettivo di ciclo (§10):** sui bench non scorciabili Full batte Python anche su `isort` / `string_proc` in modalità checked — non bloccante al primo merge.

### Fase 7 — Debug sulla VM

- Compilazione debug: `LINE` + debug-info; no superistruzioni / fast-path in debug.
- Comandi: `break <riga>`, `clear`, `list_breakpoints`, `step`, `next`, `finish`, `continue`, `bt`, `locals`, `print <expr>` (sola lettura da registri + debug-info).
- **Politica:** con `--debug` il programma parte **fermo all'ingresso di `main`**; da lì e a ogni stop si gestiscono i breakpoint. **Non si supporta** attach a un'esecuzione non-debug già avviata (`Ctrl-C` termina).
- Test: `tests/debug/*.c` + `.in` + `.expect` (breakpoint, step/next/finish su ricorsione, print di tipi vari, errore runtime in debug).
- Rimuovere in Full i punti in cui `DebugMode` forza il token path in `parse.c` (restano solo per micro se servono).

**Fase opzionale — debug minimo su albero** (solo se serve prima della F7): `IrDebugActive` all'avvio, breakpoint/step in `IrExecuteStmt`, fast-path off, print/locals dal frame. Piccolo; sostituito da F7.

### Fase 8 — Pulizia di Full

- Ritirare l'executor ad albero a VM stabile (restano albero IR come IR intermedia e `ir_lower` come middle-end).
- Token path in Full solo con `BABYC_NO_IR` o codice non compilabile (obiettivo: zero in suite).
- Acceleratori token (tight loop, fast array fill, ecc.) fuori dalla build Full.
- **Analyze** (`BABYC_ANALYZE`, solo Full): offset `{`→`}` sempre a definizione; in execute skip a `}` senza diagnostica; in analyze skip off e validazione su tutto il corpo; errori file:riga:colonna. Aperto: solo sintattici vs anche semantici.

### Fase 9 — Micro a pochi KB (SOSPESA)

Schede per feature (float, struct, fptr, printf completo, switch/goto, multi-dim, macro args, lib, interattivo, riga/colonna, diagnostiche): byte guadagnati, test che smettono di passare, impatto utente. Decisione utente. Serve budget numerico (§8).

TOKEN_V2 / skip offset: solo se riducono byte/RAM micro; correggere prima i gap noti (`556`, `582`, `575`, colonne).

---

## 6. Ordine e dipendenze

```
F0 ──┬─► F1 (gate, ramo separato) ──────────────────────────┐
     ├─► F2 ─► F3 ─► F4 ─► F5 ───────────────────────────────┼─► F6 ─► F7 ─► F8
     └─► F9 (sospesa, indipendente)
```

- F6 solo con F1 in banda **Go** e F2–F5 chiuse.
- Se F1 è Hold/Stop: rivedere formato; F2–F4 restano utili all'oracolo; non toccare F5–F6.
- Fase opzionale debug-albero solo se il debug serve prima di F7.

---

## 7. Cosa non fare

| Divieto | Perché |
|---|---|
| Merge VM sul trunk prima di gate F1 e di F2–F5 | motore non isolabile / non verificato |
| Replicare fast-path nell'emettitore senza misura | la VM senza kernel già batte IR+kernel su alcuni microbench del prototipo |
| Aggiungere `IrExprKind` solo per ottimizzare | policy in vigore |
| Cambiare semantica uninit/const/scala puntatori per il lowering | diagnostica = valore del progetto |
| Far dipendere il lowering da `Value*` o ricerca per nome | tradisce F2–F3 |
| Rollback dello split D1 o ri-spezzare eval/stmt | albero ritirato in F8; F4 = consolidamento |
| WASM nel perimetro corrente | rimandato |
| Promettere «N× più veloce» senza misura | vietato |

---

## 8. Decisioni aperte

1. **Budget micro:** KB flash/RAM e architettura target (Fase 9).
2. **Formato istruzione / set opcode** (Fase 6).
3. **Heap della VM:** riuso tracciamento attuale vs nuovo.
4. **Diagnostiche sempre on vs solo checked.**
5. **Analyze:** solo sintattici nei rami saltati, o anche semantici.
6. **Debug:** confermare «fermo a `main`, no attach» (Fase 7).
7. **Soglie F1:** riconfermare bande col prototipo esteso.

---

## 9. Rischi

| Rischio | Mitigazione |
|---|---|
| Controlli diagnostica annullano il guadagno VM | Gate F1 prima dell'emettitore |
| Tipizzazione incompleta (uchar, narrow, cast) | F2 + `IrVerify` + regress 718/720–723 + 17 repro |
| Residui token/nome sul hot path | F3 prima del lowering |
| Heap fragile (714, 719, 724) | già nel prototipo F1 |
| VM diverge dalla semantica | oracolo + `both` + shadow test lowering |
| Emettitore ad hoc sui fast-path | F5 lineare; pattern solo su misura |
| Bench scorciabili | bench non scorciabili in F0 |
| Doppio costo albero+lineare | `BABYC_LOWER` flag fino a stabilità |
| Linkage gcc/MSVC dopo tocchi runtime | pezzo a pezzo, entrambi i toolchain |
| Simboli Full che gonfiano micro | solo-Full fuori da `Makefile.minimal`; gate `size` |
| Doc disallineati | un solo `HANDOFF` aggiornato a ogni fase |

---

## 10. Criteri di successo

1. Bench non scorciabili Full battono Python 3.12 (incluse `isort` e `string_proc`) in modalità checked — obiettivo di ciclo.
2. Suite dual a zero fail sulla VM; `both` senza differenze sostanziali.
3. `IrVerify` pulito; zero `VariableGet`/`ExpressionParse` sul hot path IR.
4. Sessioni debug F7 verdi; nessun check debug nell'esecuzione normale.
5. Micro non cresce; linka senza `ir_*`; feature rimosse solo con scheda (F9).
6. Un solo `HANDOFF` allineato al codice.

---

## 11. Fuori perimetro

WASM; riscrittura di `expression.c`; nuove feature di linguaggio; diagnostica semantica in analyze finché non decisa; ottimizzazioni dell'executor ad albero oltre alla Fase 2.

---

## 12. Changelog rev.2 (2026-10-03)

- Metriche §2 allineate a dual 719/0, D1 chiuso, TOKEN_V2 chiuso, fix repro-v5.
- Decisione 10 e Fase 4: **consolidamento** runtime post-D1, non estrazione da zero; no rollback eval/stmt.
- Fase 2: gate esplicito 718/720–723 + 17 repro; `IrVerify`.
- Fase 5: policy `BABYC_LOWER` flag.
- Fase 6: gate minimo merge separato dall'obiettivo «battere Python».
- F0: obbligo di riscrivere baseline e documentare allineamento D1.
