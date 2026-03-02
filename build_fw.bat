@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 > C:\Users\trist\projects\GP2040-CE\build_log.txt 2>&1
cd /d C:\Users\trist\projects\GP2040-CE
echo === CONFIGURE === >> build_log.txt
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGP2040_BOARDCONFIG=RP2040AdvancedBreakoutBoard -DPICO_SDK_FETCH_FROM_GIT=on >> build_log.txt 2>&1
if %errorlevel% neq 0 (
    echo CONFIGURE FAILED >> build_log.txt
    exit /b %errorlevel%
)
echo === BUILD === >> build_log.txt
cmake --build build --config Release --parallel >> build_log.txt 2>&1
if %errorlevel% neq 0 (
    echo BUILD FAILED >> build_log.txt
    exit /b %errorlevel%
)
echo === BUILD COMPLETE === >> build_log.txt
dir build\GP2040-CE_*.uf2 >> build_log.txt 2>&1
