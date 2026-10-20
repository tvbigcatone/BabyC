## Handoff prioritario — Linear STRICT dopo la chiusura `047_grep` (2026-10-19)

Baseline esclusiva: `babyc_baseline_2026-10-18_loop_promotion.zip`, SHA-256 `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`. Delta cumulativo in ingresso: chiusura 651, SHA-256 `f77e4ef698b4c83df642ecf01a3e3a35f821a0ad0913e9796ba65acbb95a92e6`. Applicare il delta cancellando prima i percorsi di `babyc/DELETE_FILES.txt` e poi sovrapponendo i file.

- **Chiuso `047_grep`:** il corpo `main` viene eseguito in Linear STRICT, senza fallback, con output uguale a Tree e al `.expect` con ACCEL=0/1. La causa era duplice: veto sull'indicizzazione annidata con radice parametro (`argv[1][0]` via `char **`) e lookup globale rigido durante lowering dei rami runtime non eseguiti (`documentation` è esclusa da `#if 0`). La prima correzione ha poi esposto un veto di `FnCodeIndexedPointerParamsSafe()` sull'aritmetica esplicita pointer-plus-offset; il gate ora riconosce la sola forma larga emessa dall'emettitore, mantenendo il controllo per gli altri usi.
- **Nuovo test 1015:** `1015_param_ptrptr_index.c/.expect`, esercita davvero `char **` e `text[0][1]` (output `98`). Nessun `.expect` esistente modificato.
- **Matrice finale ricostruita:** Tree 1007/1007 a entrambi i valori ACCEL; Linear STRICT 1006/1007 a entrambi, unico residuo `126_csmith_modulo_zero`. La causa di 126 è distinta (`lower_reject` su espressione binaria del caso Csmith/modulo-zero), non una variante del problema 047.
- **Prestazioni:** A/B alternato tra il binario 651 e candidato 047, output stdout identici. Almeno 7 coppie per ognuna delle otto configurazioni dei quattro benchmark; `string_scan` ACCEL=0, entrambi `string_scan_bounded` e `dfs_rec` sono stati estesi a 36 coppie. I dump IR sono identici per tutti e otto gli abbinamenti, ma alcune mediane wall-time restano sfavorevoli e il controllo A/A mostra oscillazioni fino a decine di punti percentuali sullo stesso binario. Non è dimostrato un costo runtime/instruction-stream aggiuntivo; nemmeno è corretto certificare costo esattamente zero dalle misure rumorose. Dettagli nel report e CSV sotto `docs/reports/`.
- Build Linux/GCC completata da clean state, 0 warning/errori. Codice senza nuovi header POSIX o estensioni GCC. MSVC/Windows non compilato in questa sessione; compatibilità da verificare localmente. Bytecode compatto fuori perimetro.

Prossimo residuo Linear STRICT: `126_csmith_modulo_zero`, diagnosi autonoma; non modificarlo attribuendogli la causa del 047. Report: `docs/reports/LINEAR_GREP_047_CLOSURE_2026-10-19.md`.

---

## Handoff prioritario — Linear STRICT dopo la chiusura `651_sqlite_exec_null_varargs` (2026-10-19)

Baseline esclusiva: `babyc_baseline_2026-10-18_loop_promotion.zip`, SHA-256 `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`. Applicare il delta cancellando prima i percorsi elencati in `babyc/DELETE_FILES.txt`.

- Chiuso `651_sqlite_exec_null_varargs`: il gate in `src/ir_lin_emit.c` ora consente alle funzioni che risolvono a `Intrinsic` registrata di usare l'ABI comune `Value[]`; la whitelist resta applicata ai soli host name non risolti. Questo corregge un rifiuto del lowering per `sqlite3_open`/`sqlite3_exec`/`sqlite3_close`, non un caso speciale SQLite.
- Test 1014 nuovo: chiamate intrinseche varargs con 0 intero e `NULL` nei parametri opzionali. Output uguale in Tree e Linear STRICT/ACCEL=0,1. Nessun `.expect` esistente modificato.
- Matrice dal pacchetto finale: Tree 1006/1006; Linear STRICT 1004/1006 a ACCEL=0/1. Unici residui Linear STRICT `047_grep`, `126_csmith_modulo_zero`; `058`, `062`, `651` sono chiusi.
- Prestazioni confrontate contro il delta 058, 21 coppie alternate per ogni configurazione e 36 per cinque configurazioni inizialmente sfavorevoli; ogni output stdout/hash è identico. Le mediane appaiate restano nel rumore (−2,55%..+1,66%), con outlier ampi. Non dichiarare costo esattamente zero come dimostrato; CSV completo e numeri per configurazione nel report.
- Build Linux/GCC pulita, 0 warning/errori. MSVC/Windows non eseguito in questa sessione; nessun header POSIX o estensione GCC aggiunti dal fix. Bytecode compatto fuori ambito.

