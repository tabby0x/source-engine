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
set "ACTIVE_STEAM_DLL=%BIN_DIR%\steam_api.dll"
set "STUB_STEAM_DLL=%BIN_DIR%\steam_api_stub.dll"
set "REAL_STEAM_DLL=%TF2_STEAM_API%"

if not exist "%TF2_EXE%" (
    echo Could not find "%TF2_EXE%".
    echo Build TF2 first with scripts\build_tf2.ps1.
    exit /b 1
)

if not exist "%REAL_STEAM_DLL%" (
    echo Could not find "%REAL_STEAM_DLL%".
    echo Update .tf2local.json or TF2_INSTALL_PATH if TF2 is installed elsewhere.
    exit /b 1
)

tasklist /fi "imagename eq hl2_launcher.exe" | find /i "hl2_launcher.exe" >nul
if not errorlevel 1 (
    echo hl2_launcher.exe is already running. Close the current TF2 instance first.
    exit /b 1
)

if not exist "%STUB_STEAM_DLL%" (
    if exist "%ACTIVE_STEAM_DLL%" (
        copy /y "%ACTIVE_STEAM_DLL%" "%STUB_STEAM_DLL%" >nul
        if errorlevel 1 exit /b 1
    )
)

copy /y "%REAL_STEAM_DLL%" "%ACTIVE_STEAM_DLL%" >nul
if errorlevel 1 exit /b 1

set "SteamAppId=440"
set "SteamGameId=440"
set "TF_READONLY_INVENTORY=1"

pushd "%TF_BUILD_ROOT%"
start /wait "Team Fortress 2 - Steam API" "%TF2_EXE%" -steam -game tf -insecure -tf_readonly_inventory -nojoy -novid -condebug -nobreakpad -nocrashdialog -windowed -w 1920 -h 1080 +cl_mouseenable 1 +cl_mouselook 1 +m_rawinput 1 +joystick 0 +joy_advanced 0 %*
set "TF2_EXIT=%ERRORLEVEL%"
popd

if exist "%STUB_STEAM_DLL%" (
    copy /y "%STUB_STEAM_DLL%" "%ACTIVE_STEAM_DLL%" >nul
)

exit /b %TF2_EXIT%
