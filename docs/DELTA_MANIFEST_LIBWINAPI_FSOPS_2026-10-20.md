# libWINAPI — filesystem mutation and free-space APIs

Date: 2026-10-20

## Scope

This cumulative delta adds the following public APIs to the existing libWINAPI subset:

- `DeleteFile`
- `MoveFile`
- `CreateDirectory`
- `RemoveDirectory`
- `SetFileAttributes`
- `GetDiskFreeSpace`

The declarations are in `src/winapi/include/wfiles.h`; implementations and shared path/error helpers are in `src/winapi/src/wfiles.c`.

## Behavior and constraints

- Path inputs reuse the existing Windows-to-POSIX drive/path conversion.
- `DeleteFile` rejects directories and readonly files; errors are translated to Win32 error codes.
- `MoveFile` refuses a destination observed to exist before rename. The check and rename are not atomic against a competing process; this is a known limitation.
- `CreateDirectory` supports default permissions only. Non-null security attributes are rejected with `ERROR_NOT_SUPPORTED` rather than ignored.
- `RemoveDirectory` calls the POSIX operation directly; it intentionally avoids the legacy `win_rmdir` helper, which treats missing paths as success for compatibility with a different CRT API.
- `SetFileAttributes` projects readonly to POSIX write bits. Hidden is inferred from a dot-prefixed basename and cannot be toggled in-place; attempts to change hidden status are rejected. System/temporary/archive attributes do not map to independent POSIX inode flags.
- `GetDiskFreeSpace` uses `statvfs`, a conventional 512-byte logical sector, and a cluster size rounded up to cover the filesystem fragment size. DWORD cluster counts saturate for very large filesystems.

## Regression coverage

Tests cover readonly toggling, readonly deletion rejection, file and directory rename, no-overwrite destination behavior, missing paths, directory attributes/size, security-attribute rejection, disk free-space results, null outputs, and `GetLastError` behavior.

## Validation

- Standard CMake build with `-Werror`: CTest 1/1.
- ASan + UBSan with leak detection, threads and shared memory enabled: CTest 1/1.
- `-fanalyzer -Werror`, threads and shared memory enabled: CTest 1/1.
- Native MSVC/Windows compilation was not run in this environment.

## Adjacent correctness fix

`GetDiskFreeSpaceEx` now multiplies filesystem block counters by `f_frsize` (allocation fragment size), falling back to `f_bsize` only when `f_frsize` is zero. The smoke test verifies that available bytes do not exceed total free bytes, and total free bytes do not exceed total bytes.
