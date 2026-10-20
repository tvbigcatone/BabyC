# Delta manifest — libWINAPI FILETIME (2026-10-19, follow-up)

Baseline: `babyc_baseline_2026-10-19_phase2t1.zip`  
Baseline SHA-256: `d4c4ee298112801b1e221463dafa6d0c526cf8cc5dce9614f14a66d8ce7b5e9f`

This is a cumulative overlay. Every payload path starts with `babyc/` and is new or modified relative to that exact baseline. No files were deleted. The manifest does not hash itself to avoid recursive self-reference.

## SHA-256 before → after

| File | Before | After |
|---|---|---|
| `babyc/src/cstdlib_ext/win.c` | `b759984c1d45ed4ead7e9d8633a824f2bf7ceb65da112854c99bfd59a91f3fdc` | `2e476423b3f13ca8f07358c95a51855243351986d051e0c25241cc91cbd99463` |
| `babyc/src/winapi/include/wfiles.h` | `0302d96f7884a13a4d5309df1ba20d803d37bcd317ac966482b16f408bf7fd49` | `6eebcac9beddd730114ac135325b624851c226ae1668a6b0caf543f04e8e231a` |
| `babyc/src/winapi/include/wtime.h` | `2784a0e56b1e26f64612d347c930a1e49485d1e6ae051eb3cd03b09f0f08024d` | `8795f02036cc3a71a02dbed9b617160c6640d5db54e1ffec0ac5bbba69cd9a9f` |
| `babyc/src/winapi/src/wfile_handles.c` | `18dee4a09640c13ccfc3d4179791e427431bf4bdf6386e2d24bb013fee975a0c` | `5e29134f4c824c8a74f3ff43e866fd7289c3fe6a85450bfdc9d2ce5598063b96` |
| `babyc/src/winapi/src/wfiles.c` | `5c620eeaa204af027c3c594e2874434b9cf48e9fb495a6cc6d4cfb4d647df646` | `1a3c1767a813a14b5d7ff4c403e79d219664c5fbbb1ed56cd21eb975f765a1c5` |
| `babyc/src/winapi/src/wprivate.h` | `ee26564cfbe909ef1ec9bcdee88b3d2f344193d3bdb6ea7761a74a7ecf1fee8e` | `b2d4537c826827fdcd7d0dd574331d67f1eb8c56c5671beb433d7200154ab955` |
| `babyc/src/winapi/src/wtime.c` | `5abffe81dce70d465c51bc2bf40acfeb06fecd15fd1c39b3bd3243b5b64a7f17` | `5d015d1ee8931762ec851ff266c9ff58023f2792fd52df346ba381096b3b2006` |
| `babyc/src/winapi/tests/smoke.c` | `4ac577ab90c435b7be4a44b7b15dcc9c736c992a4b06189b74a7337d3a5dcbc3` | `bd30c5943c1a08a55dacaab682a8bf19aa2a0c120303e8f4452616a18d3723c8` |
| `babyc/src/winapi/STATUS.md` | `66bf35b5242e428b4b4e08184e8a02057bbfb621a59960c7705db7203815ab00` | `54bac94a0a17c4212a956e000e0adf5d6c3b18995fe89d8612ab8547e1fcc04f` |
| `babyc/src/tests/windows/winapi_filetime.c` | `(absent)` | `ca6622928f53825f7d69537be386e3462b5088010b8b68b2d6ec625422f66ab6` |

## Contents

- FILETIME epoch/calendar conversion APIs and checked shared `timespec` conversion helper.
- Access/write timestamps in `FindFirstFile`/`FindNextFile`, creation-time proxy policy, and lease-protected `GetFileTime`.
- Guest bindings in native Win32 and libWINAPI branches.
- CTest smoke vectors for conversions, invalid dates, time zones, file-handle errors, and exact `utimensat` timestamp preservation through both first and subsequent enumeration results.
- Additional explicit DST-policy test: under `TZ=Europe/Rome`, summer and winter dates receive the same current bias and both round-trip Local→UTC.
- Non-numbered guest regression `src/tests/windows/winapi_filetime.c`.
- `STATUS.md` documents policies, verification and remaining external MSVC check.

## Verification in this follow-up

- Standard CMake build with `-Werror`: passed; CTest 1/1.
- Full CMake build (`WINAPI_THREADS=ON`, `WINAPI_SHMEM=ON`) with `-Werror`: passed; CTest 1/1.
- Full ASan+UBSan build with leak detection: passed; CTest 1/1.
- Interpreter `make clean && make`: completed; guest FILETIME, CreateThread, exitcode+cs and full-surface tests each returned `rc=0`.
- Numbered matrix: tree ACCEL=0 `1009/1009`; linear STRICT ACCEL=1 `1009/1009`; linear ACCEL=1 with no-tree-fallback gate `1009/1009`, `violations=0`, `tree_body=0`.

## Explicitly unresolved by design

- POSIX `struct stat` has no portable creation/birth-time member; `st_ctim` remains a documented best-effort proxy. Native birth-time support would require per-platform guarded extensions and fallback behavior.
- Native MSVC/Windows SDK compilation is to be verified externally by the user.
