@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM  Dual-channel regression test (Windows version)
REM  Run VM interpreter and compile channel for each tests/*.lm,
REM  compare stdout consistency.
REM  Usage: regress_dual.bat
REM ============================================================

cd /d "%~dp0"

REM Windows toolchain paths
set "MINGW_PATH=C:\mingw64\bin"
set "GIT_USR_BIN=D:\apps\git\Git\usr\bin"
set "PATH=%GIT_USR_BIN%;%MINGW_PATH%;%PATH%"

set "BIN_BIN=bin\lumyr.exe"
set "TMP_DIR=%TEMP%\lumyr_regress"

if not exist "%BIN_BIN%" (
    echo [ERROR] %BIN_BIN% missing, run build.bat build first
    exit /b 1
)

REM Clean and recreate temp dir
if exist "%TMP_DIR%" rmdir /s /q "%TMP_DIR%" 2>nul
mkdir "%TMP_DIR%"

set "PASS=0"
set "FAIL=0"
set "SKIP=0"

echo === Dual-channel regression test ===
echo.

for %%f in (tests\*.lm) do call :runtest "%%f"

echo.
echo === pass=%PASS% fail=%FAIL% skip=%SKIP% ===

if exist "%TMP_DIR%" rmdir /s /q "%TMP_DIR%" 2>nul

endlocal
exit /b 0

:runtest
set "fullpath=%~1"
set "base=%~nx1"
set "name=%~n1"

REM Skip tests not suitable for dual-channel comparison
set "skip_this=0"
for %%s in (requests_test.lm crypto_enc_test.lm thread_stress.lm thread_test.lm thread_container_test.lm gc_thread_stress.lm json_type_thread_test.lm cond_timeout_test.lm lock_test.lm lock_test2.lm threadlocal_test.lm gc_return_race.lm gc_promotion_test.lm gc_efficiency_diag.lm mem_leak_test.lm) do (
    if /i "!base!"=="%%s" set "skip_this=1"
)

if "%skip_this%"=="1" (
    set /a SKIP+=1
    echo SKIP   !base!
    goto :eof
)

REM Unique temp files per test
set "vmo=%TMP_DIR%\%name%_vm.out"
set "cmo=%TMP_DIR%\%name%_cc.out"
set "cbin=%TMP_DIR%\%name%_bin"

REM Clean up previous run
del "%vmo%" "%cmo%" "%cbin%.exe" "%cbin%.c" 2>nul

REM VM interpreter
"%BIN_BIN%" "%fullpath%" 2>nul > "%vmo%"
if errorlevel 1 (
    echo VM-FAIL !base!
    set /a FAIL+=1
    goto :cleanup
)

REM Compile channel - run in project dir so gcc can find includes
"%BIN_BIN%" -c "%fullpath%" -o "%cbin%" 2>nul
if errorlevel 1 (
    echo CC-GEN-FAIL !base!
    set /a FAIL+=1
    goto :cleanup
)

REM Run compiled binary
if exist "%cbin%.exe" (
    "%cbin%.exe" 2>nul > "%cmo%"
) else if exist "%cbin%" (
    "%cbin%" 2>nul > "%cmo%"
) else (
    echo CC-NOBIN !base!
    set /a FAIL+=1
    goto :cleanup
)
if errorlevel 1 (
    echo CC-RUN-FAIL !base!
    set /a FAIL+=1
    goto :cleanup
)

REM Compare stdout
fc /b "%vmo%" "%cmo%" >nul 2>&1
if errorlevel 1 (
    echo DIFF   !base!
    set /a FAIL+=1
) else (
    echo PASS   !base!
    set /a PASS+=1
)

:cleanup
del "%vmo%" "%cmo%" "%cbin%.exe" "%cbin%.c" 2>nul
goto :eof
