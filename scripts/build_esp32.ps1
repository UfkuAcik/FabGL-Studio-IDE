[CmdletBinding()]
param(
    [string]$ManifestPath,
    [string]$SketchDirectory,
    [string]$ProjectPath,
    [string]$ProjectCli,
    [string]$OutputRoot,
    [string]$ArduinoCli,
    [string]$ArduinoConfig,
    [string]$FabglLibrary,
    [ValidateRange(1, 256)][int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount),
    [ValidateSet('Debug', 'Release', 'SizeOptimized', 'PerformanceOptimized')]
    [string]$BuildProfile = 'Release',
    [switch]$UseSystemToolchain,
    [switch]$EnablePsram,
    [switch]$SoakDiagnostics,
    [switch]$Clean,
    [switch]$VerboseBuild,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

if ($SoakDiagnostics -and [string]::IsNullOrWhiteSpace($ProjectPath)) {
    throw '-SoakDiagnostics requires -ProjectPath so scene and asset churn use real project data.'
}

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

function Get-NativeShortPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = Get-FullPath $Path
    if ($env:OS -ne 'Windows_NT') {
        return $fullPath
    }
    if (-not ('FabGLStudio.NativePath' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
namespace FabGLStudio {
    public static class NativePath {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern uint GetShortPathName(
            string longPath, StringBuilder shortPath, uint capacity);
    }
}
'@
    }
    $buffer = New-Object System.Text.StringBuilder 32768
    $length = [FabGLStudio.NativePath]::GetShortPathName(
        $fullPath, $buffer, [uint32]$buffer.Capacity)
    if ($length -eq 0 -or $length -ge $buffer.Capacity) {
        return $fullPath
    }
    return $buffer.ToString()
}

function Convert-ToMappedRepositoryPath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string]$Drive
    )

    $fullPath = Get-FullPath $Path
    $root = (Get-FullPath $RepositoryRoot).TrimEnd('\', '/')
    if ($fullPath.Equals($root, [System.StringComparison]::OrdinalIgnoreCase)) {
        return "$Drive\"
    }
    $prefix = $root + [System.IO.Path]::DirectorySeparatorChar
    if ($fullPath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        return "$Drive\$($fullPath.Substring($prefix.Length))"
    }
    return Get-NativeShortPath $fullPath
}

function Assert-PathWithinRepository {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [string]$Description = 'path'
    )

    $fullPath = Get-FullPath $Path
    $root = (Get-FullPath $RepositoryRoot).TrimEnd([System.IO.Path]::DirectorySeparatorChar,
                                                   [System.IO.Path]::AltDirectorySeparatorChar)
    $prefix = $root + [System.IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.Equals($root, [System.StringComparison]::OrdinalIgnoreCase) -and
        -not $fullPath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description must remain inside the repository: $fullPath"
    }
    return $fullPath
}

function Assert-NoReparsePoints {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$TrustedRoot,
        [string]$Description = 'path'
    )

    $fullPath = Get-FullPath $Path
    $root = (Get-FullPath $TrustedRoot).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar)
    $relative = $fullPath.Substring($root.Length).TrimStart('\', '/')
    $current = $root
    foreach ($segment in $relative.Split([char[]]@('\', '/'),
            [System.StringSplitOptions]::RemoveEmptyEntries)) {
        $current = Join-Path $current $segment
        if (-not (Test-Path -LiteralPath $current)) {
            break
        }
        $item = Get-Item -LiteralPath $current -Force
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Description cannot traverse a filesystem reparse point: $current"
        }
    }
    return $fullPath
}

function Get-LibraryProperties {
    param([Parameter(Mandatory = $true)][string]$LibraryRoot)

    $propertiesPath = Join-Path $LibraryRoot 'library.properties'
    if (-not (Test-Path -LiteralPath $propertiesPath -PathType Leaf)) {
        throw "FabGL library.properties not found: $propertiesPath"
    }
    $properties = @{}
    foreach ($line in Get-Content -LiteralPath $propertiesPath) {
        if ($line -match '^([^#=]+)=(.*)$') {
            $properties[$matches[1].Trim()] = $matches[2].Trim()
        }
    }
    return $properties
}

function Invoke-ArduinoProbe {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [string[]]$GlobalArguments,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $output = @(& $Executable @GlobalArguments @Arguments 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "arduino-cli probe failed: $($Arguments -join ' ')`n$($output -join [Environment]::NewLine)"
    }
    return $output
}

