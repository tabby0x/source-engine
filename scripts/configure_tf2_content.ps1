param(
    [string] $TF2InstallPath = "",
    [string] $BuildDir = "",
    [string] $ConfigFile = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

if (-not $ConfigFile) {
    $ConfigFile = Join-Path $repoRoot ".tf2local.json"
}

$config = $null
if (Test-Path $ConfigFile) {
    $config = Get-Content -Path $ConfigFile -Raw | ConvertFrom-Json
}

if (-not $TF2InstallPath) {
    if ($env:TF2_INSTALL_PATH) {
        $TF2InstallPath = $env:TF2_INSTALL_PATH
    } elseif ($config -and $config.tf2InstallPath) {
        $TF2InstallPath = [string]$config.tf2InstallPath
    }
}

if (-not $BuildDir) {
    if ($env:TF_BUILD_DIR) {
        $BuildDir = $env:TF_BUILD_DIR
    } elseif ($config -and $config.buildDir) {
        $BuildDir = [string]$config.buildDir
    } else {
        $BuildDir = "build_tf_x64"
    }
}

if (-not $TF2InstallPath) {
    throw "TF2 install path is not configured. Copy .tf2local.example.json to .tf2local.json or set TF2_INSTALL_PATH."
}

$tf2Root = Resolve-Path $TF2InstallPath
$tf2RootNative = $tf2Root.Path
$tfDir = Join-Path $tf2RootNative "tf"
$hl2Dir = Join-Path $tf2RootNative "hl2"
$platformDir = Join-Path $tf2RootNative "platform"
$steamApi64 = Join-Path $tf2RootNative "bin\x64\steam_api64.dll"

$required = @(
    (Join-Path $tfDir "tf2_misc_dir.vpk"),
    (Join-Path $tfDir "tf2_textures_dir.vpk"),
    (Join-Path $hl2Dir "hl2_misc_dir.vpk"),
    (Join-Path $platformDir "platform_misc_dir.vpk")
)

foreach ($path in $required) {
    if (-not (Test-Path $path)) {
        throw "TF2 content file was not found: $path"
    }
}

$buildRoot = Join-Path $repoRoot $BuildDir
$gameDir = Join-Path $buildRoot "tf"
$gameBinDir = Join-Path $gameDir "bin"
$engineBinDir = Join-Path $buildRoot "bin"

New-Item -ItemType Directory -Path $gameDir -Force | Out-Null

$tf2RootGameInfo = $tf2RootNative -replace "\\", "/"

$gameInfo = @"
"GameInfo"
{
	game	"Team Fortress 2"
	type multiplayer_only
	nomodels 1
	nohimodel 1
	nocrosshair 0
	nodegraph 0
	GameData "tf.fgd"
	InstancePath "maps/instances/"
	advcrosshair 1
	supportsvr 1

	FileSystem
	{
		SteamAppId 440

		SearchPaths
		{
			game+mod+custom_mod	"$tf2RootGameInfo/tf/custom/*"

			game+mod			"$tf2RootGameInfo/tf/tf2_textures_dir.vpk"
			game+mod			"$tf2RootGameInfo/tf/tf2_sound_vo_english_dir.vpk"
			game+mod			"$tf2RootGameInfo/tf/tf2_sound_misc_dir.vpk"
			game+mod+vgui		"$tf2RootGameInfo/tf/tf2_misc_dir.vpk"
			game				"$tf2RootGameInfo/hl2/hl2_textures_dir.vpk"
			game				"$tf2RootGameInfo/hl2/hl2_sound_vo_english_dir.vpk"
			game				"$tf2RootGameInfo/hl2/hl2_sound_misc_dir.vpk"
			game+vgui			"$tf2RootGameInfo/hl2/hl2_misc_dir.vpk"
			platform+vgui		"$tf2RootGameInfo/platform/platform_misc_dir.vpk"

			mod+mod_write+default_write_path		|gameinfo_path|.
			game+game_write						|gameinfo_path|.
			gamebin								|gameinfo_path|bin

			game				"$tf2RootGameInfo/tf"
			game				"$tf2RootGameInfo/hl2"
			platform			"$tf2RootGameInfo/platform"
			game+download		|gameinfo_path|download
		}
	}

	ToolsEnvironment
	{
		"Engine"			"Source"
		"UseVPLATFORM"		"1"
		"PythonVersion"		"2.7"
		"PythonHomeDisable"	"1"
	}
}
"@

$gameInfoPath = Join-Path $gameDir "gameinfo.txt"
Set-Content -Path $gameInfoPath -Value $gameInfo -Encoding ASCII

$retailSteamInf = Join-Path $tfDir "steam.inf"
if (Test-Path $retailSteamInf) {
    Copy-Item -Path $retailSteamInf -Destination (Join-Path $gameDir "steam.inf") -Force
}

$runtimeEnv = Join-Path $repoRoot ".tf2runtime.bat"
$envLines = @(
    "@echo off",
    "set ""TF2_INSTALL_PATH=$tf2RootNative""",
    "set ""TF_BUILD_DIR=$BuildDir""",
    "set ""TF_BUILD_ROOT=$buildRoot""",
    "set ""TF_GAME_DIR=$gameDir""",
    "set ""TF_GAME_BIN_DIR=$gameBinDir""",
    "set ""TF_ENGINE_BIN_DIR=$engineBinDir""",
    "set ""TF2_STEAM_API=$steamApi64"""
)
Set-Content -Path $runtimeEnv -Value $envLines -Encoding ASCII

Write-Host "Wrote $gameInfoPath"
Write-Host "Wrote $runtimeEnv"
