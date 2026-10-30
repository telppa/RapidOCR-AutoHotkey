@echo off
setlocal

rem ---------------------------------------------------------------
rem  Build RapidOCR.dll with MinGW-w64 g++.
rem  Output: ..\RapidOCR\RapidOCR.dll
rem
rem  Size-related flags -- do not remove:
rem    -s                            drop the symbol table, 793 KB down to 308 KB
rem    -ffunction-sections -fdata-sections  plus  -Wl,--gc-sections
rem                                  unreferenced functions and data never reach the DLL
rem    fmt.h                         hand-written number formatting, keeps printf/strtod
rem                                  out.  static libc pulls in about 38 KB of
rem                                  pformat/gdtoa as soon as sprintf is called once
rem  All three together: 793818 to 269824 bytes, minus 66 percent, same speed.
rem
rem  NOTE: keep this file pure ASCII and free of bare parentheses inside
rem  if-blocks.  A .bat with non-ASCII bytes gets mis-parsed by cmd.exe after
rem  a codepage switch, and an unescaped paren closes the block early.
rem ---------------------------------------------------------------

rem Locate g++: PATH first, then common w64devkit locations
set "GXX="
for %%D in (g++.exe) do if exist "%%~$PATH:D" set "GXX=%%~$PATH:D"
if not defined GXX if exist "%~dp0w64devkit\bin\g++.exe" set "GXX=%~dp0w64devkit\bin\g++.exe"
if not defined GXX if exist "%~dp0..\w64devkit\bin\g++.exe" set "GXX=%~dp0..\w64devkit\bin\g++.exe"
if not defined GXX if exist "C:\w64devkit\bin\g++.exe" set "GXX=C:\w64devkit\bin\g++.exe"
if not defined GXX (
  echo [ERROR] g++ not found.
  echo         Install MinGW-w64, w64devkit is recommended, and add its bin
  echo         folder to PATH, or unpack w64devkit into .\w64devkit\ here.
  pause
  exit /b 1
)

echo Compiler: %GXX%
"%GXX%" --version | findstr /r "g++"

"%GXX%" -O2 -std=c++17 -w -shared -ffunction-sections -fdata-sections -I ort_include -o "..\RapidOCR\RapidOCR.dll" api.cpp image.cpp draw.cpp ocr.cpp -static-libgcc -static-libstdc++ -Wl,--gc-sections -s -lole32 -loleaut32 -luuid -lshlwapi -lwindowscodecs -lgdi32

if errorlevel 1 (
  echo.
  echo [ERROR] build failed
  pause
  exit /b 1
)

echo.
for %%F in ("..\RapidOCR\RapidOCR.dll") do echo OK: %%~fF  %%~zF bytes
pause
