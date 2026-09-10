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

set "BIN_BIN=%~dp0bin\lumyr.exe"
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

for %%f in (tests\*.lm) do (
    set "fullpath=%%f"
    set "base=%%~nxf"
    set "name=%%~nf"

    REM Skip tests not suitable for dual-channel comparison
    set "skip_this=0"
    for %%s in (requests_test.lm crypto_enc_test.lm thread_stress.lm thread_test.lm thread_container_test.lm gc_thread_stress.lm json_type_thread_test.lm cond_timeout_test.lm lock_test.lm lock_test2.lm threadlocal_test.lm gc_return_race.lm gc_promotion_test.lm gc_efficiency_diag.lm mem_leak_test.lm _try_n7to13.lm) do (
        if /i "!base!"=="%%s" set "skip_this=1"
    )

    if "!skip_this!"=="1" (
        set /a SKIP+=1
        echo SKIP   !base!
    ) else (
        REM Unique temp files per test
        set "vmo=%TMP_DIR%\!name!_vm.out"
        set "cmo=%TMP_DIR%\!name!_cc.out"
        set "cbin=%TMP_DIR%\!name!_bin_%RANDOM%"

        del "!vmo!" "!cmo!" "!cbin!.exe" "!cbin!.c" 2>nul

        REM VM interpreter
        "!BIN_BIN!" "!fullpath!" > "!vmo!" 2>&1
        if errorlevel 1 (
            echo VM-FAIL !base!
            type "!vmo!"
            set /a FAIL+=1
        ) else (
            REM Check if VM output is empty
            set "vmo_size=0"
            for %%A in ("!vmo!") do set "vmo_size=%%~zA"
            if "!vmo_size!"=="0" (
                echo VM-EMPTY !base! ^(size=0^)
                set /a FAIL+=1
            ) else (
                REM Compile channel
                "!BIN_BIN!" -c "!fullpath!" -o "!cbin!" 2>nul
                if errorlevel 1 (
                    timeout /t 2 /nobreak >nul
                    set "cbin=%TMP_DIR%\!name!_bin_%RANDOM%"
                    "!BIN_BIN!" -c "!fullpath!" -o "!cbin!" 2>nul
                )
                if errorlevel 1 (
                    echo CC-GEN-FAIL !base!
                    set /a FAIL+=1
                ) else (
                    REM Run compiled binary
                    if exist "!cbin!.exe" (
                        "!cbin!.exe" > "!cmo!" 2>&1
                    ) else if exist "!cbin!" (
                        "!cbin!" > "!cmo!" 2>&1
                    ) else (
                        echo CC-NOBIN !base!
                        set /a FAIL+=1
                    )
                    if errorlevel 1 (
                        echo CC-RUN-FAIL !base!
                        set /a FAIL+=1
                    ) else (
                        REM Compare stdout
                        fc /b "!vmo!" "!cmo!" >nul 2>&1
                        if errorlevel 1 (
                            echo DIFF   !base!
                            copy "!vmo!" "%~dp0_diff_vm_!base!.out" >nul 2>&1
                            copy "!cmo!" "%~dp0_diff_cc_!base!.out" >nul 2>&1
                            set /a FAIL+=1
                        ) else (
                            echo PASS   !base!
                            set /a PASS+=1
                        )
                    )
                )
            )
        )

        REM Cleanup
        del "!vmo!" "!cmo!" 2>nul
        del "%TMP_DIR%\!name!_bin_*.exe" "%TMP_DIR%\!name!_bin_*.c" 2>nul
    )
)

echo.
echo === pass=%PASS% fail=%FAIL% skip=%SKIP% ===

if exist "%TMP_DIR%" rmdir /s /q "%TMP_DIR%" 2>nul

endlocal
exit /b 0
