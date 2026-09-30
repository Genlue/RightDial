@echo off
setlocal
cd /d "%~dp0"

rem vcvars64.bat can be blocked by the sandbox (it shells out to reg.exe), so the
rem toolchain environment is set up by hand here.
set "MSVC=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207"
set "SDK=C:\Program Files (x86)\Windows Kits\10"
set "SDKV=10.0.26100.0"

set "PATH=%MSVC%\bin\Hostx64\x64;%PATH%"
set "INCLUDE=%MSVC%\include;%SDK%\Include\%SDKV%\ucrt;%SDK%\Include\%SDKV%\um;%SDK%\Include\%SDKV%\shared;%SDK%\Include\%SDKV%\winrt;%SDK%\Include\%SDKV%\cppwinrt"
set "LIB=%MSVC%\lib\x64;%SDK%\Lib\%SDKV%\ucrt\x64;%SDK%\Lib\%SDKV%\um\x64"

if not exist bin mkdir bin

cl /nologo /std:c++20 /utf-8 /O2 /MT /EHsc /W3 /DNDEBUG /DUNICODE /D_UNICODE ^
  /Fobin/ /Febin/hooklat.exe test_hooklat.cpp ^
  /link /SUBSYSTEM:CONSOLE user32.lib
if errorlevel 1 (
  echo [!] build failed
  exit /b 1
)
echo OK: bin\hooklat.exe
