# Applicazione delta W35

Il pacchetto è cumulativo rispetto alla baseline contenuta nel delta W34.

- Conserva i sorgenti runtime della baseline W34.
- Aggiorna `tools/audit_fp_ir_arch.py` per mostrare i mismatch per categoria.
- Aggiunge `docs/FP_IR_W35_CONTRACT_AND_NEXT_STEPS_2026-10-14.md` e i log W35.
- Non applica il prototipo binary32 respinto e non dichiara risolto il supporto FP.

Per ricostruire: `cd src && make clean && make -j2`.
Per il gate FP: `python3 tools/test_fp_modes.py --babyc src/babyc`.
Per l'audit architetturale: `python3 tools/audit_fp_ir_arch.py --babyc src/babyc` (atteso exit code non-zero finché i mismatch sono aperti).
