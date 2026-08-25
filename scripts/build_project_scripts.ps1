[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ProjectPath,
    [string]$SdkRoot,
    [string]$OutputRoot,
    [string]$CMake = 'cmake',
    [ValidateSet('Ninja', 'MinGW Makefiles', 'Unix Makefiles')][string]$Generator = 'Ninja',
    [string]$CxxCompiler,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [ValidateRange(1, 256)][int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount),
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$managedGlueMarker = '# FabGL Studio managed gameplay CMake glue. Schema: 1'
$managedGlueSha256 = '5b962443c08768d73dba995354593aafaff6862db4137dea2271640494f5f21b'
$driverMarker = '# FabGL Studio generated project script driver. Schema: 2'
$sourceDriverMarker = '// FabGL Studio generated project script driver. Schema: 2'
$ownerKind = 'FabGLStudioGameplayBuildDirectory'
$ownerSchema = 1

function Get-FullPath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [string]$BasePath = (Get-Location).Path
    )

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BasePath $Path))
}

function Get-PathComparison {
    if ($env:OS -eq 'Windows_NT') {
        return [System.StringComparison]::OrdinalIgnoreCase
    }
    return [System.StringComparison]::Ordinal
}

function Test-SamePath {
    param(
        [Parameter(Mandatory = $true)][string]$Left,
        [Parameter(Mandatory = $true)][string]$Right
    )

    return (Get-FullPath $Left).Equals((Get-FullPath $Right), (Get-PathComparison))
}

function Assert-PathInside {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root,
        [string]$Description = 'path'
    )

    $fullPath = Get-FullPath $Path
    $fullRoot = (Get-FullPath $Root).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar)
    $prefix = $fullRoot + [System.IO.Path]::DirectorySeparatorChar
    $comparison = Get-PathComparison
    if (-not $fullPath.Equals($fullRoot, $comparison) -and
        -not $fullPath.StartsWith($prefix, $comparison)) {
        throw "$Description escapes its trusted root: $fullPath"
    }
    return $fullPath
}

function Test-PathInside {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root
    )

    $fullPath = Get-FullPath $Path
    $fullRoot = (Get-FullPath $Root).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar)
    $prefix = $fullRoot + [System.IO.Path]::DirectorySeparatorChar
    $comparison = Get-PathComparison
    return $fullPath.Equals($fullRoot, $comparison) -or $fullPath.StartsWith($prefix, $comparison)
}

function Assert-NoReparsePointPath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [string]$Description = 'path'
    )

    $fullPath = Get-FullPath $Path
    $volumeRoot = [System.IO.Path]::GetPathRoot($fullPath)
    if ([string]::IsNullOrWhiteSpace($volumeRoot)) {
        throw "$Description has no filesystem root: $fullPath"
    }
    $relative = $fullPath.Substring($volumeRoot.Length)
    $current = $volumeRoot
    foreach ($segment in $relative.Split([char[]]@('\', '/'),
            [System.StringSplitOptions]::RemoveEmptyEntries)) {
        $current = Join-Path $current $segment
        if (-not (Test-Path -LiteralPath $current)) {
            break
        }
        $item = Get-Item -LiteralPath $current -Force
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Description cannot traverse a symbolic link or reparse point: $current"
        }
    }
    return $fullPath
}

function Assert-NoReparsePointTree {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [string]$Description = 'directory'
    )

    $fullRoot = Assert-NoReparsePointPath -Path $Root -Description $Description
    if (-not (Test-Path -LiteralPath $fullRoot -PathType Container)) {
        throw "$Description was not found: $fullRoot"
    }
    $pending = New-Object 'System.Collections.Generic.Queue[string]'
    $pending.Enqueue($fullRoot)
    while ($pending.Count -gt 0) {
        $directory = $pending.Dequeue()
        foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force)) {
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "$Description contains a symbolic link or reparse point: $($item.FullName)"
            }
            if ($item.PSIsContainer) {
                $pending.Enqueue($item.FullName)
            }
        }
    }
    return $fullRoot
}

