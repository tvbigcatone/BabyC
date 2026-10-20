@echo off
setlocal EnableDelayedExpansion
REM Args: micro | serie1 | serie2 | all | serie1,serie2
set "SCRIPTDIR=%~dp0"
set "BABYC=%SCRIPTDIR%..\babyc.exe"
if not exist "%BABYC%" set "BABYC=%SCRIPTDIR%..\babyc"
set "BENCHDIR=%SCRIPTDIR%..\..\bench"
if not exist "%BENCHDIR%\run_bench.py" set "BENCHDIR=%SCRIPTDIR%..\bench"
if not exist "%BENCHDIR%\run_bench.py" (
    echo ERROR: bench\run_bench.py not found
    exit /b 1
)
set "PY="
where py >nul 2>&1 && set "PY=py"
if not defined PY where python3 >nul 2>&1 && set "PY=python3"
if not defined PY where python >nul 2>&1 && set "PY=python"
if not defined PY (
    echo ERROR: no python / py launcher on PATH
    exit /b 1
)
echo ============================================================
echo  BENCHMARKS
echo ============================================================
echo  babyc : %BABYC%
echo  bench : %BENCHDIR%
echo  python: %PY%
echo  args  : %*
%PY% -c "import sys; print('  version: '+sys.version.split()[0])" 2>nul
echo.
set "BABYC=%BABYC%"
pushd "%BENCHDIR%"
%PY% run_bench.py %*
set "RC=!errorlevel!"
popd
if not "!RC!"=="0" (
    echo BENCHMARKS failed rc=!RC!
    exit /b !RC!
)
echo BENCHMARKS done.
exit /b 0
