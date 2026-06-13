# Launch the HL2 build (build_hl2_x64) against retail Half-Life 2 content.
# Mirrors launch_tf2_steam_rdc.ps1 minus the TF-specific Steam inventory
# machinery (HL2 singleplayer runs fine on the built steam_api stub).
#
# -Map ""        -> boots to the main menu
# -RenderDoc     -> wrap in renderdoccmd for captures
#
# ASCII only (PS 5.1 reads BOM-less UTF-8 as ANSI).

param(
    [string] $Map = "",
    [string] $Backend = "shaderapidx11",
    [int] $Width = 1920,
    [int] $Height = 1080,
    [switch] $RenderDoc,
    [string] $CaptureTemplate = "C:\Github\source-engine\captures\renderdoc\hl2_dx11",
    [string[]] $ExtraArgs = @()
)

$ErrorActionPreference = "Stop"
$Root = "C:\Github\source-engine"
$RenderDocCmd = "C:\Github\renderdoc\x64\Release\renderdoccmd.exe"

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$Root\scripts\configure_hl2_content.ps1"
if ($LASTEXITCODE -ne 0) { Write-Error "configure_hl2_content failed"; exit 1 }

$BuildRoot = "$Root\build_hl2_x64"
$Launcher = "$BuildRoot\hl2_launcher.exe"
if (-not (Test-Path $Launcher)) {
    # waf may install the launcher under a different name; fall back to hl2.exe
    $Launcher = "$BuildRoot\hl2.exe"
}
if (-not (Test-Path $Launcher)) {
    Write-Error "No launcher found in $BuildRoot (hl2_launcher.exe / hl2.exe)"
    exit 1
}

if ((Get-Process hl2_launcher, hl2 -ErrorAction SilentlyContinue)) {
    Write-Error "an hl2 launcher is already running; close it first."
    exit 1
}

$env:SteamAppId = "220"
$env:SteamGameId = "220"

Set-Location $BuildRoot

$GameArgs = @(
    "-game", "hl2", "-insecure",
    "-nojoy", "-novid", "-condebug", "-nocrashdialog", "-nobreakpad",
    "-windowed", "-w", "$Width", "-h", "$Height",
    "-devautomation",
    "-shaderapi", $Backend
)
if ($Map) { $GameArgs += @("+map", $Map) }
$GameArgs += $ExtraArgs

if ($RenderDoc) {
    & $RenderDocCmd capture -d $BuildRoot -c $CaptureTemplate $Launcher @GameArgs
} else {
    & $Launcher @GameArgs
}
