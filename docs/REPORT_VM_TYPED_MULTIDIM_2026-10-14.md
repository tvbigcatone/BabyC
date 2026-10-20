# BabyC — chiusura del fix multidimensionale e signedness degli array locali

Data: 2026-10-14
Baseline: `babyc(20261014-080706).zip`

## Modifiche

### 1. RHS multidimensionale: classificazione scalare/aggregato

Il fix precedente resta incluso: la specializzazione per copia di `struct`/`union` non deve usare `ElemSize` del nodo `IrExIndex` esterno come se fosse la dimensione dell'elemento finale. Nelle catene annidate tale campo può descrivere una riga/sotto-array; la classificazione ora considera la profondità della catena e limita la specializzazione esistente per copia di aggregati al caso a singolo indice verificato.

### 2. Signedness delle letture dagli array locali multidimensionali

Durante la verifica differenziale è emerso un secondo difetto, indipendente ma vicino al primo: il percorso meta-fast `LDX` degli array locali leggeva gli elementi da 2 byte sempre come `short` con segno, e quelli da 4 byte sempre come `int` con segno. Solo il caso `unsigned char` consultava il tipo dichiarato. Inoltre, il tipo conservato per un array multidimensionale può contenere più livelli `TypeArray`, quindi rimuoverne uno solo non basta a individuare il tipo scalare finale.

È stata aggiunta `IrLinLocalIndexedType`, che risolve il tipo dell'elemento attraverso tutti i livelli `TypeArray` e gestisce separatamente il caso di puntatore locale non posseduto. Il caricamento meta-fast ora effettua l'estensione con segno o zero in base al tipo effettivo per larghezze 1, 2, 4 e 8 byte e aggiorna il flag unsigned del registro.

La regressione è riproducibile sulla baseline originale con `BABYC_EXEC=vm_strict`: per `unsigned short` il valore `65535` veniva stampato come `4294967295`. Con il delta, Tree, VM e VM strict producono lo stesso risultato.

## Test aggiunto

`src/tests/887_vm_local_multidim_unsigned.c` copre array locali 2D/3D di `unsigned char`, `unsigned short`, `unsigned int`, `unsigned long long` e `signed short`, incluse le estensioni a 32/64 bit. L'output atteso è in `src/tests/887_vm_local_multidim_unsigned.expect`.

## Verifiche eseguite

- Clean build GCC con `-Wall -O2`: OK, nessun warning.
- `python3 tools/test_bytecode_vm.py`: **31/31 gruppi passati**.
- Fixture `831`–`834` eseguite in `tree`, `vm`, `vm_strict`: output concordi, inclusi gli accessi 3D/4D.
- Regressione `955` eseguita in `tree`, `vm`, `vm_strict`: output concordi `255 65535 4294967295 18446744073709551615 -32768`.
- Regressioni mirate `882`, `931`, `934`: output concordi in tutte e tre le modalità.
- Il tentativo di build con AddressSanitizer/UBSan non è arrivato alla fine entro il limite della sessione; nessun esito sanitizer viene quindi dichiarato.
- MSVC non verificato su questa revisione.

## Limite distinto emerso durante l'audit

`src/tests/546_ir_struct_array_assign.c` produce ancora valori errati in `BABYC_EXEC=vm_strict`, mentre Tree e VM non-strict producono l'output atteso. Il problema è riproducibile anche sulla baseline originale e riguarda la copia per valore di struct in array locali, non il fix signedness o le letture scalari multidimensionali. È lasciato fuori da questo delta per non includere una correzione non dimostrata; va trattato come prossimo difetto di correttezza VM.
