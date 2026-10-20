# BabyC W36 — primo passo sul contratto di storage FP

Data: 2026-10-14

## Modifica effettiva

- `ValueType` ora porta `FPFormat`, metadato distinto dal `Base` e da `Sizeof`.
- Sono stati aggiunti `TypeFPStorageSize()` e `TypeFPStorageAlign()` per centralizzare le proprietà di storage in base al formato.
- Il tipo FP legacy è marcato esplicitamente `FPStorageBinary64`, preservando il comportamento precedente finché il parser non distingue ancora `float` e `double`.
- `TypeSizeValue()` e `TypeSize()` usano l'helper centrale per oggetti FP scalari e come elementi di array.

## Limite intenzionale

Questa è infrastruttura compatibile, non il fix completo. `float` e `double` continuano a essere associati allo stesso tipo legacy; quindi il probe architetturale resta rosso. Non si dichiara corretto il layout binary32 né il rounding delle operazioni `float`.

## Verifiche

- Build GCC da `src/`: passata, zero warning.
- `python3 tools/test_fp_modes.py --babyc src/babyc`: 28/28.
- `python3 tools/audit_fp_ir_arch.py --babyc src/babyc`: atteso fallimento diagnostico, 0/4 modalità conformi; mismatch rimasti in size/layout, rounding binary32 e struct.

## Prossimo passo

Propagare il formato attraverso load/store e accessi indiretti, quindi introdurre un descrittore `FloatType` distinto solo quando le conversioni e i percorsi di memoria possano conservarne il formato senza reinterpretare ogni valore come binary64.
