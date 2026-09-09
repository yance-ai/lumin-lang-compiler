@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM  Lumin Language Compiler - Windows Build Script
REM  Usage: build.bat [command]
REM ============================================================

cd /d "%~dp0"

REM Windows toolchain paths (adjust if needed)
set "MINGW_PATH=C:\mingw64\bin"
set "GIT_USR_BIN=D:\apps\git\Git\usr\bin"
set "WINFLEXBISON=third_party\windows\tools\winflexbison"

set "BIN_BIN=bin\lumin.exe"

set "PATH=%GIT_USR_BIN%;%MINGW_PATH%;%WINFLEXBISON%;%PATH%"

set "MAKE=mingw32-make"
where %MAKE% >nul 2>&1
if errorlevel 1 set "MAKE=make"

goto :main

:usage
echo.
echo Usage: build.bat [command]
echo.
echo Commands:
echo     check        Check toolchain env (gcc, bison, flex, make)
echo     build        Build compiler binary (default)
echo     compiler     Run compiled binary: compile tests/sample.lm
echo     rebuild      Distclean + full rebuild
echo     clean        Clean build artifacts, keep generated parser
echo     distclean    Full clean, remove flex/bison generated sources
echo     run          Build and run compiler REPL
echo     help         Show this help
echo.
goto :eof

:check
echo ==^> Check toolchain
echo.
echo -- gcc --
gcc --version 2>&1 | findstr /c:"gcc"
if errorlevel 1 echo [FAIL] gcc not found
echo.
echo -- make --
%MAKE% --version 2>&1 | findstr /c:"Make"
if errorlevel 1 echo [FAIL] make not found
echo.
echo -- bison --
bison --version 2>&1 | findstr /c:"bison"
if errorlevel 1 echo [FAIL] bison not found
echo.
echo -- flex --
flex --version 2>&1
if errorlevel 1 echo [FAIL] flex not found
echo.
echo ==^> Toolchain check complete
goto :eof

:build
echo ==^> Build native host: %BIN_BIN%
%MAKE% clean
%MAKE% distclean
%MAKE% CC=gcc all
if errorlevel 1 (
    echo [FAIL] Build failed
    exit /b 1
)
echo ==^> Build success: %BIN_BIN%
goto :eof

:compiler
echo ==^> Using %BIN_BIN% compile tests/sample.lm
if not exist "%BIN_BIN%" (
    echo [ERROR] %BIN_BIN% missing, run build.bat build first
    exit /b 1
)
"%BIN_BIN%" -c tests/sample.lm
goto :eof

:rebuild
echo ==^> Rebuild native host binary
%MAKE% clean
%MAKE% distclean
%MAKE% CC=gcc all
if errorlevel 1 (
    echo [FAIL] Build failed
    exit /b 1
)
echo ==^> Rebuild success: %BIN_BIN%
goto :eof

:clean
echo ==^> Clean artifacts, keep parser generated files
%MAKE% clean
goto :eof

:distclean
echo ==^> Distclean, remove parser generated files
%MAKE% distclean
goto :eof

:run
echo ==^> Build and run REPL
%MAKE% CC=gcc all
if errorlevel 1 (
    echo [FAIL] Build failed
    exit /b 1
)
"%BIN_BIN%"
goto :eof

:main
if "%~1"=="" goto :build
if /i "%~1"=="check" goto :check
if /i "%~1"=="build" goto :build
if /i "%~1"=="compiler" goto :compiler
if /i "%~1"=="rebuild" goto :rebuild
if /i "%~1"=="clean" goto :clean
if /i "%~1"=="distclean" goto :distclean
if /i "%~1"=="run" goto :run
if /i "%~1"=="help" goto :usage
if /i "%~1"=="--help" goto :usage
if /i "%~1"=="-h" goto :usage

echo Error: unknown command '%~1'
goto :usage
