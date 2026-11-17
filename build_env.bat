@echo off
REM ============================================================================
REM  build_env.bat -- shared setup for the Windows build scripts.
REM  CALL it (call "%~dp0build_env.bat"); never run it on its own. It changes the
REM  caller's environment, and the caller's setlocal discards that when it ends.
REM
REM  Sets:
REM    REPO_ROOT      the folder this file is in (the repo root), no trailing backslash
REM    PICO_SDK_PATH  the pico-sdk 2.3.1 checkout to build against
REM    FETCH_FLAG     -DFETCHCONTENT_FULLY_DISCONNECTED=on once dependencies are already
REM                   downloaded (offline rebuilds); empty on a fresh clone so CMake can fetch
REM  and puts the MSVC x64 host tools on PATH (pioasm and the other SDK host tools are
REM  built with the host compiler; the firmware itself is built with arm-none-eabi-gcc).
REM
REM  Optional overrides:
REM    PICO_SDK_PATH   set in your environment to use an SDK elsewhere (for example one shared
REM                    by several checkouts). Default: <repo>\build\_deps\pico_sdk-src,
REM                    where CMake's FetchContent puts the SDK.
REM    NOBD_VCVARSALL  full path to vcvarsall.bat, to skip the Visual Studio search.
REM ============================================================================

set "REPO_ROOT=%~dp0"
if "%REPO_ROOT:~-1%"=="\" set "REPO_ROOT=%REPO_ROOT:~0,-1%"

REM ---- pico-sdk ---------------------------------------------------------------
if not defined PICO_SDK_PATH set "PICO_SDK_PATH=%REPO_ROOT%\build\_deps\pico_sdk-src"
if exist "%PICO_SDK_PATH%\pico_sdk_init.cmake" goto sdk_found
echo ERROR: no pico-sdk found at "%PICO_SDK_PATH%".
echo   Clone pico-sdk 2.3.1 with its submodules, then point PICO_SDK_PATH at it:
echo     git clone --branch 2.3.1 --recurse-submodules https://github.com/raspberrypi/pico-sdk
echo     set PICO_SDK_PATH=C:\full\path\to\pico-sdk
echo   The SDK's lib\mbedtls submodule must be at v2.28.8 (see CLAUDE.md).
exit /b 1
:sdk_found

REM ---- offline rebuilds -------------------------------------------------------
set "FETCH_FLAG="
if exist "%REPO_ROOT%\build\_deps\arduinojson-src" set "FETCH_FLAG=-DFETCHCONTENT_FULLY_DISCONNECTED=on"

REM ---- MSVC environment -------------------------------------------------------
REM Already in an x64 Developer prompt (or an earlier call): nothing to do.
if /I "%VSCMD_ARG_TGT_ARCH%"=="x64" goto vcvars_done
set "VCVARSALL=%NOBD_VCVARSALL%"
if defined VCVARSALL goto vcvars_run
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto vcvars_missing
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARSALL=%%i\VC\Auxiliary\Build\vcvarsall.bat"
if not defined VCVARSALL goto vcvars_missing
:vcvars_run
call "%VCVARSALL%" x64 >nul 2>&1
REM vcvarsall sets VSCMD_ARG_TGT_ARCH on success; check that, not its exit code.
if /I "%VSCMD_ARG_TGT_ARCH%"=="x64" goto vcvars_done
:vcvars_missing
echo ERROR: could not set up the MSVC x64 build environment.
echo   Install "Build Tools for Visual Studio 2022" with the C++ tools, run from an
echo   "x64 Native Tools Command Prompt", or set NOBD_VCVARSALL to the full path of vcvarsall.bat.
exit /b 1
:vcvars_done
exit /b 0