Prossimo passo: indagare `047` e `126` separatamente con `BABYC_DUMP_LIN=1`. Non promuovere un esito se STRICT ha usato fallback e non modificare `.expect` storici per nascondere divergenze.

---

## Storico Handoff — Linear STRICT dopo la chiusura `058_macro_bug` (2026-10-19)

Baseline esclusiva: `babyc_baseline_2026-10-18_loop_promotion.zip`, SHA-256 `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`. Applicazione cumulativa: cancellare i percorsi di `babyc/DELETE_FILES.txt`, poi sovrapporre il delta finale.

- Corpus: sorgenti numerate `001..1013`, 1.005 `.expect`, 8 source-only; nessun `.expect` esistente modificato. L'unica expect nuova è `1013_conditional_fp_mixed.expect`.
- Chiuso `058_macro_bug`: la riga `z = MIN(y, 13.5)` non viene più rifiutata in Linear STRICT. Root cause nel gate FP: annotazioni IR propagavano il flag FP alle relazioni (che restituiscono int) e il gate non ammetteva il ternario FP della macro. Ora il lowering decide il tipo-valore, converte i rami e promuove a double gli operandi misti; le conversioni implicite negli assegnamenti e negli inizializzatori scalari sono condivise.
- Build pulita Linux/GCC: 0 warning/errori. Matrice sulla ricostruzione dal pacchetto finale: Tree 1005/1005; Linear STRICT 1002/1005 a entrambi i valori ACCEL; residui `047_grep`, `126_csmith_modulo_zero`, `651_sqlite_exec_null_varargs`, senza timeout.
- Per la diagnosi consultare `docs/reports/LINEAR_MACRO_058_CLOSURE_2026-10-19.md` e i dump `linear_macro058_before/after_*`. La nuova prova confronta output reale con `.expect` normalizzando soltanto CRLF/LF e preservando gli spazi significativi, non solo la diagnostica.
- Performance Linear: almeno 7 coppie AB/BA in tutte le 8 combinazioni; 21 coppie di follow-up sulle due scan workload. I dati sono completi, compresi i risultati sfavorevoli; la dispersione è ampia nelle misure accelerate da pochi millisecondi e non supporta una conclusione di overhead zero, ma il follow-up non dimostra un rallentamento stabile oltre il rumore.
- **Residuo di verifica piattaforma:** MSVC/Windows non è stato eseguito qui. La patch usa C portabile e non aggiunge header POSIX/estensioni GCC; serve comunque la conferma MSVC locale. Bytecode compatto non coinvolto.

Decisioni attive: Tree resta oracolo/default; non mascherare fallback; niente fast-path con riconoscimento della forma del test; il prossimo obiettivo suggerito è il più vecchio residuo Linear STRICT tra `047`, `126`, `651`.

---

## Aggiornamento 2026-10-17 — prima scansione libc/UB

- Regola di prodotto: quando la libc BabyC può identificare una violazione delle precondizioni C al confine della funzione, deve arrestare l'esecuzione con diagnostica invece di invocare il CRT host o restituire un risultato plausibile.
- Prima tranche applicata a `string.h`, `ctype.h`, `stdlib.h`, `stdio.h`, `math.h` e `time.h`; coperti anche i wrapper legacy in `src/clibrary.c`. Dettagli, test e gap aperti in `docs/LIBC_UB_AUDIT_2026-10-17.md`.
- Nuovi test 1050–1054; suite duale 965/965 per ciascuna delle tre configurazioni e FP 32/32. ASan/UBSan mirato 083 e 1050–1054 senza segnalazioni. Bytecode gate timed out a 120 s; MSVC e full sanitizer corpus non verificati. I test 1050–1052 non passano in Linear strict per il limite preesistente `strict: tree fallback (body)`.
- Non dichiarare la libc “UB-proof”: restano soprattutto bounds/provenienza degli oggetti stack/global, completezza del tracking heap, format `printf`/`scanf`, `gets`/`sprintf` e la scansione POSIX/estensioni.

