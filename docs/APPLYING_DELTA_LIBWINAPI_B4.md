# Applicazione del delta libWINAPI B4 alla baseline BabyC

Estrarre questo ZIP nella directory che contiene la cartella `babyc/`, mantenendo i percorsi dell'archivio. I file sovrascritti sono solo quelli esplicitamente elencati nel manifest.

## Visual Studio

Il delta aggiorna anche i progetti Visual Studio presenti nella baseline:

- `src/msvc/babyc/babyc.vcxproj` e `babyc.vcxproj.filters`
- `src/msvc/babyc/babyc_lib.vcxproj` e `babyc_lib.vcxproj.filters`

Entrambi i progetti compilano già `src/cstdlib_ext/win.c`; ora il nuovo header `src/cstdlib_ext/winerror_defs.h` è anche incluso nei progetti e visibile in Solution Explorer. Non vengono aggiunti al target Windows i sorgenti backend Linux di `src/winapi`: il ramo `WIN32` del bridge usa le API native Win32, mentre il backend libWINAPI resta destinato al ramo Linux.

Aprire `src/msvc/babyc/babyc.sln` e compilare le configurazioni desiderate. La compilazione MSVC non è stata eseguita in questo ambiente; il delta non deve essere considerato una certificazione di build Windows nativa.

## Verifica Linux

Dalla directory `src/`:

```sh
make clean
make -j2
./babyc tests/windows/winapi_console_file.c
BABYC_NO_IR=1 ./babyc tests/windows/winapi_console_file.c
make -C tests posix-tests
```

La suite del repository può essere eseguita con `tests/run_suite_dual.sh`.
