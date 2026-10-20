# BabyC delta — suite 622/0 + C99 snprintf (Windows)

## Root cause (656 on Windows)
MSVC `_snprintf` returns -1 on truncation and may omit NUL → `n=-1 buf=abcde`.

## Fix (cstdlib/stdio.c)
- StdioOutPutc / StdioOutPuts: always count would-be length (C99); no spin when full
- StdioFprintfWord/Long/FP/Pointer: format to temp via sprintf, emit via OutPuts
  (no host _snprintf for bounded buffers)

## Result
snprintf(buf, 5, "%s", "abcdefgh") → n=8, buf="abcd\0"

## Suite
passed=622 failed=0 skipped=20
Tests unchanged.
