# BabyC / libWINAPI integration — B2

## Scope

B2 extends the B1 `windows.h` runtime bridge. It exposes more of libWINAPI's existing API surface in the BabyC interpreted API table and registers the same API names on the native Windows branch using Win32 wrappers. The POSIX registrations (`unistd.h`, `fcntl.h`, `dirent.h`) are unchanged.

## APIs added to the `windows.h` bridge

- `FormatMessage` (supported caller-buffer/system-message subset; allocation-buffer mode is not supported).
- `GlobalMemoryStatusEx`.
- Event signaling: `CreateEvent`, `OpenEvent`, `SetEvent`, `ResetEvent`, `WaitForSingleObject`.
- Mutexes: `CreateMutex`, `ReleaseMutex`.
- File mappings: `CreateFileMapping`, `OpenFileMapping`, `MapViewOfFile`, `UnmapViewOfFile`.
- Filesystem capacity and logical drives: `GetDiskFreeSpaceEx`, `GetLogicalDriveStrings`.

The native Windows branch calls the native Win32 APIs; Linux calls the vendored libWINAPI implementation. On Linux, named events (`CreateEvent` with a name and `OpenEvent`) remain unsupported by the current libWINAPI implementation and return `ERROR_CALL_NOT_IMPLEMENTED`. `CreateThread` is deliberately not bridged yet because BabyC's interpreted function-pointer/callback ABI needs an explicit adapter before a C runtime thread can safely invoke an interpreted function.

## Drive-list fallback

In container/overlay environments `/etc/mtab` can contain no block-device entries, causing `GetLogicalDriveStrings` to return an empty list. `update_drive_list()` now synthesizes a single `C:` mapping for the host root filesystem only when no drive entries were discovered. The existing drive-discovery path remains unchanged when it succeeds.

## Regression coverage

`src/tests/windows/winapi_console_file.c` now covers message formatting, physical memory statistics, logical drive enumeration, free-space statistics, manual-reset event signaling, mutex acquisition/release, and file-backed mapping/view/unmap, in addition to B1 file/console/environment checks.

## Validation

- Linux build succeeds with the repository Makefile.
- WinAPI integration regression passes with IR and `BABYC_NO_IR=1`.
- BabyPOSIX regression tests pass.
- Full dual interpreter corpus: 923/923 IR, 923/923 NO_IR, 923/923 IR with shadow disabled.
- `make -C tests test` cannot run as currently configured because the legacy target list references missing `00_assignment.test` prerequisites; use `tests/run_suite_dual.sh` for the actual extant corpus.
- A native Windows/MSVC or MinGW build was not available in this environment and remains unverified.

## Remaining API gaps in this tranche

- `OpenEvent` and named `CreateEvent` are exported for source compatibility but remain unsupported by Linux libWINAPI (they report `ERROR_CALL_NOT_IMPLEMENTED`).
- `CreateThread` is not exported until BabyC can safely trampoline interpreted callbacks into native threads.
- The older GUI/window-management names remain registered as explicit unsupported stubs on Linux.
