# BabyC — esperimento d'implementazione binary32 W34 (2026-10-14)

## Esito

**Esperimento respinto e modifiche sperimentali rimosse.** Non è stato lasciato nella baseline il prototipo che introduceva un secondo oggetto `ValueType` con `Base == TypeFP` e `Sizeof == sizeof(float)`.

## Cosa è stato provato

Il prototipo separava `TokenFloatType` da `TokenDoubleType` nel parser, aggiungendo `Picoc.FloatType` con storage size 4 ma mantenendo `Base == TypeFP` per non rompere immediatamente i numerosi dispatch FP esistenti.

Il build del prototipo è riuscito dopo aver corretto la dichiarazione locale usata per misurare l'allineamento. Il gate FP è però sceso a **24/28**: i casi token relativi a ritorni FP, intrinsics, parametri puntatore e accessi/assegnazioni ad array hanno mostrato divergenze. Il probe architetturale è rimasto **0/4 modalità conformi**. L'esperimento non è quindi accettabile come implementazione.

## Evidenza architetturale

Il prototipo conferma due fatti distinti:

1. Una distinzione del solo tipo sorgente non è sufficiente: percorsi runtime/token e accessi a memoria continuano a presumere la rappresentazione double.
2. Il lowering IR contiene dimensioni FP hardcoded come `sizeof(double)`, per cui la sola modifica di `ValueType.Sizeof` non fa propagare la dimensione corretta attraverso array e oggetti locali.

Nel probe ripristinato, GCC produce `sizeof(float[3]) == 12`, `sizeof(double[3]) == 24` e struct mista di 24 byte. BabyC Tree/VM/compact riporta array locali da 8 byte e struct mista da 32 byte; il percorso token riporta le dimensioni degli array corrette, ma fallisce ancora i test di precisione binary32. Il probe resta un test diagnostico, non una gate verde.

## Verifiche dopo il ripristino

- Build pulita GCC: completata, nessun warning osservato nel log.
- `tools/test_fp_modes.py --babyc src/babyc`: **28/28**.
- `tools/audit_fp_ir_arch.py --babyc src/babyc`: **0/4** modalità conformi (problema già presente, non introdotto dal prototipo).
- È stato eseguito `make clean` prima della verifica finale per evitare oggetti compilati misti.

## Prossimo passo tecnico obbligato

Prima di introdurre un tipo binary32 visibile al resto del programma, definire un contratto unico per `FP precision`, `FP storage size`, `FP value representation` e `FP load/store conversion`. Poi rimuovere le dimensioni hardcoded nei percorsi di memoria/lowering, rendere le scritture `float` arrotondare a binary32, e solo dopo instradare `TokenFloatType` e `TokenDoubleType` verso descrittori distinti. Le verifiche devono includere Tree, VM lineare, token e compact, array locali/globali, membri struct, puntatori, conversioni, chiamate e ritorni.

Non dichiarare risolto FP IR finché il probe architetturale non passa e i gate esistenti restano verdi.
