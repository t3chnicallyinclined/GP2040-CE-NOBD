@echo off
REM ============================================================================
REM  Build the NOBD firmware for the RP2040 Advanced Breakout Board.
REM
REM    build.bat            production image (latency instrumentation compiled out)
REM    build.bat selftest   self-test image: measure + the firmware drives its own button
REM                         pins on a fixed schedule; check with tools/selftest/selftest.py
REM    build.bat selftest diag   same, plus stage counters on the right stick and no idle-skip
REM                         (report comes from the loop, not the ISR): locates a dead stage
REM    build.bat nobdhid    NOBD HID personality, forced on (measure image)
REM    build.bat nobdhid selftest   same, plus the self-driving stimulus
REM    build.bat measure    measurement image (TE_LATENCY_MEASURE=ON: on-die probe
REM                         + report edge-time stamp + OLED D/W HUD). See docs/latency-testing/.
REM
REM  Other boards, or several at once: build_nobd.bat (see its header).
REM
REM  Notes:
REM   - Builds the repo this file is in, from any working directory.
REM   - The Pico SDK defaults to <repo>\build\_deps\pico_sdk-src; set PICO_SDK_PATH to use
REM     another one (see build_env.bat). It is passed to CMake explicitly because CMake
REM     ignores the environment variable once the build cache has a value.
REM   - The board MUST be pinned (-DGP2040_BOARDCONFIG) or a reconfigure defaults it to Pico and
REM     the build breaks on a missing BoardConfig.h.
REM ============================================================================
setlocal
call "%~dp0build_env.bat"
if errorlevel 1 exit /b 1
cd /d "%REPO_ROOT%"

set MEASURE=OFF
set SELFTEST=OFF
if /I "%~1"=="measure" set MEASURE=ON
if /I "%~1"=="selftest" set MEASURE=ON
if /I "%~1"=="selftest" set SELFTEST=ON
set DIAG=OFF
if /I "%~2"=="diag" set DIAG=ON
REM NOBD HID: boot straight into the NOBD HID personality regardless of saved config. Mode
REM selection lives in webconfig, which SKIP_WEBBUILD does not rebuild, so a test image needs
REM this to be reachable at all on a stick that already has a mode saved.
set NOBDHID=OFF
if /I "%~1"=="nobdhid" set NOBDHID=ON
if /I "%~1"=="nobdhid" set MEASURE=ON
if /I "%~2"=="selftest" set SELFTEST=ON

set GP2040_BOARDCONFIG=RP2040AdvancedBreakoutBoard

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DGP2040_BOARDCONFIG=RP2040AdvancedBreakoutBoard -DPICO_BOARD=pico ^
  -DPICO_SDK_PATH="%PICO_SDK_PATH%" ^
  -DSKIP_WEBBUILD=on %FETCH_FLAG% ^
  -DTE_LATENCY_MEASURE=%MEASURE% -DTE_SELFTEST=%SELFTEST% -DTE_SELFTEST_DIAG=%DIAG% ^
  -DNOBD_HID_MODE=%NOBDHID%
if errorlevel 1 exit /b 1
cmake --build build --parallel
if errorlevel 1 exit /b 1

echo.
echo === Done  (TE_LATENCY_MEASURE=%MEASURE% TE_SELFTEST=%SELFTEST% NOBD_HID_MODE=%NOBDHID%) ===
for %%f in (build\GP2040-CE-NOBD_*_RP2040AdvancedBreakoutBoard.uf2) do echo   %%f
endlocal
