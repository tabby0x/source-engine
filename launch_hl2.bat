@echo off
setlocal

set "ROOT=%~dp0"
set "CONFIGURE=%ROOT%scripts\configure_hl2_content.ps1"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CONFIGURE%"
if errorlevel 1 exit /b 1

call "%ROOT%.hl2runtime.bat"

set "HL2_EXE=%HL2_BUILD_ROOT%\hl2_launcher.exe"

if not exist "%HL2_EXE%" (
    echo Could not find "%HL2_EXE%".
    echo Build HL2 first: waf configure with --build-games=hl2, then build + install.
    exit /b 1
)

tasklist /fi "imagename eq hl2_launcher.exe" | find /i "hl2_launcher.exe" >nul
if not errorlevel 1 (
    echo hl2_launcher.exe is already running - TF2 and HL2 share the exe name.
    echo Close the running instance first.
    exit /b 1
)

rem HL2 singleplayer runs on the built steam_api stub: no -steam flag and none
rem of TF2's real-Steam-API inventory machinery. No -nobreakpad either - with
rem no Steam crash handler in the way, breakpad minidumps are worth keeping.
set "SteamAppId=220"
set "SteamGameId=220"

rem DX11 backend by default - every HL2 session here is migration work.
rem Override with:  set HL2_SHADERAPI=shaderapidx9  before calling this bat.
if not defined HL2_SHADERAPI set "HL2_SHADERAPI=shaderapidx11"

rem Extra args pass through, e.g.:  launch_hl2.bat +map d1_trainstation_01
pushd "%HL2_BUILD_ROOT%"
start /wait "Half-Life 2" "%HL2_EXE%" -game hl2 -insecure -nojoy -novid -condebug -nocrashdialog -windowed -w 1920 -h 1080 -devautomation -shaderapi %HL2_SHADERAPI% %*
set "HL2_EXIT=%ERRORLEVEL%"
popd

exit /b %HL2_EXIT%