---

# BabyC — HANDOFF (W29rc)

**Data:** 2026-10-17 (P0 helpers)  
**Baseline di partenza:** `attachments/babyc.zip`  
**Stato numerico:** `docs/CURRENT_STATUS.md`  
**MSVC:** OK (conferma utente locale)

---

## 0. Decisioni attive

1. **Executor ad albero = oracolo.** IR/VM/bytecode concordano o falliscono con fallback esplicito.
2. **VM / bytecode compact = sperimentali.** `runs=0` atteso ≠ coverage.
3. **Path token** (`BABYC_NO_IR` / micro) permanente come front-end e fallback.
4. **Costo zero a flag spento** su path non attivi.
5. **Nessuna promessa di velocità non misurata.**
6. **Anti-benchmark-tuning:** fast-path solo se pattern generale/ricorrente.  
   - **A1 chiusa:** rimossa eccezione `key = key` da `IrTryFastInsertShift`; solo corpo a 2 stmt (shift + dec).  
   - **A2 chiusa:** `IrTryFastPtrScanPred` tenuto (scan-count su charset piccolo; forma C reale).
7. **F6/F7 / debug:** experimental, dietro flag.
8. **W29rc = consolidation** completata su doc + A1/A2 + sanitizer smoke; prossima wave = feature/correttezza VM, non nuovi shape matcher.

---



## P0 — Storage + shared helpers (2026-10-17)

**Exec config:** `BABYC_PATH` / `BABYC_ACCEL` / `BABYC_STRICT` (see `docs/P0_EXEC_CONFIG.md`). Legacy EXEC/NO_IR/VM_STRICT still aliased.


**Filosofia:** un significato runtime → un helper. Preferire `static inline` per evitare overhead di chiamata quando il compilatore può inlineare; questo è un obiettivo strutturale, non una garanzia di costo zero misurato.
Cold Decl/Call = funzione condivisa + `GUARD`. Niente duplicazione tree↔linear. Ogni affermazione prestazionale richiede confronto prima/dopo sullo stesso host e workload.

**Helper DONE (P0.24 CLOSED):** H1–H11 (incl. `IrResolveMemberLayout`);
C1–C9 (incl. `IrAllocOwnedBlob` / `IrCopyStructBlob`).

**Lista viva:** `docs/P0_DUP_HELPER_INVENTORY.md`  
**Descriptor:** `docs/P0_STORAGE_DESCRIPTOR.md`

Verifica 2026-10-17: suite duale A-IR / B-NO_IR / C-IR-SHADOW0 = 960/960 ciascuna. I casi mirati di Linear strict e FP sono verificati separatamente; non equivale a una suite Linear completa. Gate bytecode aggregato ancora aperto; il caso 819 passa il test bytecode mirato con `attempts=1, runs=1`. STRICT multidim 964–982 PASS secondo i report dedicati.

## 1. Architettura

```
C source → parse / type
            ├─ TOKEN  (micro, BABYC_NO_IR, fallback)
            └─ BODY-IR
                 ├─ ir_compile → albero
                 ├─ ir_lower → IrLinInst
                 ├─ executor albero (oracolo)
                 └─ ir_vm / bytecode compact (sperimentale)
```

Shape FP (`BABYC_NO_SHAPE_FP=1` disabilita):

- `IrTryFastInsertShift` — insertion-sort shift canonico (`body_n == 2`)
- `IrTryFastPtrScanPred` — `while (*p)` / `while (p < end)` + predicato charset + count

---

## 2. Comandi

```bash
cd src && make clean && make -j$(nproc)
python3 tools/run_suite.py src              # atteso: 0 fail
python3 tools/test_bytecode_vm.py           # atteso: 0 FAIL
# BABYC_NO_SHAPE_FP=1 ./src/babyc bench/isort.c
```