function Get-SafeSketchName {
    param([Parameter(Mandatory = $true)][string]$ProjectManifest)

    $name = [System.IO.Path]::GetFileNameWithoutExtension($ProjectManifest)
    $name = [regex]::Replace($name, '[^A-Za-z0-9_-]', '_')
    if ([string]::IsNullOrWhiteSpace($name)) {
        $name = 'FabGLProject'
    }
    if ($name[0] -eq '-') {
        $name = "Project_$name"
    }
    if ($name.Length -gt 48) {
        $name = $name.Substring(0, 48)
    }
    return $name
}

function Find-ProjectCli {
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [string]$ExplicitPath
    )

    if ($ExplicitPath) {
        return Get-FullPath $ExplicitPath $RepositoryRoot
    }
    $executableName = if ($env:OS -eq 'Windows_NT') {
        'fabgl_project_cli.exe'
    }
    else {
        'fabgl_project_cli'
    }
    $candidates = @(
        (Join-Path $RepositoryRoot "out\build\desktop-release\tools\project_cli\$executableName"),
        (Join-Path $RepositoryRoot "out\build\release\tools\project_cli\$executableName"),
        (Join-Path $RepositoryRoot "out\build\dev\tools\project_cli\$executableName"),
        (Join-Path $RepositoryRoot "out\build\esp32-agent\tools\project_cli\$executableName"),
        (Join-Path $RepositoryRoot "bin\$executableName")
    )
    $available = @($candidates | Where-Object {
            Test-Path -LiteralPath $_ -PathType Leaf
        } | ForEach-Object {
            Get-Item -LiteralPath $_
        } | Sort-Object LastWriteTimeUtc -Descending)
    if ($available.Count -gt 0) {
        return $available[0].FullName
    }
    throw 'fabgl_project_cli was not found. Build the desktop tools or pass -ProjectCli explicitly.'
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $ManifestPath) {
    $ManifestPath = Join-Path $repositoryRoot 'toolchains\manifest.json'
}
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repositoryRoot 'out\esp32'
}

$ManifestPath = Get-FullPath $ManifestPath $repositoryRoot
$OutputRoot = Assert-PathWithinRepository -Path $OutputRoot -RepositoryRoot $repositoryRoot `
    -Description 'ESP32 output root'
if ($OutputRoot.Equals($repositoryRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'ESP32 output root cannot be the repository root.'
}
$OutputRoot = Assert-NoReparsePoints -Path $OutputRoot -TrustedRoot $repositoryRoot `
    -Description 'ESP32 output root'
if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
    throw "Toolchain manifest not found: $ManifestPath"
}

$firmwareTemplateDirectory = Join-Path $repositoryRoot 'platforms\fabgl\firmware'
$projectExport = $null
if ($ProjectPath) {
    if ($PSBoundParameters.ContainsKey('SketchDirectory')) {
        throw '-ProjectPath and -SketchDirectory are mutually exclusive.'
    }
    $ProjectPath = Get-FullPath $ProjectPath $repositoryRoot
    if (-not $ProjectPath.EndsWith('.fglproject',
            [System.StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $ProjectPath -PathType Leaf)) {
        throw "ProjectPath must identify an existing .fglproject file: $ProjectPath"
    }
    $stagingRoot = Assert-PathWithinRepository -Path (Join-Path $OutputRoot 'staged') `
        -RepositoryRoot $repositoryRoot -Description 'ESP32 project staging root'
    $sketchName = Get-SafeSketchName -ProjectManifest $ProjectPath
    $SketchDirectory = Assert-PathWithinRepository -Path (Join-Path $stagingRoot $sketchName) `
        -RepositoryRoot $repositoryRoot -Description 'ESP32 staged sketch directory'
}
else {
    if (-not $SketchDirectory) {
        $SketchDirectory = $firmwareTemplateDirectory
    }
    $SketchDirectory = Get-FullPath $SketchDirectory $repositoryRoot
    $sketchName = Split-Path -Leaf $SketchDirectory
    if (-not (Test-Path -LiteralPath $SketchDirectory -PathType Container)) {
        throw "Sketch directory not found: $SketchDirectory"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $SketchDirectory "$sketchName.ino") -PathType Leaf)) {
        throw "Arduino sketch must contain $sketchName.ino: $SketchDirectory"
    }
}
if (-not (Test-Path -LiteralPath $firmwareTemplateDirectory -PathType Container)) {
    throw "Firmware template directory not found: $firmwareTemplateDirectory"
}

