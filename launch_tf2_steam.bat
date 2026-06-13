@echo off
setlocal

set "ROOT=%~dp0"
set "CONFIGURE=%ROOT%scripts\configure_tf2_content.ps1"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CONFIGURE%"
if errorlevel 1 exit /b 1

call "%ROOT%.tf2runtime.bat"

set "GAME_DIR=%TF_GAME_DIR%"
set "BIN_DIR=%TF_ENGINE_BIN_DIR%"
set "TF2_EXE=%TF_BUILD_ROOT%\hl2_launcher.exe"

if not exist "%TF2_EXE%" (
    echo Could not find "%TF2_EXE%".
    echo Build TF2 first with scripts\build_tf2.ps1.
    exit /b 1
)

tasklist /fi "imagename eq hl2_launcher.exe" | find /i "hl2_launcher.exe" >nul
if not errorlevel 1 (
    echo hl2_launcher.exe is already running. Close the current TF2 instance first.
    exit /b 1
)

rem The real Steam API is RESIDENT at bin\steam_api.dll — kept in place by
rem configure_tf2_content.ps1 (already run above) after every build. The old
rem swap-on-launch/restore-on-exit dance is gone: a parked bat restoring the
rem stub after the game exited silently broke WebAPI auth tickets (loadouts).

set "SteamAppId=440"
set "SteamGameId=440"
set "TF_READONLY_INVENTORY=1"

pushd "%TF_BUILD_ROOT%"
start /wait "Team Fortress 2 - Steam API" "%TF2_EXE%" -steam -game tf -insecure -tf_readonly_inventory -nojoy -novid -condebug -nobreakpad -nocrashdialog -windowed -w 1920 -h 1080 +cl_mouseenable 1 +cl_mouselook 1 +m_rawinput 1 +joystick 0 +joy_advanced 0 %*
set "TF2_EXIT=%ERRORLEVEL%"
popd

exit /b %TF2_EXIT%
