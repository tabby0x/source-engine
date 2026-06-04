param(
    [ValidateSet("x64", "x86")]
    [string] $Arch = "x64",

    [string] $Prefix = "build_hl2",

    [string] $OutDir = "build",

    [string] $BuildGames = "hl2",

    [ValidateSet("debug", "release", "none")]
    [string] $BuildType = "release",

    [switch] $Dedicated,
    [switch] $Tests,
    [switch] $DebugEngine,
    [int] $Jobs = 0,
    [switch] $ConfigureOnly,
    [switch] $InstallOnly
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"

if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe was not found. Install Visual Studio with the C++ desktop workload."
}

$vsInstall = & $vswhere -version '[16.0,17.0)' -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) {
    $vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}

if (-not $vsInstall) {
    throw "No Visual Studio install with MSVC x86/x64 tools was found."
}

$devCmd = Join-Path $vsInstall "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $devCmd)) {
    throw "VsDevCmd.bat was not found at $devCmd."
}

$python = Get-Command py.exe -ErrorAction SilentlyContinue
if (-not $python) {
    throw "py.exe was not found. Install Python 3 and the Python launcher for Windows."
}

if ($Jobs -le 0) {
    $Jobs = [int]$env:NUMBER_OF_PROCESSORS
    if ($Jobs -le 0) {
        $Jobs = 16
    }
}

$wafArgs = @(
    "--out=$OutDir",
    "-T", $BuildType,
    "--disable-warns",
    "--prefix=$Prefix",
    "--build-games=$BuildGames"
)

if ($Arch -eq "x86") {
    $wafArgs += "--32bits"
}
if ($Dedicated) {
    $wafArgs += "-d"
}
if ($Tests) {
    $wafArgs += "--tests"
}
if ($DebugEngine) {
    $wafArgs += "--debug-engine"
}

$quotedArgs = $wafArgs | ForEach-Object {
    if ($_ -match '\s') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ }
}

$steps = @()
if (-not $InstallOnly) {
    $steps += "py -3 -x waf configure $($quotedArgs -join ' ')"
}
if (-not $ConfigureOnly) {
    $steps += "py -3 -x waf -j$Jobs install"
}

$cmd = @(
    "`"$devCmd`" -arch=$Arch -host_arch=x64",
    "cd /d `"$repoRoot`"",
    ($steps -join " && ")
) -join " && "

Write-Host "Using Visual Studio: $vsInstall"
Write-Host "Running: $($steps -join ' && ')"

& cmd.exe /d /s /c $cmd
exit $LASTEXITCODE
