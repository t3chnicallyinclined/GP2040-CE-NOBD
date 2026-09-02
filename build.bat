@echo off
REM ============================================================================
REM  Canonical build for gp2040-te (THIS repo).
REM
REM  The legacy build_*.bat scripts build the GP2040-CE *fallback* repo, not this
REM  one -- use THIS script to build gp2040-te.
REM
REM    build.bat            production image (latency instrumentation compiled out)
REM    build.bat measure    measurement image (TE_LATENCY_MEASURE=ON: on-die probe
REM                         + report edge-time stamp + OLED D/W HUD). See docs/latency-testing/.
REM
REM  Notes:
REM   - The Pico SDK lives in the GP2040-CE build tree (shared); adjust PICO_SDK_PATH if it moves.
REM   - The board MUST be pinned (-DGP2040_BOARDCONFIG) or a reconfigure defaults it to Pico and
REM     the build breaks on a missing BoardConfig.h.
REM ============================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
cd /d "%~dp0"

set MEASURE=OFF
if /I "%~1"=="measure" set MEASURE=ON

set PICO_SDK_PATH=C:\Users\trist\projects\GP2040-CE\build\_deps\pico_sdk-src
set GP2040_BOARDCONFIG=RP2040AdvancedBreakoutBoard

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DGP2040_BOARDCONFIG=RP2040AdvancedBreakoutBoard -DPICO_BOARD=pico ^
  -DSKIP_WEBBUILD=on -DFETCHCONTENT_FULLY_DISCONNECTED=on ^
  -DTE_LATENCY_MEASURE=%MEASURE%
if errorlevel 1 exit /b 1
cmake --build build --parallel
if errorlevel 1 exit /b 1

echo.
echo === Done  (TE_LATENCY_MEASURE=%MEASURE%) ===
for %%f in (build\GP2040-CE-NOBD_*_RP2040AdvancedBreakoutBoard.uf2) do echo   %%f
endlocal
