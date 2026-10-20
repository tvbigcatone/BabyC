# Delta libWINAPI — GetFileInformationByHandle (2026-10-20)

## Modifiche

- Aggiunta la struttura `BY_HANDLE_FILE_INFORMATION` e la dichiarazione pubblica `GetFileInformationByHandle` in `src/winapi/include/wfiles.h`.
- Implementata la lettura di metadati tramite `fstat()` sotto lease dell'handle: attributi base, timestamp, seriale-volume derivato da `st_dev`, dimensione a 64 bit, numero di link e indice file a 64 bit derivato da `st_ino`.
- Gli output vengono costruiti in una struttura locale e copiati al chiamante solo a operazione completata; puntatore output nullo, handle invalido, tipi di file non supportati e timestamp fuori intervallo falliscono esplicitamente.
- `ftCreationTime` è una proxy basata su `ctime` POSIX, perché il backend non espone portabilmente il birth time. `dwVolumeSerialNumber` e file index sono identificatori derivati dal filesystem host, non garanzie di identità cross-volume equivalenti a Windows.
- Aggiunti test per attributi, dimensione vuota e non vuota, link/index, timestamp, output nullo e handle chiuso.

## Verifica

- Build CMake standard con `-Werror`: superata; CTest 1/1.
- Build con `WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`, `-fanalyzer -Werror`: superata; CTest 1/1.
- Build ASan+UBSan con leak detection, thread e shared memory abilitati: superata; CTest 1/1.
- Build nativa MSVC/Windows non eseguita in questo ambiente.

## Limiti

Implementazione per file regolari del backend. Non simula handle Win32 di console, pipe, device o file remoti. Gli attributi e gli identificatori riportati sono un mapping minimo dal filesystem POSIX e non una replica completa delle semantiche NTFS.
