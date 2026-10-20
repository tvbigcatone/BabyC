# Delta libWINAPI — FileNameInfo e FileNormalizedNameInfo (2026-10-20)

## Modifiche

- Aggiunto `FILE_NAME_INFO`, con `FileNameLength` in byte e unità UTF-16 da 16 bit esplicite (`WORD`) per preservare l'ABI Win32 anche su host POSIX con `wchar_t` a 32 bit.
- `GetFileInformationByHandleEx` supporta `FileNameInfo` e `FileNormalizedNameInfo` per handle regolari registrati. Il nome assoluto è recuperato da `/proc/self/fd` mentre il lease protegge il descriptor.
- Implementata conversione UTF-8→UTF-16 con verifica di continuation byte, overlong encoding, surrogate isolati, code point fuori range e generazione di coppie surrogate per caratteri fuori dal BMP. Sequenze non valide falliscono con `ERROR_NO_UNICODE_TRANSLATION`.
- I buffer troppo corti falliscono con `ERROR_INSUFFICIENT_BUFFER` senza modifiche parziali. Handle il cui file è stato scollegato dal filesystem non espongono il suffisso Linux ` (deleted)`.

## Test e verifiche

- Test su path ASCII e UTF-8 (`è`), classi nome normalizzato/non normalizzato, buffer insufficiente, file unlinkato con handle ancora aperto e nome POSIX con byte non UTF-8.
- Build + CTest superati: GCC `-Werror`, ASan/UBSan con leak detection (10 ripetizioni), GCC `-fanalyzer -Werror`, TSan, `WINAPI_DISPLAY=ON`, `WINAPI_X11=ON`, GCC con `WINAPI_THREADS=ON` e `WINAPI_SHMEM=ON`, Clang con thread e shared memory abilitati.

## Limiti

- Dipende da Linux e `/proc/self/fd`; su altri sistemi restituisce non supportato. Solo handle regolari. `FileRenameInfo`, `FileDispositionInfo`, `FileAllocationInfo` e altre classi non sono dichiarate implementate. La compilazione nativa MSVC/Windows non è stata eseguita.