$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1) {
    throw "Unsupported toolchain manifest schema: $($manifest.schemaVersion)"
}
$expectedCore = [string]$manifest.profile.arduinoCore.version
$expectedFabgl = [string]$manifest.profile.fabgl.version
$expectedFabglCommit = [string]$manifest.profile.fabgl.commit
$compilerWarnings = [string]$manifest.profile.compiler.warnings
$compilerCppExtraFlags = [string]$manifest.profile.compiler.cppExtraFlags
if ($compilerWarnings -ne 'default' -or
    $compilerCppExtraFlags -ne '-Wno-error=narrowing') {
    throw 'Unexpected compiler compatibility contract in the toolchain manifest.'
}
$expectedCli = @($manifest.artifacts | Where-Object id -eq 'arduino-cli-windows-x86_64')[0]

$profileOptimizationFlags = $null
$profileDefinition = $null
$profileDebugLevel = 'none'
switch ($BuildProfile) {
    'Debug' {
        $profileOptimizationFlags = '-Og -g3 -fno-omit-frame-pointer'
        $profileDefinition = '-DFABGL_STUDIO_PROFILE_DEBUG=1'
        $profileDebugLevel = 'debug'
    }
    'Release' {
        $profileOptimizationFlags = '-O2 -g1'
        $profileDefinition = '-DNDEBUG -DFABGL_STUDIO_PROFILE_RELEASE=1'
    }
    'SizeOptimized' {
        $profileOptimizationFlags = '-Os -g0 -ffunction-sections -fdata-sections'
        $profileDefinition = '-DNDEBUG -DFABGL_STUDIO_PROFILE_SIZE_OPTIMIZED=1'
    }
    'PerformanceOptimized' {
        $profileOptimizationFlags = '-O3 -g0 -funroll-loops'
        $profileDefinition = '-DNDEBUG -DFABGL_STUDIO_PROFILE_PERFORMANCE_OPTIMIZED=1'
    }
    default {
        throw "Unsupported ESP32 build profile: $BuildProfile"
    }
}
$compilerCppProfileFlags =
    "$compilerCppExtraFlags $profileOptimizationFlags $profileDefinition"
$compilerCProfileFlags = "$profileOptimizationFlags $profileDefinition"
if ($SoakDiagnostics) {
    $compilerCppProfileFlags += ' -DFABGL_STUDIO_SOAK_DIAGNOSTICS=1'
}
$partitionScheme = 'huge_app'
$partitionAppBytes = 0x300000
$fqbn = [string]$manifest.profile.fqbn
$fqbn = $fqbn.Replace('DebugLevel=none', "DebugLevel=$profileDebugLevel")
if ($EnablePsram) {
    $fqbn = $fqbn.Replace('PSRAM=disabled', 'PSRAM=enabled')
}
$requiredBoardOptions = @(
    'UploadSpeed=921600',
    'CPUFreq=240',
    'FlashFreq=40',
    'FlashMode=dio',
    'FlashSize=4M',
    "PartitionScheme=$partitionScheme",
    "DebugLevel=$profileDebugLevel"
)
if ($fqbn -notmatch '^esp32:esp32:esp32:') {
    throw "Unsafe or unexpected board profile in manifest: $fqbn"
}
$boardOptionSet = @($fqbn.Substring('esp32:esp32:esp32:'.Length).Split(','))
foreach ($requiredOption in $requiredBoardOptions) {
    if ($requiredOption -notin $boardOptionSet) {
        throw "Board profile is missing required option '$requiredOption': $fqbn"
    }
}
if ($fqbn -notmatch ',PSRAM=(disabled|enabled)(,|$)') {
    throw "Board profile has no explicit PSRAM option: $fqbn"
}

if ($DryRun) {
    [ordered]@{
        schemaVersion = 1
        dryRun = $true
        success = $true
        boardProfile = [string]$manifest.profile.id
        buildProfile = $BuildProfile
        fqbn = $fqbn
        partitionScheme = $partitionScheme
        partitionAppBytes = [uint64]$partitionAppBytes
        compilerWarnings = $compilerWarnings
        compilerCppExtraFlags = $compilerCppProfileFlags
        compilerCExtraFlags = $compilerCProfileFlags
        soakDiagnostics = [bool]$SoakDiagnostics
        buildProperties = @(
            "compiler.cpp.extra_flags=$compilerCppProfileFlags",
            "compiler.c.extra_flags=$compilerCProfileFlags"
        )
        project = if ($ProjectPath) { $ProjectPath } else { $null }
        sketch = $SketchDirectory
        outputRoot = $OutputRoot
        exportPerformed = $false
        compilePerformed = $false
        uploadPerformed = $false
    } | ConvertTo-Json -Depth 5
    return
}