function Get-GameplaySources {
    param([Parameter(Mandatory = $true)][string]$ScriptsRoot)

    $fullRoot = Assert-NoReparsePointPath -Path $ScriptsRoot -Description 'Scripts directory'
    if (-not (Test-Path -LiteralPath $fullRoot -PathType Container)) {
        throw "Scripts directory was not found: $fullRoot"
    }

    $sources = New-Object 'System.Collections.Generic.List[string]'
    $pending = New-Object 'System.Collections.Generic.Queue[string]'
    $pending.Enqueue($fullRoot)
    while ($pending.Count -gt 0) {
        $directory = $pending.Dequeue()
        foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force)) {
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Scripts cannot contain a symbolic link or reparse point: $($item.FullName)"
            }
            if ($item.PSIsContainer) {
                $pending.Enqueue($item.FullName)
                continue
            }
            if ($item.Extension -in @('.cc', '.cpp', '.cxx')) {
                $source = Assert-PathInside -Path $item.FullName -Root $fullRoot `
                    -Description 'gameplay source'
                $sources.Add($source)
            }
        }
    }
    $ordered = @($sources | Sort-Object -CaseSensitive)
    if ($ordered.Count -eq 0) {
        throw "No gameplay C++ sources were found under $fullRoot. Run fabgl_project_cli new-script first."
    }
    if ($ordered.Count -gt 4096) {
        throw "Scripts contains more than the supported 4096 C++ source files: $fullRoot"
    }
    return $ordered
}

function Resolve-Executable {
    param(
        [Parameter(Mandatory = $true)][string]$Value,
        [Parameter(Mandatory = $true)][string]$Description,
        [Parameter(Mandatory = $true)][string]$BasePath
    )

    if ([System.IO.Path]::IsPathRooted($Value) -or $Value.Contains('\') -or $Value.Contains('/')) {
        $resolved = Get-FullPath $Value $BasePath
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            throw "$Description was not found: $resolved"
        }
        return $resolved
    }
    $command = Get-Command $Value -CommandType Application -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        throw "$Description was not found on PATH: $Value"
    }
    return $command.Source
}

function Find-SdkRoot {
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [string]$ExplicitRoot
    )

    if ($ExplicitRoot) {
        $candidates = @(Get-FullPath $ExplicitRoot $RepositoryRoot)
    }
    elseif ($env:FABGL_STUDIO_SDK_ROOT) {
        $candidates = @(Get-FullPath $env:FABGL_STUDIO_SDK_ROOT $RepositoryRoot)
    }
    else {
        $candidates = @(
            (Join-Path $RepositoryRoot 'out\install\desktop-release'),
            (Join-Path $RepositoryRoot 'out\install\sdk-smoke'),
            (Join-Path $RepositoryRoot 'out\install\release'),
            (Join-Path $RepositoryRoot 'out\install\runtime-sdk-full'),
            $RepositoryRoot
        )
    }
    foreach ($candidate in $candidates) {
        $fullCandidate = Get-FullPath $candidate $RepositoryRoot
        $config = Join-Path $fullCandidate 'lib\cmake\FabGLStudio\FabGLStudioConfig.cmake'
        if (Test-Path -LiteralPath $config -PathType Leaf) {
            Assert-NoReparsePointPath -Path $config -Description 'FabGL Studio SDK' | Out-Null
            return $fullCandidate
        }
    }
    if ($ExplicitRoot -or $env:FABGL_STUDIO_SDK_ROOT) {
        throw "FabGLStudioConfig.cmake was not found under the selected SDK root: $($candidates[0])"
    }
    throw 'FabGL Studio SDK was not found. Install the SDK or pass -SdkRoot explicitly.'
}

function ConvertTo-CMakePath {
    param([Parameter(Mandatory = $true)][string]$Path)

    return (Get-FullPath $Path).Replace('\', '/').Replace(';', '\;').Replace('"', '\"')
}

function Write-ManagedTextFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Marker,
        [Parameter(Mandatory = $true)][string]$Contents
    )

    Assert-NoReparsePointPath -Path $Path -Description 'generated build file' | Out-Null
    if (Test-Path -LiteralPath $Path) {
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
            throw "Generated build path is not a file: $Path"
        }
        $existing = [System.IO.File]::ReadAllText($Path)
        if (-not $existing.StartsWith($Marker, [System.StringComparison]::Ordinal)) {
            throw "Refusing to replace an unmanaged build file: $Path"
        }
        if ($existing -eq $Contents) {
            return
        }
    }
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Contents, $utf8)
}

function Initialize-OwnedOutput {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$ManifestPath,
        [Parameter(Mandatory = $true)][string]$ProjectGuid
    )

    $ownerPath = Join-Path $Path '.fabglstudio-gameplay-build.json'
    if (Test-Path -LiteralPath $Path) {
        Assert-NoReparsePointTree -Root $Path -Description 'gameplay build output' | Out-Null
        if (-not (Test-Path -LiteralPath $ownerPath -PathType Leaf)) {
            $entries = @(Get-ChildItem -LiteralPath $Path -Force)
            if ($entries.Count -ne 0) {
                throw "Refusing to use a non-empty unowned build directory: $Path"
            }
        }
        else {
            $owner = Get-Content -LiteralPath $ownerPath -Raw | ConvertFrom-Json
            if ($owner.kind -ne $ownerKind -or $owner.schemaVersion -ne $ownerSchema -or
                $owner.projectGuid -ne $ProjectGuid -or
                -not (Test-SamePath -Left ([string]$owner.projectManifest) -Right $ManifestPath)) {
                throw "Gameplay build ownership metadata does not match this project: $ownerPath"
            }
            return
        }
    }
    else {
        New-Item -ItemType Directory -Path $Path | Out-Null
        Assert-NoReparsePointPath -Path $Path -Description 'gameplay build output' | Out-Null
    }

    $owner = [ordered]@{
        kind = $ownerKind
        schemaVersion = $ownerSchema
        projectGuid = $ProjectGuid
        projectManifest = $ManifestPath
    }
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText(
        $ownerPath, ($owner | ConvertTo-Json -Depth 3) + [Environment]::NewLine, $utf8)
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$ProjectPath = Get-FullPath $ProjectPath $repositoryRoot
Assert-NoReparsePointPath -Path $ProjectPath -Description 'project manifest' | Out-Null
if (-not $ProjectPath.EndsWith('.fglproject', [System.StringComparison]::OrdinalIgnoreCase) -or
    -not (Test-Path -LiteralPath $ProjectPath -PathType Leaf)) {
    throw "ProjectPath must identify an existing .fglproject file: $ProjectPath"
}

$projectRoot = Split-Path -Parent $ProjectPath
Assert-NoReparsePointPath -Path $projectRoot -Description 'project root' | Out-Null
$manifest = Get-Content -LiteralPath $ProjectPath -Raw | ConvertFrom-Json
if ($manifest.kind -ne 'FabGLStudioProject' -or $manifest.formatVersion -ne 2 -or
    $manifest.projectRoot -ne '.') {
    throw "Unsupported or unsafe FabGL Studio project manifest: $ProjectPath"
}
$projectGuid = [string]$manifest.projectGuid
if ($projectGuid -notmatch '^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$') {
    throw "Project manifest has no canonical projectGuid: $ProjectPath"
}

$scriptsRoot = Join-Path $projectRoot 'Scripts'
$managedGlue = Join-Path $scriptsRoot 'FabGLStudioScripts.cmake'
$sources = @(Get-GameplaySources -ScriptsRoot $scriptsRoot)
if (-not (Test-Path -LiteralPath $managedGlue -PathType Leaf)) {
    throw "Managed gameplay CMake glue was not found: $managedGlue"
}
Assert-NoReparsePointPath -Path $managedGlue -Description 'managed gameplay CMake glue' | Out-Null
$glueText = [System.IO.File]::ReadAllText($managedGlue)
if (-not $glueText.StartsWith($managedGlueMarker, [System.StringComparison]::Ordinal)) {
    throw "Gameplay CMake glue is custom or incompatible; refusing to execute it: $managedGlue"
}
$glueHash = (Get-FileHash -LiteralPath $managedGlue -Algorithm SHA256).Hash.ToLowerInvariant()
if ($glueHash -ne $managedGlueSha256) {
    throw "Gameplay CMake glue is modified or outdated; run fabgl_project_cli new-script to refresh it: $managedGlue"
}

$SdkRoot = Find-SdkRoot -RepositoryRoot $repositoryRoot -ExplicitRoot $SdkRoot
$cmakeExecutable = Resolve-Executable -Value $CMake -Description 'CMake' -BasePath $repositoryRoot
$cmakeVersionOutput = @(& $cmakeExecutable --version 2>&1)
if ($LASTEXITCODE -ne 0 -or ($cmakeVersionOutput -join ' ') -notmatch 'cmake version ([0-9]+)\.([0-9]+)') {
    throw "Unable to determine CMake version from $cmakeExecutable"
}
if ([int]$matches[1] -lt 3 -or ([int]$matches[1] -eq 3 -and [int]$matches[2] -lt 24)) {
    throw 'CMake 3.24 or newer is required for deterministic gameplay script builds.'
}
if ($CxxCompiler) {
    $CxxCompiler = Resolve-Executable -Value $CxxCompiler -Description 'C++ compiler' `
        -BasePath $repositoryRoot
}

