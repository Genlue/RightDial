@echo off
setlocal
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH set "VSPATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "VCVARS=%VSPATH%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  echo [!] vcvars64.bat not found: "%VCVARS%"
  exit /b 1
)
call "%VCVARS%" >nul

if not exist bin mkdir bin

taskkill /F /IM RightDial.exe >nul 2>&1

rc /nologo /fo bin\app.res src\app.rc
if errorlevel 1 (
  echo [!] rc failed
  exit /b 1
)

cl /nologo /std:c++20 /utf-8 /O1 /MT /EHsc /W3 /DNDEBUG /DUNICODE /D_UNICODE ^
  /I third_party /I third_party\nanosvg ^
  /Fobin/ /Febin/RightDial.exe ^
  src\util.cpp src\config.cpp src\render.cpp src\glass.cpp src\glass_gpu.cpp src\actions.cpp src\hook.cpp src\wheel.cpp src\tray.cpp src\settings.cpp src\main.cpp ^
  bin\app.res ^
  /link /SUBSYSTEM:WINDOWS ^
  user32.lib gdi32.lib d2d1.lib dwrite.lib windowscodecs.lib shell32.lib shlwapi.lib advapi32.lib ^
  ole32.lib comdlg32.lib comctl32.lib dwmapi.lib shcore.lib msimg32.lib uxtheme.lib d3d11.lib
if errorlevel 1 (
  echo [!] build failed
  exit /b 1
)
echo OK: bin\RightDial.exe
