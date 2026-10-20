# BabyC / libWINAPI integration — B4

## Scope

B4 hardens the `GetFileSizeEx` bridge regression coverage. The API is already implemented by libWINAPI and registered for both native Windows and Linux; this iteration tests the contract more carefully rather than adding an unverified declaration for another API.

## Regression additions

`src/tests/windows/winapi_console_file.c` now verifies that:

- querying the size returns the same 64-bit value as the file's expected byte count;
- querying file metadata does not alter the current file position;
- an invalid file handle returns `FALSE`;
- a null output pointer returns `FALSE` without crashing.

These checks run through the interpreted `windows.h` bridge on Linux, with and without the IR path.

## API inventory notes

`GetFileSizeEx` is implemented in `src/winapi/src/wfile_handles.c` using `fstat()` on the managed file descriptor and validates its output pointer. The implementation releases the handle's I/O lease on both success and failure paths. The interpreter wrapper passes the result by pointer, avoiding by-value `LARGE_INTEGER` ABI complications.

## Validation

Validation performed on this source tree and then repeated from the extracted B4 archive:

- Linux build succeeds with `make -j2`.
- WinAPI integration regression passes with IR and `BABYC_NO_IR=1` (`WinAPI bridge: ok`).
- BabyPOSIX regression tests pass (12 tests).
- Full interpreter corpus: 923/923 IR, 923/923 NO_IR, and 923/923 IR with shadow disabled.
- Native Windows/MSVC or MinGW compilation remains unverified in this environment.

## Outstanding items

- `CreateThread` still requires a safe trampoline between interpreted BabyC callbacks and native C thread entry points; it remains unregistered.
- Linux named events / `OpenEvent` remain unsupported.
- GUI/window-management names remain explicit unsupported stubs on Linux.
- Continue auditing file metadata and path APIs before exposing additional names.
