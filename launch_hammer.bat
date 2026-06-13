@echo off
setlocal

rem Hammer with the game-config chooser on every start (the retail HL2 hammer
rem behavior): -chooseconfig forces the dialog even when VPROJECT or a single
rem config would otherwise auto-select. Configs live in bin\GameConfig.txt
rem (Half-Life 2 + Team Fortress 2; both trees carry the same file).
rem
rem Launches the HL2 tree's binaries — the TF2 config works from there too
rem (the config's GameDir decides which game content gets mounted), and the
rem HL2 tree carries the custom/tf2_era_shaders DX9 .vcs override that HL2
rem content needs (retail HL2's anniversary shaders are combo-incompatible
rem with this 2017 branch).

set "ROOT=%~dp0"
set "HAMMER_EXE=%ROOT%build_hl2_x64\bin\hammer.exe"

if not exist "%HAMMER_EXE%" (
    echo Could not find "%HAMMER_EXE%".
    echo Build first: the hammer/hammer_launcher projects are part of the win32 waf build.
    exit /b 1
)

start "" "%HAMMER_EXE%" -nop4 -chooseconfig %*

exit /b 0
