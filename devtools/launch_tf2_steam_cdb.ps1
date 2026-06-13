# Launch TF2 (steam + -insecure, DX11) under cdb for LIVE crash capture.
# Steam's crash handler eats minidumps on -steam launches, so cdb live attach
# is the reliable path: it resumes the game (g), and on an unhandled fault
# prints the exception context + faulting stack, then quits so the wrapper
# script/agent can read the log.
#
# ASCII only (PS 5.1 reads BOM-less UTF-8 as ANSI).

param(
    [string]$Map = "ctf_2fort",
    [string]$Backend = "shaderapidx11",
    [string]$LogPath = "C:\Github\source-engine\build_tf_x64\cdb_live_crash.log",
    [int]$Width = 1280,
    [int]$Height = 720,
    # Match launch_tf2_steam.bat exactly: no mat_queue_mode override (auto ->
    # QUEUED mode 2), no sv_cheats/fps_max, 1920x1080, -nobreakpad, joystick
    # cvars, and NO +map (navigate via the menu like the bat repro).
    [switch]$MatchBat
)

$ErrorActionPreference = "Stop"
$Root = "C:\Github\source-engine"
$Cdb = "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe"

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$Root\scripts\configure_tf2_content.ps1"
if ($LASTEXITCODE -ne 0) { Write-Error "configure_tf2_content failed"; exit 1 }

$BuildRoot = "$Root\build_tf_x64"

if ((Get-Process hl2_launcher -ErrorAction SilentlyContinue)) {
    Write-Error "hl2_launcher.exe is already running; close it first."
    exit 1
}

if (Test-Path $LogPath) { Remove-Item $LogPath -Force }

$env:SteamAppId = "440"
$env:SteamGameId = "440"
$env:TF_READONLY_INVENTORY = "1"

Set-Location $BuildRoot

# g = run; on an unhandled exception the remaining commands execute:
# .ecxr (switch to exception context; may fail at first-chance, kv still
# prints the faulting stack), kv 40, module info, quit.
if ($MatchBat) {
    # First-chance AV breaks with the faulting context current (.ecxr fails
    # there) — dump regs, stack, then the ExecuteDefferredBuild frame's locals
    # and the queued-mesh object so we can see desc + m_pActualMesh's vtable.
    & $Cdb -G -logo $LogPath -c "g; r; kv 20; .frame 1; dv /t /v; ?? desc; ?? *this; q" -o "$BuildRoot\hl2_launcher.exe" `
        -steam -game tf -insecure -tf_readonly_inventory `
        -nojoy -novid -condebug -nobreakpad -nocrashdialog `
        -windowed -w 1920 -h 1080 `
        -devautomation `
        +cl_mouseenable 1 +cl_mouselook 1 +m_rawinput 1 +joystick 0 +joy_advanced 0 `
        -shaderapi $Backend
} else {
    & $Cdb -G -logo $LogPath -c "g; .ecxr; kv 40; lm v m shaderapidx11; q" -o "$BuildRoot\hl2_launcher.exe" `
        -steam -game tf -insecure -tf_readonly_inventory `
        -nojoy -novid -nocrashdialog `
        -windowed -w $Width -h $Height `
        -condebug -devautomation `
        +sv_cheats 1 +fps_max 60 +mat_queue_mode 0 `
        +cl_mouseenable 1 +cl_mouselook 1 +m_rawinput 1 `
        -shaderapi $Backend +map $Map
}

Write-Host "cdb session ended (exit $LASTEXITCODE); log at $LogPath"