if ($ProjectPath) {
    $stagingRoot = Assert-PathWithinRepository -Path (Join-Path $OutputRoot 'staged') `
        -RepositoryRoot $repositoryRoot -Description 'ESP32 project staging root'
    New-Item -ItemType Directory -Path $stagingRoot -Force | Out-Null
    if (Test-Path -LiteralPath $SketchDirectory) {
        if (-not $Clean) {
            throw "Staged sketch already exists; pass -Clean to replace the verified export: $SketchDirectory"
        }
        $existingResultPath = Join-Path $SketchDirectory 'ExportResult.json'
        if (-not (Test-Path -LiteralPath $existingResultPath -PathType Leaf)) {
            throw "Refusing to remove an unowned staged sketch directory: $SketchDirectory"
        }
        $existingResult = Get-Content -LiteralPath $existingResultPath -Raw | ConvertFrom-Json
        if ($existingResult.schemaVersion -ne 1 -or
            $existingResult.kind -ne 'FabGLStudioEsp32Export' -or
            $existingResult.sketchFileName -ne "$sketchName.ino") {
            throw "Refusing to remove a staged directory with invalid ownership metadata: $SketchDirectory"
        }
        $verifiedSketchDirectory = Assert-PathWithinRepository -Path $SketchDirectory `
            -RepositoryRoot $stagingRoot -Description 'owned ESP32 staged sketch directory'
        $verifiedSketchDirectory = Assert-NoReparsePoints -Path $verifiedSketchDirectory `
            -TrustedRoot $stagingRoot -Description 'owned ESP32 staged sketch directory'
        Remove-Item -LiteralPath $verifiedSketchDirectory -Recurse -Force
    }

    $ProjectCli = Find-ProjectCli -RepositoryRoot $repositoryRoot -ExplicitPath $ProjectCli
    if (-not (Test-Path -LiteralPath $ProjectCli -PathType Leaf)) {
        throw "fabgl_project_cli executable not found: $ProjectCli"
    }
    $exportOutput = @(& $ProjectCli 'export-esp32' $ProjectPath $firmwareTemplateDirectory `
            $SketchDirectory 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "ESP32 project export failed.`n$($exportOutput -join [Environment]::NewLine)"
    }
    $exportResultPath = Join-Path $SketchDirectory 'ExportResult.json'
    if (-not (Test-Path -LiteralPath $exportResultPath -PathType Leaf)) {
        throw "Project export did not produce ExportResult.json: $exportResultPath"
    }
    $projectExport = Get-Content -LiteralPath $exportResultPath -Raw | ConvertFrom-Json
    if ($projectExport.schemaVersion -ne 1 -or
        $projectExport.kind -ne 'FabGLStudioEsp32Export' -or
        $projectExport.sceneFormatVersion -ne 2 -or
        $projectExport.sketchFileName -ne "$sketchName.ino" -or
        ([string]$projectExport.payloadChecksum) -notmatch '^0x[0-9a-f]{16}$' -or
        ([string]$projectExport.packBuildChecksum) -notmatch '^0x[0-9a-f]{16}$') {
        throw "Project export metadata is invalid: $exportResultPath"
    }
    Write-Host ($exportOutput -join [Environment]::NewLine)
}

if (-not (Test-Path -LiteralPath $SketchDirectory -PathType Container)) {
    throw "Sketch directory not found after export: $SketchDirectory"
}
if (-not (Test-Path -LiteralPath (Join-Path $SketchDirectory "$sketchName.ino") -PathType Leaf)) {
    throw "Arduino sketch must contain $sketchName.ino: $SketchDirectory"
}

$bootstrapResultPath = Join-Path $repositoryRoot '.toolchains\bootstrap-result.json'
$bootstrapResult = $null
if (Test-Path -LiteralPath $bootstrapResultPath -PathType Leaf) {
    $bootstrapResult = Get-Content -LiteralPath $bootstrapResultPath -Raw | ConvertFrom-Json
}

