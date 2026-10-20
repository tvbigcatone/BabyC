# W29rc — prossimi passi

Stato chiuso W29rc: consolidation + gate perf onesto (AST/VM accel/noaccel + Python).
Suite 853/0/23, GCC warn 0, shape vs `attachments/babyc.zip`.

## Policy fissata (non riaprire senza motivo)

1. **Prodotto:** shape/kernel **ON** (default).
2. **Bench/test:** spegnere al volo (`BABYC_NO_SHAPE_FP=1`, `BABYC_NO_KERNEL=1`).
3. **Report:** sempre colonne `*_acc` e `*_noa`; motori si confrontano solo su `*_noa`.
4. **VM kernel:** `IrLinKernel` richiama gli stessi `IrTryFast*` — non confondere con vantaggio di motore.

## Priorità consigliata

### P0 — Igiene residuale (bassa, chiude consolidation)
- [ ] Sanitizer su campione più ampio / full suite (ASan sample era 84/84 dopo leak fix; non full).
- [ ] Documentare in HANDOFF i comandi gate standard (già in `W29rc_PERF_ENGINE_CLARITY.md`).

### P1 — Gap VM semantici (feature wave successiva)
Inventario e chiusura differenziale tree vs `BABYC_EXEC=vm` / `vm_strict`:
- [ ] Catalogo test con `runs=0` o mismatch output (STRICT cover storica ~305/780).
- [ ] Priorità: host+Member/Index args, multi-dim store residuali, eventuali fallback silenziosi.
- [ ] Ogni gap: test regressione + misura `vm_noa` vs `ast_noa` se tocca hot path.

### P2 — Motore VM (performance vera, senza kernel)
Dopo i numeri `vm_noa/ast_noa`:
- [ ] Profiling su fibc/sieve/tri **con** NO_SHAPE+NO_KERNEL (vantaggio già ~2–6×).
- [ ] Ridurre residual tree-in-VM (meno hybrid = più bytecode puro).
- [ ] Non aggiungere shape matcher “per il bench”; solo pattern generali usati in prod.

### P3 — Gate in routine
- [ ] Dopo ogni tocco hot-path:  
  `BABYC=./src/babyc python3 src/tools/perf_gate.py --runs 3 --baseline docs/perf_baseline_W29rc.json`
- [ ] Su macchina nuova: `--write-baseline` prima del `--strict`.
- [ ] Opzionale: CI non-strict (exit 0 + log) vs strict locale pre-merge.

### Esplicitamente fuori scope ora
- Nuovi shape FP / decoy-oriented matchers.
- Gaming dei tempi senza colonna `*_noa`.
- Refactor massivo ir_lower senza gap list prioritizzata.

## Comandi utili

```bash
# suite
python3 tools/run_suite.py src

# gate 5 colonne
BABYC=./src/babyc python3 src/tools/perf_gate.py --runs 3

# VM only smoke
BABYC_EXEC=vm ./src/babyc bench/serie1/fibc.c
BABYC_EXEC=vm BABYC_NO_SHAPE_FP=1 BABYC_NO_KERNEL=1 ./src/babyc bench/isort.c
```
