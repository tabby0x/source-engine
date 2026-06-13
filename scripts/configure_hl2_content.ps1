# Point the HL2 build at a retail Half-Life 2 install: writes gameinfo.txt
# into <BuildDir>/hl2 mounting the retail VPKs, plus .hl2runtime.bat with the
# resolved paths (same pattern as configure_tf2_content.ps1).
#
# Unlike TF2 there is no real-Steam-API swap: HL2 singleplayer has no GC or
# WebAPI inventory, so the built steam_api stub is sufficient.
#
# ASCII only (PS 5.1 reads BOM-less UTF-8 as ANSI).

param(
    [string] $HL2InstallPath = "",
    [string] $BuildDir = "build_hl2_x64",
    [string] $ConfigFile = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

if (-not $ConfigFile) {
    $ConfigFile = Join-Path $repoRoot ".hl2local.json"
}

$config = $null
if (Test-Path $ConfigFile) {
    $config = Get-Content -Path $ConfigFile -Raw | ConvertFrom-Json
}

if (-not $HL2InstallPath) {
    if ($env:HL2_INSTALL_PATH) {
        $HL2InstallPath = $env:HL2_INSTALL_PATH
    } elseif ($config -and $config.hl2InstallPath) {
        $HL2InstallPath = [string]$config.hl2InstallPath
    } else {
        # Common Steam library locations
        foreach ($candidate in @(
            "D:\SteamLibrary\steamapps\common\Half-Life 2",
            "C:\Program Files (x86)\Steam\steamapps\common\Half-Life 2"
        )) {
            if (Test-Path (Join-Path $candidate "hl2\hl2_misc_dir.vpk")) {
                $HL2InstallPath = $candidate
                break
            }
        }
    }
}

if (-not $HL2InstallPath) {
    throw "HL2 install path is not configured. Set HL2_INSTALL_PATH or create .hl2local.json."
}

$hl2Root = Resolve-Path $HL2InstallPath
$hl2RootNative = $hl2Root.Path
$hl2Dir = Join-Path $hl2RootNative "hl2"
$platformDir = Join-Path $hl2RootNative "platform"

$required = @(
    (Join-Path $hl2Dir "hl2_misc_dir.vpk"),
    (Join-Path $hl2Dir "hl2_textures_dir.vpk"),
    (Join-Path $hl2Dir "hl2_pak_dir.vpk"),
    (Join-Path $platformDir "platform_misc_dir.vpk")
)

foreach ($path in $required) {
    if (-not (Test-Path $path)) {
        throw "HL2 content file was not found: $path"
    }
}

$buildRoot = Join-Path $repoRoot $BuildDir
$gameDir = Join-Path $buildRoot "hl2"
$gameBinDir = Join-Path $gameDir "bin"
$engineBinDir = Join-Path $buildRoot "bin"

New-Item -ItemType Directory -Path $gameDir -Force | Out-Null

$hl2RootGameInfo = $hl2RootNative -replace "\\", "/"

# Retail HL2 gameinfo structure (hl2/gameinfo.txt), with absolute search paths
# into the Steam install. hl2_pak carries the maps/scenes; the dir VPKs the
# rest. The build dir itself is MOD (writable: saves, config, screenshots).
$gameInfo = @"
"GameInfo"
{
	game	"Half-Life 2"
	title	"HALF-LIFE'"
	type	singleplayer_only
	supportsvr	1

	FileSystem
	{
		SteamAppId	220

		SearchPaths
		{
			// Build-dir custom mounts FIRST: custom/tf2_era_shaders carries the
			// TF2-lineage shaders/fxc .vcs set. Retail HL2's anniversary update
			// rebuilt its .vcs with a different combo layout (98304/192 vs our
			// stdshader_dx9's 589824/384 era) — every DX9 combo lookup missed,
			// which blacked out Hammer and the DX9 backend on HL2 content.
			game+mod			"|gameinfo_path|custom/*"
			game+mod+custom_mod	"$hl2RootGameInfo/hl2/custom/*"

			game+mod			"$hl2RootGameInfo/hl2/hl2_textures_dir.vpk"
			game+mod			"$hl2RootGameInfo/hl2/hl2_sound_vo_english_dir.vpk"
			game+mod			"$hl2RootGameInfo/hl2/hl2_sound_misc_dir.vpk"
			game+mod+vgui		"$hl2RootGameInfo/hl2/hl2_misc_dir.vpk"
			game+mod			"$hl2RootGameInfo/hl2/hl2_pak_dir.vpk"
			platform+vgui		"$hl2RootGameInfo/platform/platform_misc_dir.vpk"

			mod+mod_write+default_write_path		|gameinfo_path|.
			game+game_write						|gameinfo_path|.
			gamebin								|gameinfo_path|bin

			game				"$hl2RootGameInfo/hl2"
			platform			"$hl2RootGameInfo/platform"
			game+download		|gameinfo_path|download
		}
	}
}
"@

$gameInfoPath = Join-Path $gameDir "gameinfo.txt"
Set-Content -Path $gameInfoPath -Value $gameInfo -Encoding ASCII

$retailSteamInf = Join-Path $hl2Dir "steam.inf"
if (Test-Path $retailSteamInf) {
    Copy-Item -Path $retailSteamInf -Destination (Join-Path $gameDir "steam.inf") -Force
}

# DX11 shader packs build into the TF tree (devtools/build_shaders_dx11.py
# default out = build_tf_x64/tf) but the HL2 runtime loads them from its own
# game dir. Sync missing/newer packs here so an HL2 launch can never run
# stale shaders after a pack rebuild (the "change didn't take" trap).
$packSrcDir = Join-Path $repoRoot "build_tf_x64\tf\shaders\dx11"
if (Test-Path $packSrcDir) {
    $packDstDir = Join-Path $gameDir "shaders\dx11"
    New-Item -ItemType Directory -Path $packDstDir -Force | Out-Null
    foreach ($pack in (Get-ChildItem -Path $packSrcDir -Filter "*.vcsx")) {
        $dst = Join-Path $packDstDir $pack.Name
        if (-not (Test-Path $dst) -or ($pack.LastWriteTime -gt (Get-Item $dst).LastWriteTime)) {
            Copy-Item -Path $pack.FullName -Destination $dst -Force
            Write-Host "Synced shader pack $($pack.Name)"
        }
    }
}

$runtimeEnv = Join-Path $repoRoot ".hl2runtime.bat"
$envLines = @(
    "@echo off",
    "set ""HL2_INSTALL_PATH=$hl2RootNative""",
    "set ""HL2_BUILD_DIR=$BuildDir""",
    "set ""HL2_BUILD_ROOT=$buildRoot""",
    "set ""HL2_GAME_DIR=$gameDir""",
    "set ""HL2_GAME_BIN_DIR=$gameBinDir""",
    "set ""HL2_ENGINE_BIN_DIR=$engineBinDir"""
)
Set-Content -Path $runtimeEnv -Value $envLines -Encoding ASCII

Write-Host "Wrote $gameInfoPath"
Write-Host "Wrote $runtimeEnv"