if (-not $OutputRoot) {
    $isDeveloperTree =
        (Test-Path -LiteralPath (Join-Path $repositoryRoot '.git')) -and
        (Test-Path -LiteralPath (Join-Path $repositoryRoot 'CMakeLists.txt') -PathType Leaf)
    if ($isDeveloperTree) {
        $buildStorageRoot = Join-Path $repositoryRoot 'out\project-scripts'
    }
    else {
        $localApplicationData = [Environment]::GetFolderPath(
            [Environment+SpecialFolder]::LocalApplicationData)
        if ([string]::IsNullOrWhiteSpace($localApplicationData)) {
            throw 'No writable per-user application-data directory is available; pass -OutputRoot explicitly.'
        }
        $buildStorageRoot = Join-Path $localApplicationData 'FabGLStudio\project-scripts'
    }
    $OutputRoot = Join-Path $buildStorageRoot ($projectGuid.ToLowerInvariant())
}
$OutputRoot = Get-FullPath $OutputRoot $repositoryRoot
$outputVolumeRoot = [System.IO.Path]::GetPathRoot($OutputRoot)
if ((Test-SamePath -Left $OutputRoot -Right $repositoryRoot) -or
    (Test-SamePath -Left $OutputRoot -Right $outputVolumeRoot)) {
    throw 'Gameplay build output cannot be a repository or filesystem root.'
}
if (Test-PathInside -Path $OutputRoot -Root $projectRoot) {
    throw 'Gameplay build output must remain outside the project source tree.'
}
Assert-NoReparsePointPath -Path $OutputRoot -Description 'gameplay build output' | Out-Null

