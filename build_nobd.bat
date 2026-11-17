@echo off
REM ============================================================================
REM  Release-style build: one UF2 per board, copied into release\.
REM
REM    build_nobd.bat                 RP2040AdvancedBreakoutBoard, Pico, PicoW, Haute42COSMOX
REM    build_nobd.bat Pico PicoW      only the boards named (any folder name under configs\)
REM
REM  The CI release (.github/workflows/release.yml) builds ARBB, Pico, PicoW and WaveshareZero;
REM  to mirror it locally:  build_nobd.bat RP2040AdvancedBreakoutBoard Pico PicoW WaveshareZero
REM  Haute42 M Lite / M Ultra:  build_cosmox.bat
REM
REM  Builds the repo this file is in, from any working directory. SDK and Visual Studio
REM  are located by build_env.bat. PicoW needs PICO_BOARD=pico_w; every other board uses pico.
REM  Pico2 (RP2350) is not built: untested on RP2350 hardware, only RP2040 is released.
REM ============================================================================
setlocal
call "%~dp0build_env.bat"
if errorlevel 1 exit /b 1
cd /d "%REPO_ROOT%"
if not exist release mkdir release

set "BOARDS=%*"
if not defined BOARDS set "BOARDS=RP2040AdvancedBreakoutBoard Pico PicoW Haute42COSMOX"

for %%b in (%BOARDS%) do call :build %%b

echo.
echo === Release files ===
dir /b release\GP2040-CE-NOBD_*.uf2
if defined ANY_FAILED exit /b 1
exit /b 0

:build
set "GP2040_BOARDCONFIG=%~1"
set "PICO_BOARD=pico"
if /I "%~1"=="PicoW" set "PICO_BOARD=pico_w"
echo === Building %GP2040_BOARDCONFIG% (%PICO_BOARD%) ===
REM Fresh cache per board: the board config and PICO_BOARD both feed the toolchain setup.
del build\CMakeCache.txt >nul 2>&1
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGP2040_BOARDCONFIG=%GP2040_BOARDCONFIG% -DPICO_BOARD=%PICO_BOARD% -DPICO_SDK_PATH="%PICO_SDK_PATH%" -DSKIP_WEBBUILD=on %FETCH_FLAG%
if errorlevel 1 goto build_failed
cmake --build build --config Release --parallel
if errorlevel 1 goto build_failed
for %%f in (build\GP2040-CE-NOBD_*_%GP2040_BOARDCONFIG%.uf2) do copy "%%f" release\ >nul
echo   Done: %GP2040_BOARDCONFIG%
exit /b 0

:build_failed
echo   FAILED: %GP2040_BOARDCONFIG%
set ANY_FAILED=1
exit /b 1
