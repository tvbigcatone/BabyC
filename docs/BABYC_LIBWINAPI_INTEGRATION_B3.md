# BabyC / libWINAPI integration — B3

## Scope

B3 continues the `windows.h` API-surface audit and exposes `GetFileSizeEx`, which is already implemented by libWINAPI and provides the 64-bit file-size result without relying on the legacy split high/low output of `GetFileSize`.

The interpreter bridge registers the function in both host branches:

- Native Windows: forwards to the native Win32 `GetFileSizeEx`.
- Linux: forwards to the vendored libWINAPI `GetFileSizeEx` implementation.

The interpreted declaration uses `BOOL GetFileSizeEx(HANDLE, LARGE_INTEGER *)`, so the `LARGE_INTEGER` result is written through a pointer and does not require passing a structure by value through the interpreter's C ABI.

## Regression coverage

`src/tests/windows/winapi_console_file.c` now checks that `GetFileSizeEx` succeeds for an opened test file and reports the same byte count as `GetFileSize` and the payload length. The existing file cleanup and error-return behavior is preserved.

## Validation

- Linux build succeeds with the repository Makefile.
- WinAPI integration regression passes with IR and `BABYC_NO_IR=1` (`WinAPI bridge: ok` in both modes).
- BabyPOSIX regression tests pass (12 tests).
- Full dual interpreter corpus: 923/923 IR, 923/923 NO_IR, and 923/923 IR with shadow disabled.
- Native Windows/MSVC or MinGW compilation remains unverified in this environment.

## Outstanding items

- `CreateThread` still needs a safe trampoline between interpreted BabyC callbacks and native C thread entry points; it is intentionally not exported.
- Linux libWINAPI does not currently support named events / `OpenEvent`.
- GUI/window-management compatibility names remain explicit unsupported stubs on Linux.
- Continue the API inventory for additional file metadata and path APIs, distinguishing APIs implemented by libWINAPI from those that would need new implementations.
