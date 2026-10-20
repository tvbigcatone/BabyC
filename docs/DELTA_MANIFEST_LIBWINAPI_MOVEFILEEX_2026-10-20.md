# Delta manifest — MoveFileEx (2026-10-20)

## Implementazione

- Aggiunta `MoveFileEx` e le costanti `MOVEFILE_REPLACE_EXISTING`, `MOVEFILE_COPY_ALLOWED`, `MOVEFILE_DELAY_UNTIL_REBOOT`, `MOVEFILE_WRITE_THROUGH`.
- `MoveFile` ora delega alla variante estesa con flag zero.
- Su Linux, il percorso no-replace prova `renameat2(RENAME_NOREPLACE)` per rendere atomico il controllo di esistenza della destinazione. I sistemi senza questa primitiva usano un fallback portabile con race residua.
- `MOVEFILE_REPLACE_EXISTING` usa `rename` per sostituire la destinazione; `MOVEFILE_COPY_ALLOWED` gestisce `EXDEV` copiando file regolari con `CopyFile` e rimuovendo la sorgente solo dopo copia completata; `MOVEFILE_WRITE_THROUGH` sincronizza la directory di destinazione.
- `MOVEFILE_DELAY_UNTIL_REBOOT` è rifiutato con `ERROR_NOT_SUPPORTED`; flag sconosciuti con `ERROR_INVALID_PARAMETER`.

## Test

- Destinazione già esistente senza replace.
- Replace e write-through.
- Flag sconosciuti e delay-until-reboot.
- Copia cross-volume condizionale se `/dev/shm` è un filesystem distinto, con verifica del contenuto e dell'assenza della sorgente.

## Limiti

- `MOVEFILE_COPY_ALLOWED` supporta file regolari, non directory o symlink cross-volume.
- Se la rimozione della sorgente dopo una copia cross-volume fallisce, l'API restituisce errore ma mantiene entrambe le copie per evitare perdita di dati.
- Se `fsync` della directory fallisce dopo il rename, lo spostamento può essere già avvenuto nonostante il valore `FALSE`.
- Il fallback no-replace su sistemi senza `renameat2` non può garantire atomicità contro processi concorrenti.
- Build nativa MSVC/Windows non eseguita in questo ambiente.