$toolchainMode = 'managed'
if (-not $ArduinoCli) {
    if (-not $UseSystemToolchain -and $bootstrapResult -and
        (Test-Path -LiteralPath $bootstrapResult.arduinoCli -PathType Leaf)) {
        $ArduinoCli = $bootstrapResult.arduinoCli
    }
    elseif ($UseSystemToolchain) {
        $command = Get-Command arduino-cli -ErrorAction SilentlyContinue
        if ($null -eq $command) {
            throw 'arduino-cli was not found on PATH.'
        }
        $ArduinoCli = $command.Source
        $toolchainMode = 'system-validation'
    }
    else {
        throw 'Managed toolchain is not bootstrapped. Run scripts/bootstrap_toolchain.ps1, or pass -UseSystemToolchain for a non-release local validation.'
    }
}
else {
    $ArduinoCli = Get-FullPath $ArduinoCli $repositoryRoot
    if ($UseSystemToolchain) {
        $toolchainMode = 'system-validation'
    }
}
if (-not (Test-Path -LiteralPath $ArduinoCli -PathType Leaf)) {
    throw "arduino-cli executable not found: $ArduinoCli"
}

if (-not $ArduinoConfig -and -not $UseSystemToolchain -and $bootstrapResult) {
    $ArduinoConfig = $bootstrapResult.arduinoConfig
}
$globalArguments = @('--no-color')
if ($ArduinoConfig) {
    $ArduinoConfig = Get-FullPath $ArduinoConfig $repositoryRoot
    if (-not (Test-Path -LiteralPath $ArduinoConfig -PathType Leaf)) {
        throw "Arduino CLI configuration not found: $ArduinoConfig"
    }
    $globalArguments += @('--config-file', $ArduinoConfig)
}

if (-not $FabglLibrary) {
    if (-not $UseSystemToolchain -and $bootstrapResult -and
        (Test-Path -LiteralPath $bootstrapResult.fabglLibrary -PathType Container)) {
        $FabglLibrary = $bootstrapResult.fabglLibrary
    }
    elseif ($UseSystemToolchain) {
        $FabglLibrary = Join-Path $env:USERPROFILE 'Documents\Arduino\libraries\FabGL'
    }
    else {
        throw 'Managed FabGL library is not available.'
    }
}
$FabglLibrary = Get-FullPath $FabglLibrary $repositoryRoot
if (-not (Test-Path -LiteralPath (Join-Path $FabglLibrary 'src\fabgl.h') -PathType Leaf)) {
    throw "FabGL src/fabgl.h not found: $FabglLibrary"
}
$libraryProperties = Get-LibraryProperties -LibraryRoot $FabglLibrary
if ($libraryProperties['name'] -ne 'FabGL' -or
    $libraryProperties['version'] -ne $expectedFabgl) {
    throw "FabGL $expectedFabgl is required; found '$($libraryProperties['name']) $($libraryProperties['version'])'."
}

$cliVersionOutput = Invoke-ArduinoProbe -Executable $ArduinoCli `
    -GlobalArguments $globalArguments -Arguments @('version')
$cliVersionText = ($cliVersionOutput -join ' ').Trim()
if ($toolchainMode -eq 'managed' -and $cliVersionText -notmatch "Version:\s*$([regex]::Escape($expectedCli.version))(\s|$)") {
    throw "Managed Arduino CLI version mismatch; expected $($expectedCli.version), got: $cliVersionText"
}
if ($toolchainMode -ne 'managed' -and $cliVersionText -notmatch "Version:\s*$([regex]::Escape($expectedCli.version))(\s|$)") {
    Write-Warning "System validation uses a non-release CLI. Locked release version is $($expectedCli.version); detected: $cliVersionText"
}

$coreList = Invoke-ArduinoProbe -Executable $ArduinoCli `
    -GlobalArguments $globalArguments -Arguments @('core', 'list')
$coreText = $coreList -join [Environment]::NewLine
if ($coreText -notmatch "(?m)^esp32:esp32\s+$([regex]::Escape($expectedCore))\s") {
    throw "Arduino-ESP32 $expectedCore is not installed in the selected Arduino data directory."
}
$arduinoDataOutput = Invoke-ArduinoProbe -Executable $ArduinoCli `
    -GlobalArguments $globalArguments -Arguments @('config', 'get', 'directories.data')
$arduinoDataDirectory = Get-FullPath (($arduinoDataOutput -join '').Trim()) $repositoryRoot
$arduinoDownloadsOutput = Invoke-ArduinoProbe -Executable $ArduinoCli `
    -GlobalArguments $globalArguments -Arguments @('config', 'get', 'directories.downloads')
$arduinoDownloadsDirectory =
    Get-FullPath (($arduinoDownloadsOutput -join '').Trim()) $repositoryRoot
