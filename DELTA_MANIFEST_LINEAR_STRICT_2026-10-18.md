# BabyC cumulative delta manifest — 2026-10-19

Baseline esclusiva: `babyc_baseline_2026-10-18_loop_promotion.zip`. SHA-256 baseline: `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`. Il pacchetto è un delta cumulativo rispetto a questo ZIP: include le correzioni del delta del 18 ottobre e la rinumerazione completa di `src/tests` del 19 ottobre.

## Rinumerazione

- 1.009 sorgenti numerate univoche e contigue `001..1009`, tre cifre fino a 999 e quattro da 1000.
- 1.001 test numerati con `.expect`; otto fixture numerate source-only.
- `TEST_RENUMBER_MAP.csv`: 2.035 righe file-per-file, inclusi i 10 percorsi identici e tutti gli asset associati; 2.025 percorsi rinominati.
- `TEST_RENUMBER_RESIDUALS.csv`: vecchio/nuovo numero dei 13 residui Linear STRICT.
- Per applicare il delta: prima elimina i percorsi elencati in `DELETE_FILES.txt` dalla cartella `babyc/`, poi estrai lo ZIP preservando i percorsi.

## Verifica

Build Linux/GCC: `make -C src clean && make -C src -j2`, exit code 0, zero warning/errori. Matrice su 1.001 casi `.expect`: Tree 1001/1001 con ACCEL 0/1; Linear STRICT 988/1001 con ACCEL 0/1 e gli stessi 13 residui. Nessun nuovo fail. MSVC/Windows è lasciato alla verifica locale; bytecode fuori ambito.

La documentazione completa dei comandi e degli output reali è `docs/reports/TEST_RENUMBERING_2026-10-19.md`. `MANIFEST_SHA256.txt` copre `src/`, `tools/`, `docs/` e `bench/`; `SHA256SUMS` copre i file distributivi non compilati, escludendo se stesso e gli artefatti di build.