---

## 3. Aperture residue

| Tema | Note |
|------|------|
| Warning GCC | **0** |
| Sanitizer full | Solo smoke parse-fail + 944 + 003; non corpus intero |
| Gate perf globale | Aperto |
| Gap VM semantici | Post-W29rc |

---

## 4. Documentazione

- `CURRENT_STATUS.md` — numeri e decisioni A1/A2  
- `HANDOFF.md` — questo file  
- `docs/archive/` — W27–W35 e artefatti one-shot  

---

## 5. Process

- Delta-zip solo file toccati; non mascherare fail.
- Fail → root cause + test di regressione.
- Fallback compact non conta come supporto VM effettivo.

*W29rc chiuso su consolidation; feature wave successiva solo dopo questo freeze.*

---

## Update 2026-10-19 — Linear member/call cluster

Closed: `606_storm_inc_dec_member_call`, `609_storm_enum_union_fptr`, `618_inc_member_call_arg`, and shared `454_complex_dispatch`. Fixes are in the shared type resolver, function-designator encoding, typed indirect pre/post increment lowering, and scalar/pointer call-argument validation. No `.expect` was edited and no new fallback was added.

Current gate: Tree 1002/1002 in both ACCEL modes; Linear STRICT 995/1002 in both. Seven remaining failures: `047_grep`, `058_macro_bug`, `062_initializers`, `126_csmith_modulo_zero`, `651_sqlite_exec_null_varargs`, `873_alignment_extended`, `893_vm_struct_return_effect_fallback`. The qsort comparator cluster 947/948 is closed; see `docs/reports/LINEAR_QSORT_COMPARATOR_CLUSTER_2026-10-19.md`. Seven alternating A/B pairs per benchmark/configuration plus 21 additional pairs for each ACCEL=1 benchmark found no persistent >1% regression beyond measured noise. MSVC is not verified in this Linux turn; bytecode remains out of scope.


## Update 2026-10-19 — nested embedded member increment closure

Closed `873_alignment_extended` and added regression test `1011_nested_embedded_volatile_inc`. The old STRICT nested-member increment gate demanded at least one pointer-valued intermediate member; it rejected an ordinary embedded aggregate path and did not fetch the root type for a global aggregate. The emitter now resolves the member chain and obtains the address of the embedded parent member before typed load/modify/store. Final matrix: Tree 1003/1003 and Linear STRICT 998/1003 for ACCEL=0/1; the only five residuals are 047, 058, 062, 126, and 651. The final A/B has seven alternating pairs on all eight benchmark/config combinations; `string_scan_bounded` and `struct_score` with ACCEL=1 were extended to 21 pairs because initial raw median deltas exceeded +1%. No final raw median delta exceeds +1%; short ACCEL=1 timing noise is retained in the CSVs. No existing `.expect` changed. MSVC/Windows remains unverified; bytecode out of scope.


## Update 2026-10-19 — 062 initializers closure

Closed `062_initializers` without Tree fallback. `FnCodeIndexedPointerParamsSafe()` no longer rejects a body simply because an unused pointer parameter exists; active pointer parameters are still checked. Two latent semantics are fixed generally: fold `sizeof` on global objects/arrays and preserve row-to-pointer decay for global multidimensional arrays. New test `1012_global_array_size_row_decay`; no existing `.expect` modified. Current matrix: Tree 1004/1004, Linear STRICT 1000/1004 with ACCEL=0/1. Four residuals are `047_grep`, `058_macro_bug`, `126_csmith_modulo_zero`, and `651_sqlite_exec_null_varargs`. Performance A/B: at least seven alternating AB/BA pairs for each of four benchmarks and both ACCEL values; the initial seven-pair block has no raw median above +1%; supplementary 21-pair ACCEL=1 repeats are mixed due high timing noise. The only raw adverse extended median was `string_scan_bounded` at +3.69% in one block, while a second block was -3.49%, and the paired medians were negative in both. The direct comparison of this patch against the preceding cumulative build was +0.29% raw and -0.12% paired, with IQR crossing zero. Details and dumps: `docs/reports/LINEAR_INITIALIZERS_062_CLOSURE_2026-10-19.md`. MSVC/Windows unverified; bytecode out of scope.
