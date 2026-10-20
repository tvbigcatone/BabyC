@echo off
setlocal EnableExtensions

rem Run the BabyPOSIX interpreter tests from a Visual Studio/MSVC build.
rem Optional first argument: full path to the babyc.exe to test.
set "TEST_DIR=%~dp0"
set "PROJECT_ROOT=%~dp0..\.."
if "%~1"=="" (
    set "BABYC_EXE=%PROJECT_ROOT%\src\msvc\babyc\Release\babyc.exe"
) else (
    set "BABYC_EXE=%~1"
)

if not exist "%BABYC_EXE%" (
    echo ERROR: babyc.exe not found: "%BABYC_EXE%"
    echo Build the babyc.vcxproj in Visual Studio or pass the full path as argument.
    exit /b 2
)

pushd "%TEST_DIR%"
set "FAILED=0"
for %%T in (posix\*.c) do call :run_one "%%T"
popd
if not "%FAILED%"=="0" (
    echo.
    echo POSIX regression suite: FAILED
    exit /b 1
)
echo.
echo POSIX regression suite: all tests returned success
exit /b 0

:run_one
 echo.
 echo ==== BabyPOSIX test: %~1 ====
 "%BABYC_EXE%" "%~1"
 if errorlevel 1 (
     echo FAILED: %~1 ^(exit code %errorlevel%^)
     set "FAILED=1"
 )
 exit /b 0
