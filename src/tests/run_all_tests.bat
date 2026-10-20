@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM =============================================================================
REM  run_all_tests.bat  -  babyc test suite (Windows, pure cmd, no PowerShell)
REM
REM  Dual pass:
REM    Pass A - Body-IR enabled (default) + IR miss counters (BABYC_IR_STATS=1)
REM    Pass B - BABYC_NO_IR=1 (pure token path)
REM  Then: bench/run_bench.py if present
REM
REM  Tests are run in NUMERIC order of the leading number in the filename.
REM
REM  run_all_tests.bat
REM  run_all_tests.bat path\to\babyc.exe
REM  run_all_tests.bat path\to\babyc.exe path\to\tests
REM =============================================================================

set "BABYC=%~1"
set "TESTDIR=%~2"

if "%BABYC%"=="" set "BABYC=%~dp0..\babyc.exe"
if "%TESTDIR%"=="" set "TESTDIR=%~dp0"
if not "%TESTDIR:~-1%"=="\" set "TESTDIR=%TESTDIR%\"

if not exist "%BABYC%" (
    echo ERROR: babyc not found: "%BABYC%"
    echo Usage: run_all_tests.bat [babyc.exe] [tests_dir]
    exit /b 1
)

if not exist "%TESTDIR%001_assignment.c" (
    if exist "%TESTDIR%tests\001_assignment.c" set "TESTDIR=%TESTDIR%tests\"
)
if not exist "%TESTDIR%001_assignment.c" (
    echo ERROR: tests not found in "%TESTDIR%"
    exit /b 1
)

echo ============================================================
echo  babyc test suite  (dual pass: Body-IR on / BABYC_NO_IR=1)
echo  babyc : %BABYC%
echo  tests : %TESTDIR%
echo ============================================================
echo.

set /a TOTAL_FAILED=0

REM ----- Pass A: Body-IR (default) -----
set "BABYC_NO_IR="
set "BABYC_IR_STATS=1"
set /a IR_FUNCS_TOTAL=0
set /a IR_MISS_TOTAL=0
call :RunPass "A-IR"
if errorlevel 1 set /a TOTAL_FAILED+=1
set "BABYC_IR_STATS="

echo.
echo.

REM ----- Pass B: pure token -----
set "BABYC_NO_IR=1"
call :RunPass "B-NO_IR"
if errorlevel 1 set /a TOTAL_FAILED+=1

echo.
echo ============================================================
echo  DUAL-PASS SUMMARY  passes_with_failures=%TOTAL_FAILED%
echo ============================================================
if %TOTAL_FAILED% gtr 0 (
    echo  One or more passes had failures.
) else (
    echo  All passes clean.
)

REM ----- Benchmarks after dual-pass -----
echo.
echo ============================================================
echo  BENCHMARKS
echo ============================================================
REM <repo>/src/tests -> <repo>/bench
set "BENCHDIR=%~dp0..\..\bench"
if not exist "%BENCHDIR%\run_bench.py" set "BENCHDIR=%~dp0..\bench"
if not exist "%BENCHDIR%\run_bench.py" (
    echo  WARN: bench\run_bench.py not found - skip benchmarks
    goto :AfterBench
)
set "BABYC_NO_IR="
echo  babyc : %BABYC%
echo  bench : %BENCHDIR%
echo.
REM Prefer Windows py launcher (defaults to Python 3)
set "PY="
where py >nul 2>&1 && set "PY=py"
if not defined PY where python3 >nul 2>&1 && set "PY=python3"
if not defined PY where python >nul 2>&1 && set "PY=python"
if not defined PY (
    echo  WARN: no python / py launcher on PATH - skip benchmarks
    goto :AfterBench
)
echo  python: %PY%
%PY% -c "import sys; print('  version: '+sys.version.split()[0])" 2>nul
pushd "%BENCHDIR%"
%PY% run_bench.py
set "BENCH_RC=!errorlevel!"
popd
if not "!BENCH_RC!"=="0" (
    echo  BENCHMARKS reported failures (rc=!BENCH_RC!)
    set /a TOTAL_FAILED+=1
) else (
    echo  BENCHMARKS done.
)
:AfterBench

if %TOTAL_FAILED% gtr 0 (
    exit /b 1
)
exit /b 0


:RunPass
set "PASSNAME=%~1"
set /a PASSED=0
set /a FAILED=0
set "FAILED_LIST="

echo ------------------------------------------------------------
echo  PASS %PASSNAME%
if defined BABYC_NO_IR (
    echo  env: BABYC_NO_IR=%BABYC_NO_IR%
) else (
    echo  env: BABYC_NO_IR unset (Body-IR enabled)
)
echo ------------------------------------------------------------
echo.

pushd "%TESTDIR%"

set "LIST=%TEMP%\pc_tests_list.txt"
set "LISTS=%TEMP%\pc_tests_sorted.txt"
if exist "%LIST%" del "%LIST%" >nul 2>&1
if exist "%LISTS%" del "%LISTS%" >nul 2>&1

for %%F in (*.c) do (
    set "BASE=%%~nF"
    if exist "!BASE!.expect" (
        for /f "tokens=1 delims=_" %%N in ("!BASE!") do (
            set "NUM=00000%%N"
            set "NUM=!NUM:~-5!"
            echo !NUM! !BASE!>>"%LIST%"
        )
    )
)

if not exist "%LIST%" (
    echo ERROR: no tests with .expect found
    popd
    exit /b 1
)