$driverDirectory = Join-Path $OutputRoot 'driver'
$buildDirectory = Join-Path $OutputRoot "build\$($Configuration.ToLowerInvariant())"
$compileCommands = Join-Path $buildDirectory 'compile_commands.json'
$plan = [ordered]@{
    schemaVersion = 1
    kind = 'FabGLStudioGameplayBuildPlan'
    dryRun = [bool]$DryRun
    project = $ProjectPath
    projectGuid = $projectGuid
    sdkRoot = $SdkRoot
    configuration = $Configuration
    generator = $Generator
    cmake = $cmakeExecutable
    cxxCompiler = if ($CxxCompiler) { $CxxCompiler } else { $null }
    sources = $sources
    outputRoot = $OutputRoot
    compileCommands = $compileCommands
    moduleOutput = (Join-Path $buildDirectory 'module')
    hotReloadPerformed = $false
}
if ($DryRun) {
    $plan | ConvertTo-Json -Depth 5
    return
}

Initialize-OwnedOutput -Path $OutputRoot -ManifestPath $ProjectPath -ProjectGuid $projectGuid
New-Item -ItemType Directory -Path $driverDirectory,$buildDirectory -Force | Out-Null
Assert-NoReparsePointPath -Path $driverDirectory -Description 'generated CMake driver' | Out-Null
Assert-NoReparsePointPath -Path $buildDirectory -Description 'gameplay build directory' | Out-Null

$cmakeGluePath = ConvertTo-CMakePath $managedGlue
$driverText = @"
$driverMarker
cmake_minimum_required(VERSION 3.24)

project(FabGLStudioGameplayScriptCheck LANGUAGES CXX)

find_package(FabGLStudio CONFIG REQUIRED)
include("$cmakeGluePath")
fabgl_studio_collect_gameplay_scripts(_fabgl_gameplay_sources)

