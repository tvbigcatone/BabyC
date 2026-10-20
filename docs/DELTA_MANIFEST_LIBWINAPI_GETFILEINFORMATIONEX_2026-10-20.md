# Delta cumulativo libWINAPI — GetFileInformationByHandleEx (2026-10-20)

Baseline iniziale: `babyc_baseline_2026-10-19_phase2t1.zip`

Questo intervento aggiunge `GetFileInformationByHandleEx` per `FileBasicInfo`, `FileStandardInfo` e `FileAttributeTagInfo`. Le classi non implementate vengono rifiutate esplicitamente; i buffer vengono validati prima dell'accesso e i dati sono copiati solo dopo il completamento della query.

## Contratti e limiti

- `FILE_BASIC_INFO` ha layout di 40 byte; `FILE_STANDARD_INFO` 24 byte; `FILE_ATTRIBUTE_TAG_INFO` 8 byte. I test verificano queste dimensioni.
- `CreationTime` è un proxy `ctime` su POSIX; `ChangeTime` è `ctime`. Non equivale al birth time NTFS.
- `AllocationSize` deriva da `st_blocks * 512`, `EndOfFile` da `st_size`; il valore allocato può variare secondo filesystem e sparse files.
- `ReparseTag` è zero per i file regolari supportati. Le classi di modifica, rename, disposition, file ID, stream e directory non sono implementate.
- La build nativa MSVC/Windows non è stata eseguita in questo ambiente.

## Test

La build standard con `-Werror` e CTest 1/1 è stata eseguita con esito positivo dopo l'implementazione. Verranno registrate nel delta cumulativo le verifiche sulle altre configurazioni solo dopo averle eseguite.


### Estensione C-prio19

Aggiunte le classi `FileNameInfo` e `FileNormalizedNameInfo`, con conversione UTF-8→UTF-16, validazione del buffer e test per nomi Unicode e file scollegati. Il percorso dipende da `/proc/self/fd` su Linux.
