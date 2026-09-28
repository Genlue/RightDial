@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /std:c++20 /utf-8 /O2 /MT /EHsc /W3 /DNDEBUG /DUNICODE /D_UNICODE ^
  /I src /I third_party /I third_party\nanosvg ^
  /Fobin/ /Febin/glass_test.exe ^
  test_glass.cpp src\render.cpp src\glass.cpp src\glass_gpu.cpp src\util.cpp src\config.cpp ^
  /link /SUBSYSTEM:CONSOLE ^
  user32.lib gdi32.lib d2d1.lib dwrite.lib windowscodecs.lib shell32.lib shlwapi.lib advapi32.lib ^
  ole32.lib dwmapi.lib d3d11.lib