$arduinoUserOutput = Invoke-ArduinoProbe -Executable $ArduinoCli `
    -GlobalArguments $globalArguments -Arguments @('config', 'get', 'directories.user')
$arduinoUserDirectory = Get-FullPath (($arduinoUserOutput -join '').Trim()) $repositoryRoot
$installedCoreRoot = Join-Path $arduinoDataDirectory "packages\esp32\hardware\esp32\$expectedCore"
$partitionCsv = Join-Path $installedCoreRoot "tools\partitions\$partitionScheme.csv"
if (-not (Test-Path -LiteralPath $partitionCsv -PathType Leaf)) {
    throw "Locked partition table was not found in the installed core: $partitionCsv"
}
$partitionText = Get-Content -LiteralPath $partitionCsv -Raw
if ($partitionText -notmatch '(?m)^app0,\s*app,\s*ota_0,\s*0x10000,\s*0x300000,?\s*$') {
    throw "Partition table $partitionScheme does not provide the verified 0x300000-byte app slot."
}
$partitionSha256 =
    (Get-FileHash -LiteralPath $partitionCsv -Algorithm SHA256).Hash.ToLowerInvariant()

$fabglMarker = Join-Path $FabglLibrary '.fabglstudio-install.json'
$fabglLocked = $false
if (Test-Path -LiteralPath $fabglMarker -PathType Leaf) {
    $marker = Get-Content -LiteralPath $fabglMarker -Raw | ConvertFrom-Json
    $fabglLocked = $marker.commit -eq $expectedFabglCommit
}
if ($toolchainMode -eq 'managed' -and -not $fabglLocked) {
    throw "Managed FabGL marker does not match commit $expectedFabglCommit."
}
if ($toolchainMode -ne 'managed' -and -not $fabglLocked) {
    Write-Warning 'System FabGL 1.0.9 is suitable for compatibility compilation, but it is not the locked Olimex fork commit and must not be used for a release artifact.'
}

$buildSlot = "$sketchName-$BuildProfile"
$buildDirectory = Assert-PathWithinRepository -Path (Join-Path $OutputRoot "build\$buildSlot") `
    -RepositoryRoot $repositoryRoot -Description 'ESP32 build directory'
$binaryDirectory = Assert-PathWithinRepository -Path (Join-Path $OutputRoot "bin\$buildSlot") `
    -RepositoryRoot $repositoryRoot -Description 'ESP32 binary directory'
New-Item -ItemType Directory -Path $buildDirectory,$binaryDirectory -Force | Out-Null

$compileArduinoCli = $ArduinoCli
$compileGlobalArguments = @($globalArguments)
$compileFabglLibrary = $FabglLibrary
$compileBuildDirectory = $buildDirectory
$compileBinaryDirectory = $binaryDirectory
$compileSketchDirectory = $SketchDirectory
$shortPathConfig = $null
$substDrive = $null
if ($env:OS -eq 'Windows_NT') {
    $usedDrives = @(Get-PSDrive -PSProvider FileSystem | ForEach-Object { "$($_.Name):" })
    foreach ($letter in @('Z', 'Y', 'X', 'W', 'V', 'U', 'T')) {
        $candidate = "${letter}:"
        if ($candidate -notin $usedDrives) {
            $substDrive = $candidate
            break
        }
    }
    if (-not $substDrive) {
        throw 'No free drive letter is available for the Windows ESP32 long-path guard.'
    }
    $shortPathConfig = Join-Path $OutputRoot 'arduino-cli-short-path.yaml'
    New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
    $shortData = (Convert-ToMappedRepositoryPath $arduinoDataDirectory $repositoryRoot `
            $substDrive).Replace('\', '/')
    $shortDownloads = (Convert-ToMappedRepositoryPath $arduinoDownloadsDirectory $repositoryRoot `
            $substDrive).Replace('\', '/')
    $shortUser = (Convert-ToMappedRepositoryPath $arduinoUserDirectory $repositoryRoot `
            $substDrive).Replace('\', '/')
    $indexUrl = [string]$manifest.boardManager.indexUrl
    if ($indexUrl -notmatch '^https://') {
        throw "Board Manager index must use HTTPS: $indexUrl"
    }
    $shortConfigText = @(
        'board_manager:',
        '  additional_urls:',
        "    - '$($indexUrl.Replace("'", "''"))'",
        '  enable_unsafe_install: false',
        'directories:',
        "  data: '$($shortData.Replace("'", "''"))'",
        "  downloads: '$($shortDownloads.Replace("'", "''"))'",
        "  user: '$($shortUser.Replace("'", "''"))'",
        'library:',
        '  enable_unsafe_install: false'
    ) -join [Environment]::NewLine
    $shortConfigText | Set-Content -LiteralPath $shortPathConfig -Encoding UTF8
    & subst.exe $substDrive $repositoryRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to create the temporary ESP32 path mapping $substDrive"
    }
    $compileArduinoCli = Convert-ToMappedRepositoryPath $ArduinoCli $repositoryRoot $substDrive
    $compileGlobalArguments = @('--no-color', '--config-file',
        (Convert-ToMappedRepositoryPath $shortPathConfig $repositoryRoot $substDrive))
    $compileFabglLibrary = Convert-ToMappedRepositoryPath $FabglLibrary $repositoryRoot $substDrive
    $compileBuildDirectory = Convert-ToMappedRepositoryPath $buildDirectory $repositoryRoot $substDrive
    $compileBinaryDirectory =
        Convert-ToMappedRepositoryPath $binaryDirectory $repositoryRoot $substDrive
    $compileSketchDirectory =
        Convert-ToMappedRepositoryPath $SketchDirectory $repositoryRoot $substDrive
}

