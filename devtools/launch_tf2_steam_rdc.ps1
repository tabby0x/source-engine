# Launch TF2 with the REAL Steam API (loadout/cosmetics available) under the
# renderdoccmd capture wrapper, for DX11 visual testing with real items.
#
# The real steam_api.dll is RESIDENT: configure_tf2_content.ps1 (run below,
# and by every build) copies it over the freshly installed stub whenever the
# sizes differ. No per-launch swapping happens here — the old swap dance was
# fragile (a stale stub silently kills WebAPI auth tickets → no loadouts).
#
# -insecure is ALWAYS passed: VAC must never be active in dev sessions.
# -tf_readonly_inventory + TF_READONLY_INVENTORY=1 keep the inventory safe.

param(
    [string]$CaptureTemplate = "C:\Github\source-engine\captures\renderdoc\tf2_dx11_steam",
    [string]$Map = "",
    [string]$Backend = "shaderapidx11",
    [int]$Width = 1280,
    [int]$Height = 720,
    # Extra launch args appended verbatim, e.g. -ExtraArgs "+dx11_rt_textures","1"
    [string[]]$ExtraArgs = @()
)

$ErrorActionPreference = "Stop"
$Root = "C:\Github\source-engine"
$RenderDocCmd = "C:\Github\renderdoc\x64\Release\renderdoccmd.exe"

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$Root\scripts\configure_tf2_content.ps1"
if ($LASTEXITCODE -ne 0) { Write-Error "configure_tf2_content failed"; exit 1 }

# Pull paths from the generated runtime env (same source as the .bat)
$RealSteamApi = $null
foreach ($line in (Get-Content "$Root\.tf2runtime.bat")) {
    if ($line -match '^set "TF2_STEAM_API=(.+)"$') { $RealSteamApi = $Matches[1] }
}
if (-not $RealSteamApi -or -not (Test-Path $RealSteamApi)) {
    Write-Error "Real steam_api not found (TF2_STEAM_API in .tf2runtime.bat): $RealSteamApi"
    exit 1
}

$BuildRoot = "$Root\build_tf_x64"
$ActiveDll = "$BuildRoot\bin\steam_api.dll"

if ((Get-Process hl2_launcher -ErrorAction SilentlyContinue)) {
    Write-Error "hl2_launcher.exe is already running; close it first."
    exit 1
}

# configure_tf2_content (above) keeps the real Steam API resident - verify.
# (ASCII only in this file: PS 5.1 reads BOM-less UTF-8 as ANSI and garbles
# multi-byte characters, which breaks string parsing.)
if ((Get-Item $ActiveDll).Length -ne (Get-Item $RealSteamApi).Length) {
    Write-Error "bin\steam_api.dll is not the real Steam API (configure should have fixed this - is the file locked?)"
    exit 1
}

$env:SteamAppId = "440"
$env:SteamGameId = "440"
$env:TF_READONLY_INVENTORY = "1"

$LaunchArgs = @(
    "capture", "-d", $BuildRoot, "-c", $CaptureTemplate,
    "$BuildRoot\hl2_launcher.exe",
    "-steam", "-game", "tf", "-insecure", "-tf_readonly_inventory",
    "-nojoy", "-novid", "-nocrashdialog",
    "-windowed", "-w", "$Width", "-h", "$Height",
    "-condebug", "-devautomation",
    "+sv_cheats", "1", "+fps_max", "60", "+mat_queue_mode", "0",
    "+cl_mouseenable", "1", "+cl_mouselook", "1", "+m_rawinput", "1",
    "-shaderapi", $Backend
)
if ($Map) { $LaunchArgs += @("+map", $Map) }
if ($ExtraArgs.Count -gt 0) { $LaunchArgs += $ExtraArgs }

& $RenderDocCmd @LaunchArgs
Write-Host "Launched (renderdoccmd injects and detaches; exit code $LASTEXITCODE is normal)."
