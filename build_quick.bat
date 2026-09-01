@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d C:\Users\trist\projects\GP2040-CE
set PICO_SDK_PATH=C:\Users\trist\projects\GP2040-CE\build\_deps\pico_sdk-src
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGP2040_BOARDCONFIG=RP2040AdvancedBreakoutBoard -DSKIP_WEBBUILD=on -DFETCHCONTENT_FULLY_DISCONNECTED=on
cmake --build build --parallel
echo --- DONE ---
grep "length" build\maple.pio.h