$arguments = @($compileGlobalArguments)
$arguments += @(
    'compile',
    '--fqbn', $fqbn,
    '--library', $compileFabglLibrary,
    '--build-path', $compileBuildDirectory,
    '--output-dir', $compileBinaryDirectory,
    '--jobs', [string]$Jobs,
    '--warnings', $compilerWarnings,
    '--build-property', "compiler.cpp.extra_flags=$compilerCppProfileFlags",
    '--build-property', "compiler.c.extra_flags=$compilerCProfileFlags"
)
if ($Clean) {
    $arguments += '--clean'
}
if ($VerboseBuild) {
    $arguments += '--verbose'
}
$arguments += $compileSketchDirectory

$forbiddenArguments = @('upload', '--upload', '-u')
foreach ($argument in $arguments) {
    if ($forbiddenArguments -contains $argument) {
        throw "Internal safety check rejected forbidden upload argument: $argument"
    }
}

Write-Host "Compiling $SketchDirectory"
Write-Host "Build profile: $BuildProfile ($profileOptimizationFlags)"
Write-Host "Board profile: $fqbn"
Write-Host "Toolchain mode: $toolchainMode"
Write-Host 'Upload is not part of this script and no serial port will be opened.'
$compileExitCode = $null
$compileOutput = New-Object 'System.Collections.Generic.List[string]'
$previousErrorActionPreference = $ErrorActionPreference
try {
    # Native compilers legitimately write warnings to stderr. With Stop in
    # Windows PowerShell, a redirected native stderr record can terminate this
    # pipeline before LASTEXITCODE is inspected, turning an allowed vendor
    # warning into a false build failure. Capture every line and decide solely
    # from the native process exit code below.
    $ErrorActionPreference = 'Continue'
    & $compileArduinoCli @arguments 2>&1 | ForEach-Object {
        $line = $_.ToString()
        $compileOutput.Add($line)
        Write-Host $line
    }
    $compileExitCode = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $previousErrorActionPreference
    if ($substDrive) {
        & subst.exe $substDrive /D | Out-Null
    }
}
if ($compileExitCode -ne 0) {
    throw "ESP32 compilation failed with exit code $compileExitCode."
}
$programStorageBytes = $null
$globalStaticRamBytes = $null
$dynamicRamRemainingBytes = $null
foreach ($line in $compileOutput) {
    if ($line -match '^Sketch uses ([0-9]+) bytes .* Maximum is ([0-9]+) bytes\.$') {
        $programStorageBytes = [uint64]$matches[1]
        if ([uint64]$matches[2] -ne [uint64]$partitionAppBytes) {
            throw 'Arduino size report does not match the verified app partition size.'
        }
    }
    if ($line -match '^Global variables use ([0-9]+) bytes .* leaving ([0-9]+) bytes .* Maximum is ([0-9]+) bytes\.$') {
        $globalStaticRamBytes = [uint64]$matches[1]
        $dynamicRamRemainingBytes = [uint64]$matches[2]
        if ($globalStaticRamBytes + $dynamicRamRemainingBytes -ne [uint64]$matches[3]) {
            throw 'Arduino dynamic-memory report is internally inconsistent.'
        }
    }
}
if ($null -eq $programStorageBytes -or $null -eq $globalStaticRamBytes -or
    $null -eq $dynamicRamRemainingBytes) {
    throw 'Locked Arduino CLI did not emit the required program/RAM size analysis.'
}

