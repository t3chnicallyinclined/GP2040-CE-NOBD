@echo off
REM Build the Haute42 COSMOX M Lite + M Ultra NOBD firmware (RP2040). Mirrors
REM build_nobd.bat's per-board pattern (sets the GP2040_BOARDCONFIG env var,
REM deletes the cache once, reconfigures per board).
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
cd /d C:\Users\trist\projects\GP2040-CE

set PICO_SDK_PATH=C:\Users\trist\projects\GP2040-CE\build\_deps\pico_sdk-src
if not exist release mkdir release

echo [1/2] Building Haute42COSMOXMLite...
set GP2040_BOARDCONFIG=Haute42COSMOXMLite
set PICO_BOARD=pico
del build\CMakeCache.txt >nul 2>&1
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGP2040_BOARDCONFIG=Haute42COSMOXMLite -DPICO_BOARD=pico -DSKIP_WEBBUILD=on -DFETCHCONTENT_FULLY_DISCONNECTED=on 2>&1
cmake --build build --config Release --parallel 2>&1
for %%f in (build\GP2040-CE-NOBD_*_Haute42COSMOXMLite.uf2) do copy "%%f" release\ >nul 2>&1
echo   Done: Haute42COSMOXMLite

echo [2/2] Building Haute42COSMOXMUltra...
set GP2040_BOARDCONFIG=Haute42COSMOXMUltra
set PICO_BOARD=pico
del build\CMakeCache.txt >nul 2>&1
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGP2040_BOARDCONFIG=Haute42COSMOXMUltra -DPICO_BOARD=pico -DSKIP_WEBBUILD=on -DFETCHCONTENT_FULLY_DISCONNECTED=on 2>&1
cmake --build build --config Release --parallel 2>&1
for %%f in (build\GP2040-CE-NOBD_*_Haute42COSMOXMUltra.uf2) do copy "%%f" release\ >nul 2>&1
echo   Done: Haute42COSMOXMUltra

echo.
echo === Release files ===
dir /b release\GP2040-CE-NOBD_*_Haute42COSMOXM*.uf2
