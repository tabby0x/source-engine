param(
    [ValidateSet("Release", "Development", "Debug")]
    [string] $Configuration = "Release",

    [ValidateSet("x64", "x86")]
    [string] $Arch = "x64",

    [int] $Jobs = 30,
    [switch] $ConfigureOnly,
    [switch] $InstallOnly
)

$ErrorActionPreference = "Stop"

switch ($Configuration) {
    "Release" {
        $buildType = "release"
        $debugEngine = $false
        $defaultOut = "build_tf_x64"
    }
    "Development" {
        $buildType = "release"
        $debugEngine = $true
        $defaultOut = "build_tf_x64_dev"
    }
    "Debug" {
        $buildType = "debug"
        $debugEngine = $true
        $defaultOut = "build_tf_x64_debug"
    }
}

if ($Arch -eq "x86") {
    $defaultOut = $defaultOut -replace "_x64", "_x86"
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$buildScript = Join-Path $PSScriptRoot "build_windows_vs2019.ps1"

$buildArgs = @{
    Arch = $Arch
    OutDir = $defaultOut
    Prefix = $defaultOut
    BuildGames = "tf"
    BuildType = $buildType
    Jobs = $Jobs
}

if ($debugEngine) {
    $buildArgs.DebugEngine = $true
}
if ($ConfigureOnly) {
    $buildArgs.ConfigureOnly = $true
}
if ($InstallOnly) {
    $buildArgs.InstallOnly = $true
}

& $buildScript @buildArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (-not $ConfigureOnly) {
    & (Join-Path $PSScriptRoot "configure_tf2_content.ps1") -BuildDir $defaultOut
}
