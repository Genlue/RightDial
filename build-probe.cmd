@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /std:c++20 /utf-8 /O2 /MT /EHsc /W3 /DNDEBUG /DUNICODE /D_UNICODE ^
  /I src /I third_party /Fobin/ /Febin/accent_probe.exe ^
  test_accent.cpp src\config.cpp src\util.cpp ^
  /link /SUBSYSTEM:CONSOLE ole32.lib advapi32.lib user32.lib shell32.lib shlwapi.lib dwmapi.lib
bin\accent_probe.exe
