# W40 — FP binary32 memory boundary preparation

## Scope

W40 prepares the interpreter's scalar-lvalue boundary for a future distinct `float` type. The runtime currently uses a `double` execution temporary for FP values; therefore a binary32 object cannot be loaded by copying four bytes directly into that temporary, nor stored by copying its first four bytes. Such copies are endian-dependent and do not implement IEEE-754 conversion or rounding.

## Changes

- `VariableValueRefresh()` decodes a binary32 lvalue from `float` storage into the `double` execution temporary when its type metadata says `FPStorageBinary32`.
- `VariableValueCommit()` converts the temporary to `float`, stores exactly `sizeof(float)` bytes, and writes the rounded value back into the execution temporary.
- `VariableAllocValueFromExistingData()` reserves a proxy for binary32 values even if the source address is aligned, and decodes the raw bytes when initializing that proxy.
- Pointer-to-array indexing now obtains row stride through `TypeSize()` rather than reading the derived type's `Sizeof` directly. This keeps the stride calculation on the shared size contract.

## Limitations

This does **not** yet introduce a separate frontend `float` type. `float` and `double` still map to the legacy binary64 type in this baseline, so the binary32 proxy branches are preparatory and are not reached by normal source declarations yet. In particular, W40 does not claim correct `sizeof(float)`, binary32 rounding, or FP aggregate layout.

The next safe step must establish a separate semantic type while ensuring all scalar execution temporaries have adequate storage and that addressable binary32 objects use explicit load/store proxies. Function parameters and return values, pointer arithmetic, arrays, struct members, and all execution modes need differential coverage before enabling it by default.
