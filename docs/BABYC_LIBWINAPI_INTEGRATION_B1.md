# BabyC / libWINAPI integration — B1

## Scope

This is the first runtime integration tranche, based on the supplied BabyC source archive and the available libWINAPI C53 source snapshot. It does not claim complete Win32 coverage yet.

## Runtime architecture

- `src/winapi/` vendors libWINAPI source and public headers.
- On Linux, `src/cstdlib_ext/win.c` registers interpreted Win32 entry points backed by libWINAPI or portable POSIX implementations.
- On Windows, the same translation unit retains the native Win32 adapter branch; the CMake/POSIX libWINAPI sources are not added to the MSVC project.
- `src/include.c` registers both `windows.h` and the legacy `win.h` to `WinSetupFunc` on either host. The separate BabyPOSIX registrations for `unistd.h`, `fcntl.h`, and `dirent.h` remain intact.
- `src/cstdlib_ext/winerror_defs.h` is generated from the numeric `ERROR_*` and `NO_ERROR` constants in `src/winapi/include/werror.h`, so interpreted Windows code can reference the public error catalog instead of only a few hard-coded errors.

## Linux APIs wired in this tranche

- Last error, sleep, tick count, process/thread IDs, process exit, system/local time and performance counter/frequency APIs.
- File handles: `CreateFile`, `ReadFile`, `WriteFile`, `CloseHandle`, `SetFilePointer`, `GetFileSize`.
- Current directory and file enumeration: `GetCurrentDirectory`, `SetCurrentDirectory`, `FindFirstFile`, `FindNextFile`, `FindClose`.
- Standard handles: `GetStdHandle` and `WriteFile` to stdout/stderr, `ReadFile` from stdin.
- Environment and host helpers: `GetEnvironmentVariable`, `SetEnvironmentVariable`, `GetTempPath`, `GetModuleFileName`, `GetComputerName`, `GetUserName`.
- Filesystem helpers: `CopyFile`, `DeleteFile`, `MoveFile`, `CreateDirectory`, `RemoveDirectory`.
- The `Beep` compatibility helper emits the terminal bell; it cannot reproduce Windows frequency/duration control.
- All 727 numeric `ERROR_*`/`NO_ERROR` macros in the vendored catalog are made visible to BabyC; regenerate with `python3 tools/gen_winerror_defs.py` if `werror.h` changes.

## Explicit Linux limitations

Window-management, cursor, keyboard-injection and shell-launch helpers from the legacy `win.c` are registered to preserve source-level API names, but return failure/zero and set `ERROR_CALL_NOT_IMPLEMENTED`. A real Linux desktop backend is still required for those semantics; they must not be mistaken for working GUI implementations.

The next tranche should expose the remaining libWINAPI public APIs (message formatting, events/threads/mutexes, mappings, drive information, memory statistics, string helpers and other file APIs) to BabyC, then build a bidirectional API inventory against the legacy `win.c` and BabyPOSIX declarations. Windows-native compilation still needs verification on a Windows/MSVC toolchain.

## Regression coverage

`src/tests/windows/winapi_console_file.c` checks both `windows.h` and `unistd.h`, the Win32 error catalog, last-error state, file create/write/read/seek/size/close, file search, environment and filesystem helpers, standard output, and the explicit failure behavior of GUI stubs.

Run from `src/`:

```sh
make
make -C tests windows-tests
make -C tests posix-tests
BABYC_NO_IR=1 ./babyc tests/windows/winapi_console_file.c
```