add_library(fabgl_gameplay_scripts SHARED
    `${_fabgl_gameplay_sources}
    "`${CMAKE_CURRENT_LIST_DIR}/ModuleEntry.cpp")
target_compile_features(fabgl_gameplay_scripts PRIVATE cxx_std_20)
target_link_libraries(fabgl_gameplay_scripts PRIVATE FabGLStudio::Engine)
fabgl_studio_enable_gameplay_warnings(fabgl_gameplay_scripts)
set_target_properties(fabgl_gameplay_scripts PROPERTIES
    PREFIX ""
    OUTPUT_NAME "fabgl_gameplay_scripts"
    RUNTIME_OUTPUT_DIRECTORY "`${CMAKE_BINARY_DIR}/module"
    LIBRARY_OUTPUT_DIRECTORY "`${CMAKE_BINARY_DIR}/module")
"@
$moduleEntryText = @"
$sourceDriverMarker
#include <fabgl/scripting/script_module.h>

FGL_SCRIPT_MODULE_EXPORT bool
fabglStudioGetScriptModuleV1(fabgl::scripting::ScriptModuleView* output) noexcept {
    return fabgl::scripting::detail::exportRegisteredScriptModule(output);
}
"@
Write-ManagedTextFile -Path (Join-Path $driverDirectory 'CMakeLists.txt') `
    -Marker $driverMarker -Contents $driverText
Write-ManagedTextFile -Path (Join-Path $driverDirectory 'ModuleEntry.cpp') `
    -Marker $sourceDriverMarker -Contents $moduleEntryText

$configureArguments = @(
    '--fresh',
    '-S', $driverDirectory,
    '-B', $buildDirectory,
    '-G', $Generator,
    "-DCMAKE_PREFIX_PATH=$SdkRoot",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
    '-DCMAKE_COLOR_DIAGNOSTICS=OFF'
)
if ($CxxCompiler) {
    $configureArguments += "-DCMAKE_CXX_COMPILER=$CxxCompiler"
}

Write-Host "Configuring gameplay scripts for $ProjectPath"
& $cmakeExecutable @configureArguments
$configureExitCode = $LASTEXITCODE
if ($configureExitCode -ne 0) {
    throw "Gameplay script configure failed with exit code $configureExitCode."
}

$buildArguments = @(
    '--build', $buildDirectory,
    '--config', $Configuration,
    '--target', 'fabgl_gameplay_scripts',
    '--parallel', [string]$Jobs
)
Write-Host "Building $($sources.Count) gameplay source file(s) with strict warnings"
& $cmakeExecutable @buildArguments
$buildExitCode = $LASTEXITCODE
if ($buildExitCode -ne 0) {
    throw "Gameplay script build failed with exit code $buildExitCode. Compiler diagnostics above retain their original file:line locations."
}

if (-not (Test-Path -LiteralPath $compileCommands -PathType Leaf)) {
    throw "CMake did not produce compile_commands.json: $compileCommands"
}
$database = Get-Content -LiteralPath $compileCommands -Raw | ConvertFrom-Json
foreach ($source in $sources) {
    $entry = $database | Where-Object {
            $_.file -and (Test-SamePath -Left ([string]$_.file) -Right $source)
        } | Select-Object -First 1
    if ($null -eq $entry) {
        throw "compile_commands.json does not contain gameplay source: $source"
    }
    $commandLine = if ($entry.command) {
        [string]$entry.command
    }
    elseif ($entry.arguments) {
        @($entry.arguments) -join ' '
    }
    else {
        ''
    }
    if ($commandLine -notmatch '(^|\s)(-Werror|/WX)(\s|$)') {
        throw "Gameplay source is not compiled with warnings-as-errors: $source"
    }
}

$moduleExtension = if ($env:OS -eq 'Windows_NT') { '.dll' }
                   elseif ($IsMacOS) { '.dylib' }
                   else { '.so' }
$modulePath = Join-Path $buildDirectory "module/fabgl_gameplay_scripts$moduleExtension"
if (-not (Test-Path -LiteralPath $modulePath -PathType Leaf)) {
    throw "Gameplay module target succeeded but its output is missing: $modulePath"
}
$modulePath = (Resolve-Path -LiteralPath $modulePath).Path

$result = [ordered]@{
    schemaVersion = 2
    kind = 'FabGLStudioGameplayBuildResult'
    success = $true
    project = $ProjectPath
    projectGuid = $projectGuid
    sdkRoot = $SdkRoot
    configuration = $Configuration
    generator = $Generator
    cmake = $cmakeExecutable
    cxxCompiler = if ($CxxCompiler) { $CxxCompiler } else { $null }
    sourceCount = $sources.Count
    sources = $sources
    compileCommands = $compileCommands
    compileCommandsSha256 =
        (Get-FileHash -LiteralPath $compileCommands -Algorithm SHA256).Hash.ToLowerInvariant()
    target = 'fabgl_gameplay_scripts'
    module = $modulePath
    moduleSha256 = (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash.ToLowerInvariant()
    hotReloadPerformed = $false
}
$resultPath = Join-Path $OutputRoot 'build-result.json'
$utf8 = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText(
    $resultPath, ($result | ConvertTo-Json -Depth 5) + [Environment]::NewLine, $utf8)
$result | ConvertTo-Json -Depth 5
