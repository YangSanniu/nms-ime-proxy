@echo off
setlocal
cd /d "%~dp0"

set "VSW=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="
if exist "%VSW%" (
    for /f "usebackq delims=" %%i in (`"%VSW%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
)
if not defined VSPATH (
    echo MSVC not found. Install Visual Studio 2022 Build Tools with the "Desktop development with C++" workload.
    exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

if not exist dist mkdir dist
del /q "dist\steam_api64.dll" "dist\nms-ime-panel.exe" 2>nul

cl /nologo /O2 /LD /MT /EHsc /W3 /std:c++17 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS steam_api64_proxy.cpp /Fe:dist\steam_api64.dll /Fo:dist\proxy.obj /link /IMPLIB:dist\steam_api64.lib user32.lib
if errorlevel 1 exit /b 1
del /q "dist\proxy.obj" "dist\steam_api64.lib" "dist\steam_api64.exp" "steam_api64.lib" "steam_api64.exp" 2>nul

set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
"%CSC%" -nologo -codepage:65001 -target:winexe -out:dist\nms-ime-panel.exe -reference:System.dll -reference:System.Drawing.dll -reference:System.Windows.Forms.dll panel\panel.cs
if errorlevel 1 exit /b 1

copy /y nms-ime-proxy.ini dist\ >nul
copy /y install.ps1 dist\ >nul
copy /y README.md dist\ >nul
copy /y README.en.md dist\ >nul
copy /y LICENSE dist\ >nul

dumpbin /nologo /exports dist\steam_api64.dll | findstr /C:"SteamAPI_" /C:"SteamInternal_"