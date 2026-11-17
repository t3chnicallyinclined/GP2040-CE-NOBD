@echo off
REM Haute42 COSMOX M Lite + M Ultra (RP2040). Same as: build_nobd.bat Haute42COSMOXMLite Haute42COSMOXMUltra
call "%~dp0build_nobd.bat" Haute42COSMOXMLite Haute42COSMOXMUltra
exit /b %errorlevel%