$primaryBinaryPath = Join-Path $binaryDirectory "$sketchName.ino.bin"
if (-not (Test-Path -LiteralPath $primaryBinaryPath -PathType Leaf)) {
    throw "Compilation succeeded but the primary firmware .bin was not found: $binaryDirectory"
}
$primaryBinary = Get-Item -LiteralPath $primaryBinaryPath
if ([uint64]$primaryBinary.Length -gt [uint64]$partitionAppBytes) {
    throw "Firmware exceeds the verified $partitionScheme app partition: $($primaryBinary.Length) > $partitionAppBytes bytes."
}
$elfPath = Join-Path $binaryDirectory "$sketchName.ino.elf"
$elf = if (Test-Path -LiteralPath $elfPath -PathType Leaf) {
    Get-Item -LiteralPath $elfPath
}
else {
    $null
}
$mapPath = Join-Path $buildDirectory "$sketchName.ino.map"
$map = if (Test-Path -LiteralPath $mapPath -PathType Leaf) {
    Get-Item -LiteralPath $mapPath
}
else {
    $null
}
if ($null -eq $map -or [uint64]$map.Length -eq [uint64]0) {
    throw "ESP32 link succeeded but the required memory map was not produced: $mapPath"
}
$payload = if ($ProjectPath) {
    Get-Item -LiteralPath (Join-Path $SketchDirectory 'ProjectPayload.fglpak')
}
else {
    $null
}
$summary = [ordered]@{
    schemaVersion = 2
    success = $true
    toolchainMode = $toolchainMode
    profile = $manifest.profile.id
    buildProfile = $BuildProfile
    profileOptimizationFlags = $profileOptimizationFlags
    fqbn = $fqbn
    partitionScheme = $partitionScheme
    partitionAppBytes = [uint64]$partitionAppBytes
    partitionCsv = $partitionCsv
    partitionSha256 = $partitionSha256
    arduinoCli = $ArduinoCli
    arduinoConfig = if ($ArduinoConfig) { $ArduinoConfig } else { $null }
    arduinoCliVersion = $cliVersionText
    arduinoCore = $expectedCore
    fabglVersion = $expectedFabgl
    fabglCommitVerified = $fabglLocked
    compilerWarnings = $compilerWarnings
    compilerCompatibilityFlags = $compilerCppExtraFlags
    compilerCppExtraFlags = $compilerCppProfileFlags
    compilerCExtraFlags = $compilerCProfileFlags
    soakDiagnostics = [bool]$SoakDiagnostics
    compileArguments = $arguments
    sketch = $SketchDirectory
    projectSource = if ($ProjectPath) { $ProjectPath } else { $null }
    exportPerformed = [bool]$ProjectPath
    projectExport = $projectExport
    payload = if ($payload) { $payload.FullName } else { $null }
    payloadBytes = if ($payload) { [uint64]$payload.Length } else { $null }
    payloadSha256 = if ($payload) {
        (Get-FileHash -LiteralPath $payload.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    else { $null }
    binary = $primaryBinary.FullName
    binaryBytes = [uint64]$primaryBinary.Length
    binarySha256 = (Get-FileHash -LiteralPath $primaryBinary.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    appPartitionUsagePercent =
        [Math]::Round(100.0 * [double]$primaryBinary.Length / [double]$partitionAppBytes, 3)
    programStorageBytes = $programStorageBytes
    programStorageUsagePercent =
        [Math]::Round(100.0 * [double]$programStorageBytes / [double]$partitionAppBytes, 3)
    globalStaticRamBytes = $globalStaticRamBytes
    dynamicRamRemainingBytes = $dynamicRamRemainingBytes
    dynamicRamTotalBytes = $globalStaticRamBytes + $dynamicRamRemainingBytes
    elf = if ($elf) { $elf.FullName } else { $null }
    elfBytes = if ($elf) { [uint64]$elf.Length } else { $null }
    elfSha256 = if ($elf) {
        (Get-FileHash -LiteralPath $elf.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    else { $null }
    map = if ($map) { $map.FullName } else { $null }
    mapBytes = if ($map) { [uint64]$map.Length } else { $null }
    mapSha256 = if ($map) {
        (Get-FileHash -LiteralPath $map.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    else { $null }
    psramProfile = if ($EnablePsram) { 'enabled-experimental' } else { 'disabled-reference' }
    uploadPerformed = $false
}
$summaryPath = Join-Path $OutputRoot 'build-result.json'
$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
$summary | ConvertTo-Json -Depth 5
