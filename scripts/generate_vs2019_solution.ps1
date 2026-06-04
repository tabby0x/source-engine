param(
    [string] $SolutionPath = "",
    [int] $Jobs = 30
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not $SolutionPath) {
    $SolutionPath = Join-Path $repoRoot "source-engine-tf2.sln"
}

$projectPath = [System.IO.Path]::ChangeExtension($SolutionPath, ".vcxproj")
$solutionDir = Split-Path -Parent $SolutionPath

function ConvertTo-RelativePath([string] $BasePath, [string] $TargetPath) {
    $baseFull = [System.IO.Path]::GetFullPath($BasePath)
    if (-not $baseFull.EndsWith([System.IO.Path]::DirectorySeparatorChar)) {
        $baseFull += [System.IO.Path]::DirectorySeparatorChar
    }

    $targetFull = [System.IO.Path]::GetFullPath($TargetPath)
    $baseUri = New-Object System.Uri($baseFull)
    $targetUri = New-Object System.Uri($targetFull)
    return [System.Uri]::UnescapeDataString($baseUri.MakeRelativeUri($targetUri).ToString()).Replace("/", "\")
}

$projectRel = ConvertTo-RelativePath $solutionDir $projectPath

$projectGuid = "{7B19531F-5501-466B-B965-1F71F2209D3D}"
$cppProjectTypeGuid = "{BC8A1FFA-BEE3-4634-8014-F334798102B3}"

function Escape-Xml([string] $value) {
    return [System.Security.SecurityElement]::Escape($value)
}

$configs = @(
    @{ Name = "TF2 Release"; BuildConfig = "Release"; Out = "build_tf_x64" },
    @{ Name = "TF2 Development"; BuildConfig = "Development"; Out = "build_tf_x64_dev" },
    @{ Name = "TF2 Debug"; BuildConfig = "Debug"; Out = "build_tf_x64_debug" }
)

$sourceDirs = @(
    "appframework", "common", "engine", "filesystem", "game", "gcsdk",
    "inputsystem", "launcher", "launcher_main", "materialsystem", "particles",
    "public", "serverbrowser", "stub_steam", "tier0", "tier1", "tier2",
    "vgui2", "vguimatsurface", "vstdlib"
)

$sourceRoots = $sourceDirs | ForEach-Object { Join-Path $repoRoot $_ } | Where-Object { Test-Path $_ }
$compileFiles = Get-ChildItem -Path $sourceRoots -Recurse -File -Include *.c,*.cc,*.cpp,*.cxx |
    Where-Object { $_.FullName -notmatch "\\build" } |
    Sort-Object FullName
$includeFiles = Get-ChildItem -Path $sourceRoots -Recurse -File -Include *.h,*.hh,*.hpp,*.hxx,*.inl |
    Where-Object { $_.FullName -notmatch "\\build" } |
    Sort-Object FullName

$projectDirMacro = '$(ProjectDir)'
$configXml = foreach ($cfg in $configs) {
@"
  <ItemGroup Condition="'`$(Configuration)|`$(Platform)'=='$($cfg.Name)|x64'">
    <NMakeBuildCommandLine>powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$($projectDirMacro)scripts\build_tf2.ps1" -Configuration $($cfg.BuildConfig) -Arch x64 -Jobs $Jobs</NMakeBuildCommandLine>
    <NMakeReBuildCommandLine>powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$($projectDirMacro)scripts\build_tf2.ps1" -Configuration $($cfg.BuildConfig) -Arch x64 -Jobs $Jobs</NMakeReBuildCommandLine>
    <NMakeCleanCommandLine>powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "if (Test-Path '$($projectDirMacro)$($cfg.Out)') { Remove-Item -Recurse -Force '$($projectDirMacro)$($cfg.Out)' }"</NMakeCleanCommandLine>
    <NMakeOutput>$($projectDirMacro)$($cfg.Out)\hl2_launcher.exe</NMakeOutput>
    <NMakePreprocessorDefinitions>WIN32;_WIN32;TF_CLIENT_DLL;TF_DLL;CLIENT_DLL;GAME_DLL;SOURCE_ENGINE;%(NMakePreprocessorDefinitions)</NMakePreprocessorDefinitions>
    <NMakeIncludeSearchPath>$($projectDirMacro)public;$($projectDirMacro)game\shared;$($projectDirMacro)game\shared\tf;$($projectDirMacro)game\client;$($projectDirMacro)game\client\tf;$($projectDirMacro)game\server;$($projectDirMacro)game\server\tf;$($projectDirMacro)common;%(NMakeIncludeSearchPath)</NMakeIncludeSearchPath>
  </ItemGroup>
"@
}

$compileXml = $compileFiles | ForEach-Object {
    $rel = ConvertTo-RelativePath $repoRoot $_.FullName
    "    <ClCompile Include=""$(Escape-Xml $rel)"" />"
}

$includeXml = $includeFiles | ForEach-Object {
    $rel = ConvertTo-RelativePath $repoRoot $_.FullName
    "    <ClInclude Include=""$(Escape-Xml $rel)"" />"
}

$projectXml = @"
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" ToolsVersion="15.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="TF2 Release|x64"><Configuration>TF2 Release</Configuration><Platform>x64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="TF2 Development|x64"><Configuration>TF2 Development</Configuration><Platform>x64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="TF2 Debug|x64"><Configuration>TF2 Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <VCProjectVersion>16.0</VCProjectVersion>
    <Keyword>MakeFileProj</Keyword>
    <ProjectGuid>$projectGuid</ProjectGuid>
    <RootNamespace>SourceEngineTF2</RootNamespace>
  </PropertyGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Label="Configuration" Condition="'`$(Configuration)|`$(Platform)'=='TF2 Release|x64'"><ConfigurationType>Makefile</ConfigurationType><PlatformToolset>v142</PlatformToolset></PropertyGroup>
  <PropertyGroup Label="Configuration" Condition="'`$(Configuration)|`$(Platform)'=='TF2 Development|x64'"><ConfigurationType>Makefile</ConfigurationType><PlatformToolset>v142</PlatformToolset></PropertyGroup>
  <PropertyGroup Label="Configuration" Condition="'`$(Configuration)|`$(Platform)'=='TF2 Debug|x64'"><ConfigurationType>Makefile</ConfigurationType><PlatformToolset>v142</PlatformToolset></PropertyGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.props" />
$($configXml -join "`r`n")
  <ItemGroup>
$($compileXml -join "`r`n")
  </ItemGroup>
  <ItemGroup>
$($includeXml -join "`r`n")
  </ItemGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.targets" />
</Project>
"@

Set-Content -Path $projectPath -Value $projectXml -Encoding UTF8

$solution = @"
Microsoft Visual Studio Solution File, Format Version 12.00
# Visual Studio Version 16
VisualStudioVersion = 16.0.33423.256
MinimumVisualStudioVersion = 10.0.40219.1
Project("$cppProjectTypeGuid") = "SourceEngineTF2", "$projectRel", "$projectGuid"
EndProject
Global
	GlobalSection(SolutionConfigurationPlatforms) = preSolution
		TF2 Release|x64 = TF2 Release|x64
		TF2 Development|x64 = TF2 Development|x64
		TF2 Debug|x64 = TF2 Debug|x64
	EndGlobalSection
	GlobalSection(ProjectConfigurationPlatforms) = postSolution
		$projectGuid.TF2 Release|x64.ActiveCfg = TF2 Release|x64
		$projectGuid.TF2 Release|x64.Build.0 = TF2 Release|x64
		$projectGuid.TF2 Development|x64.ActiveCfg = TF2 Development|x64
		$projectGuid.TF2 Development|x64.Build.0 = TF2 Development|x64
		$projectGuid.TF2 Debug|x64.ActiveCfg = TF2 Debug|x64
		$projectGuid.TF2 Debug|x64.Build.0 = TF2 Debug|x64
	EndGlobalSection
	GlobalSection(SolutionProperties) = preSolution
		HideSolutionNode = FALSE
	EndGlobalSection
EndGlobal
"@

Set-Content -Path $SolutionPath -Value $solution -Encoding UTF8

Write-Host "Generated $SolutionPath"
Write-Host "Generated $projectPath"
