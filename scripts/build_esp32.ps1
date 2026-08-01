[CmdletBinding()]
param(
    [string]$ManifestPath,
    [string]$SketchDirectory,
    [string]$OutputRoot,
    [string]$ArduinoCli,
    [string]$ArduinoConfig,
    [string]$FabglLibrary,
    [ValidateRange(1, 256)][int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount),
    [switch]$UseSystemToolchain,
    [switch]$EnablePsram,
    [switch]$Clean,
    [switch]$VerboseBuild
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

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

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $ManifestPath) {
    $ManifestPath = Join-Path $repositoryRoot 'toolchains\manifest.json'
}
if (-not $SketchDirectory) {
    $SketchDirectory = Join-Path $repositoryRoot 'platforms\fabgl\firmware'
}
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repositoryRoot 'out\esp32'
}

$ManifestPath = Get-FullPath $ManifestPath $repositoryRoot
$SketchDirectory = Get-FullPath $SketchDirectory $repositoryRoot
$OutputRoot = Assert-PathWithinRepository -Path $OutputRoot -RepositoryRoot $repositoryRoot `
    -Description 'ESP32 output root'
if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
    throw "Toolchain manifest not found: $ManifestPath"
}
if (-not (Test-Path -LiteralPath $SketchDirectory -PathType Container)) {
    throw "Sketch directory not found: $SketchDirectory"
}
$sketchName = Split-Path -Leaf $SketchDirectory
if (-not (Test-Path -LiteralPath (Join-Path $SketchDirectory "$sketchName.ino") -PathType Leaf)) {
    throw "Arduino sketch must contain $sketchName.ino: $SketchDirectory"
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

$fqbn = [string]$manifest.profile.fqbn
if ($EnablePsram) {
    $fqbn = $fqbn.Replace('PSRAM=disabled', 'PSRAM=enabled')
}
if ($fqbn -notmatch '^esp32:esp32:esp32:' -or
    $fqbn -notmatch 'PartitionScheme=huge_app') {
    throw "Unsafe or unexpected board profile in manifest: $fqbn"
}

$buildDirectory = Assert-PathWithinRepository -Path (Join-Path $OutputRoot 'build') `
    -RepositoryRoot $repositoryRoot -Description 'ESP32 build directory'
$binaryDirectory = Assert-PathWithinRepository -Path (Join-Path $OutputRoot 'bin') `
    -RepositoryRoot $repositoryRoot -Description 'ESP32 binary directory'
New-Item -ItemType Directory -Path $buildDirectory,$binaryDirectory -Force | Out-Null

$arguments = @($globalArguments)
$arguments += @(
    'compile',
    '--fqbn', $fqbn,
    '--library', $FabglLibrary,
    '--build-path', $buildDirectory,
    '--output-dir', $binaryDirectory,
    '--jobs', [string]$Jobs,
    '--warnings', $compilerWarnings,
    '--build-property', "compiler.cpp.extra_flags=$compilerCppExtraFlags"
)
if ($Clean) {
    $arguments += '--clean'
}
if ($VerboseBuild) {
    $arguments += '--verbose'
}
$arguments += $SketchDirectory

$forbiddenArguments = @('upload', '--upload', '-u')
foreach ($argument in $arguments) {
    if ($forbiddenArguments -contains $argument) {
        throw "Internal safety check rejected forbidden upload argument: $argument"
    }
}

Write-Host "Compiling $SketchDirectory"
Write-Host "Profile: $fqbn"
Write-Host "Toolchain mode: $toolchainMode"
Write-Host 'Upload is not part of this script and no serial port will be opened.'
& $ArduinoCli @arguments
$compileExitCode = $LASTEXITCODE
if ($compileExitCode -ne 0) {
    throw "ESP32 compilation failed with exit code $compileExitCode."
}

$primaryBinary = Get-ChildItem -LiteralPath $binaryDirectory -Filter '*.ino.bin' -File |
    Where-Object Name -NotMatch 'bootloader|partitions' |
    Sort-Object Name |
    Select-Object -First 1
if ($null -eq $primaryBinary) {
    throw "Compilation succeeded but the primary firmware .bin was not found: $binaryDirectory"
}
$elf = Get-ChildItem -LiteralPath $binaryDirectory -Filter '*.ino.elf' -File |
    Sort-Object Name | Select-Object -First 1
$summary = [ordered]@{
    schemaVersion = 1
    success = $true
    toolchainMode = $toolchainMode
    profile = $manifest.profile.id
    fqbn = $fqbn
    arduinoCli = $ArduinoCli
    arduinoConfig = if ($ArduinoConfig) { $ArduinoConfig } else { $null }
    arduinoCliVersion = $cliVersionText
    arduinoCore = $expectedCore
    fabglVersion = $expectedFabgl
    fabglCommitVerified = $fabglLocked
    compilerWarnings = $compilerWarnings
    compilerCppExtraFlags = $compilerCppExtraFlags
    sketch = $SketchDirectory
    binary = $primaryBinary.FullName
    binaryBytes = [uint64]$primaryBinary.Length
    binarySha256 = (Get-FileHash -LiteralPath $primaryBinary.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    elf = if ($elf) { $elf.FullName } else { $null }
    elfBytes = if ($elf) { [uint64]$elf.Length } else { $null }
    psramProfile = if ($EnablePsram) { 'enabled-experimental' } else { 'disabled-reference' }
    uploadPerformed = $false
}
$summaryPath = Join-Path $OutputRoot 'build-result.json'
$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
$summary | ConvertTo-Json -Depth 5
