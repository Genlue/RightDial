@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /std:c++20 /utf-8 /O2 /MT /EHsc /W3 /DNDEBUG /DUNICODE /D_UNICODE ^
  /I src /I third_party ^
  /Fobin/ /Febin/lockchord_probe.exe ^
  test_lockchord.cpp src\actions.cpp src\config.cpp src\util.cpp ^
  /link /SUBSYSTEM:CONSOLE user32.lib shell32.lib shlwapi.lib ole32.lib advapi32.lib dwmapi.lib
if errorlevel 1 exit /b 1
bin\lockchord_probe.exe
