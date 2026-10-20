# BabyC — Piano token path V2 (chiuso)

Aggiornato: 2026-10-02  
Zip: `artifacts/babyc-token-path-v2.zip`

## Architettura unica

TOKEN_V2 è **sempre attivo** (niente `#ifdef BABYC_TOKEN_V2`).  
Path: IR di default; token per `BABYC_NO_IR`, debug, micro.

## Fasi

| Fase | Stato |
|------|--------|
| P1 Irb retired | FATTO |
| P2 micro | FATTO |
| Fase 0–1 lex stats / fast HashIfLevel==0 | FATTO |
| Fase 2 TOKEN_V2 | FATTO — **unica** architettura token |
| Fase 3 / 3b skip runtime brace+stmt | FATTO |
| Fase 4 ParserCopyPos (rewind puri) | FATTO sui siti sicuri; macro/body restano full `ParserCopy` |
| BABYC_SKIP_OFFSETS | **ARRESTO** — skip è path freddo (`RunModeSkip`); i kernel `isort`/`string_proc` riparsano in `RunModeRun`. Costo offset table non giustificato (criterio § originale) |

## Misura TOKEN_V2 vs v1

| | v1 | V2 unificato |
|--|-----|--------------|
| text | 384203 | 384451 (+248 B) |
| NO_IR string_proc | ~3.96 s | ~3.82 s (~3%) |

## Line

`Parser->Line` = EOL classico. `TokenLineTab`/`TokenIndex` sempre popolati (oracolo/offset futuri).

## Build / CI

```text
make -j
make -f Makefile.minimal
BABYC_NO_IR=1 ./babyc t.c
./tools/run_suite_dual.sh    # 705/0 ×2 o exit 1
```

## Suite

**705/0 ×2** (IR + NO_IR)

## Post-chiusura (2026-10-03)
- `irb.c` rimosso dal tree (non era in Makefile; P1 Irb retired completato sul filesystem).
- `IrFrameGetByName`/`SetByName` → `ir_frame.c`.

## Debito fuori piano token (prodotto)

1. **Debug Full**: Body-IR + stepping **oppure** controlli uninit/OOB/null anche sul path token.
2. Micro: già non linka `ir_compile`; stub `BabycAnalyze*` solo se si mescolano oggetti Full.

## Apply

```bash
cd src
unzip -o babyc-token-path-v2.zip
cp -a delta_uni/* .
cp -a delta_uni/tests/* tests/
# se presenti vecchi test irb:
test -f DELETE_THESE_FILES.txt && while read f; do case $f in \#*|'')continue;; *) rm -f "$f";; esac; done < DELETE_THESE_FILES.txt
rm -f irb.c
make -j
./tools/run_suite_dual.sh
```
