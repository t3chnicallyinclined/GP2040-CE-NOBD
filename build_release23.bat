@echo off
REM Full nobd-23 release build: every board that nobd-22 shipped (7 RP2040 + the
REM RP2350 W6100 board). Mirrors build_nobd.bat's per-board pattern.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
cd /d C:\Users\trist\projects\GP2040-CE
set PICO_SDK_PATH=C:\Users\trist\projects\GP2040-CE\build\_deps\pico_sdk-src
if not exist release mkdir release

call :build RP2040AdvancedBreakoutBoard pico
call :build Pico pico
call :build PicoW pico_w
call :build Haute42COSMOX pico
call :build Haute42COSMOXMLite pico
call :build Haute42COSMOXMUltra pico
call :build WaveshareZero pico

REM RP2350 board (W6100 EVB Pico2) — has the W6100, unaffected by the fix, rebuilt for version parity.
echo === Building W6100EVBPico2 (RP2350) ===
set GP2040_BOARDCONFIG=W6100EVBPico2
set PICO_BOARD=pico2
del build\CMakeCache.txt >nul 2>&1
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGP2040_BOARDCONFIG=W6100EVBPico2 -DPICO_BOARD=pico2 -DPICO_PLATFORM=rp2350-arm-s -DSKIP_WEBBUILD=on -DFETCHCONTENT_FULLY_DISCONNECTED=on 2>&1
cmake --build build --config Release --parallel 2>&1
for %%f in (build\GP2040-CE-NOBD_*_W6100EVBPico2.uf2) do copy "%%f" release\ >nul 2>&1
echo   Done: W6100EVBPico2

echo.
echo === nobd-23 release files ===
dir /b release\GP2040-CE-NOBD_0.7.12-nobd-23_*.uf2
goto :eof

:build
echo === Building %~1 (%~2) ===
set GP2040_BOARDCONFIG=%~1
set PICO_BOARD=%~2
del build\CMakeCache.txt >nul 2>&1
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGP2040_BOARDCONFIG=%~1 -DPICO_BOARD=%~2 -DSKIP_WEBBUILD=on -DFETCHCONTENT_FULLY_DISCONNECTED=on 2>&1
cmake --build build --config Release --parallel 2>&1
for %%f in (build\GP2040-CE-NOBD_*_%~1.uf2) do copy "%%f" release\ >nul 2>&1
echo   Done: %~1
goto :eof
