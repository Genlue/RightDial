@echo off
setlocal
cd /d "%~dp0"

set "WIX=%USERPROFILE%\.dotnet\tools\wix.exe"
if not exist "%WIX%" (
  echo [!] wix.exe not found, install with: dotnet tool install --global wix
  exit /b 1
)

if not exist bin\RightDial.exe (
  echo [!] bin\RightDial.exe missing, run build.cmd first
  exit /b 1
)

"%WIX%" build -arch x64 -culture zh-cn -ext WixToolset.UI.wixext -out bin\RightDial-1.0.3-setup.msi installer\product.wxs
if errorlevel 1 (
  echo [!] msi build failed
  exit /b 1
)
echo OK: bin\RightDial-1.0.3-setup.msi