sort "%LIST%" /o "%LISTS%"

for /f "usebackq tokens=2" %%B in ("%LISTS%") do (
    call :RunOne "%%B"
)

del "%LIST%" 2>nul
del "%LISTS%" 2>nul

popd

echo.
echo ------------------------------------------------------------
echo  PASS %PASSNAME% SUMMARY  passed=%PASSED%  failed=%FAILED%
if /I "%PASSNAME%"=="A-IR" (
    if not defined IR_FUNCS_TOTAL set /a IR_FUNCS_TOTAL=0
    if not defined IR_MISS_TOTAL set /a IR_MISS_TOTAL=0
    echo  IR body coverage: ir_funcs=!IR_FUNCS_TOTAL!  token_fallback_miss=!IR_MISS_TOTAL!
    set /a IR_TOTAL=IR_FUNCS_TOTAL+IR_MISS_TOTAL
    if !IR_TOTAL! gtr 0 (
        set /a IR_PCT=IR_FUNCS_TOTAL*100/IR_TOTAL
        echo  IR body rate: !IR_PCT!%%  (miss = functions compiled to token path, not Body-IR)
    ) else (
        echo  IR body rate: n/a  (no babyc-ir stats captured)
    )
)
echo ------------------------------------------------------------
if not "%FAILED_LIST%"=="" (
    echo  Failed:
    for %%T in (%FAILED_LIST%) do echo    - %%T
    echo ------------------------------------------------------------
    exit /b 1
)
echo  All tests passed in this pass.
exit /b 0


:RunOne
set "NAME=%~1"
set "OUT=%TEMP%\pc_%NAME%.out"
set "ERR=%TEMP%\pc_%NAME%.err"
set "OUTN=%TEMP%\pc_%NAME%_o.txt"
set "EXPN=%TEMP%\pc_%NAME%_e.txt"
set "DIFF=%TEMP%\pc_%NAME%.diff"

echo|set /p="Test: %NAME% ... "

echo %NAME%| findstr /I "args" >nul
if !errorlevel! == 0 (
    "%BABYC%" "%TESTDIR%%NAME%.c" - arg1 arg2 arg3 arg4 >"%OUT%" 2>"%ERR%"
) else (
    echo %NAME%| findstr /I "analyze" >nul
    if !errorlevel! == 0 (
        "%BABYC%" --analyze "%TESTDIR%%NAME%.c" >"%OUT%" 2>"%ERR%"
    ) else (
        "%BABYC%" "%TESTDIR%%NAME%.c" >"%OUT%" 2>"%ERR%"
    )
)

if exist "%ERR%" (
    for %%A in ("%ERR%") do if %%~zA gtr 0 (
        for /f "usebackq delims=" %%L in ("%ERR%") do (
            echo %%L| findstr /C:"babyc-ir" >nul
            if !errorlevel! == 0 (
                for %%Z in (%%L) do (
                    set "TOK=%%Z"
                    if "!TOK:~0,3!"=="ir=" (
                        set "N=!TOK:~3!"
                        set /a IR_FUNCS_TOTAL+=!N! 2>nul
                    )
                    if "!TOK:~0,6!"=="token=" (
                        set "N=!TOK:~6!"
                        set /a IR_MISS_TOTAL+=!N! 2>nul
                    )
                )
            ) else (
                echo %%L>>"%OUT%"
            )
        )
    )
)
if not exist "%OUT%" type nul >"%OUT%"

set "EXPF=%TESTDIR%%NAME%.expect"
REM Platform-specific expect: Win32 (x86) uses .expect.win32 when present
if /I "%PROCESSOR_ARCHITECTURE%"=="x86" (
    if exist "%TESTDIR%%NAME%.expect.win32" set "EXPF=%TESTDIR%%NAME%.expect.win32"
)
if defined PROCESSOR_ARCHITEW6432 (
    REM 32-bit process on 64-bit OS still reports x86 in PROCESSOR_ARCHITECTURE
    if /I "%PROCESSOR_ARCHITECTURE%"=="x86" if exist "%TESTDIR%%NAME%.expect.win32" set "EXPF=%TESTDIR%%NAME%.expect.win32"
)
call :Norm "%EXPF%" "%EXPN%"
call :Norm "%OUT%" "%OUTN%"

fc /w "%EXPN%" "%OUTN%" >"%DIFF%" 2>&1
if !errorlevel! == 0 (
    echo OK
    set /a PASSED+=1
) else (
    echo FAIL
    set /a FAILED+=1
    set "FAILED_LIST=!FAILED_LIST! %NAME%"
    echo   --- diff ^(%NAME%^) ---
    type "%DIFF%"
    echo   --- end diff ---
)

del "%OUT%" 2>nul
del "%ERR%" 2>nul
del "%OUTN%" 2>nul
del "%EXPN%" 2>nul
del "%DIFF%" 2>nul
goto :eof


:Norm
set "IN=%~1"
set "OU=%~2"
if exist "%OU%" del "%OU%" >nul 2>&1
type nul >"%OU%"
if not exist "%IN%" goto :eof
for %%A in ("%IN%") do if %%~zA==0 goto :eof

for /f "usebackq delims=" %%L in (`findstr /n "^" "%IN%"`) do (
    set "LINE=%%L"
    set "LINE=!LINE:*:=!"
    echo(!LINE!>>"%OU%"
)
goto :eof
