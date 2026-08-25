[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Port,

    [Parameter(Mandatory = $true)]
    [ValidateSet('olimex-esp32-sbc-fabgl-revb')]
    [string]$ConfirmBoardProfile,

    [string]$BuildResultPath,
    [switch]$DryRun
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
    $root = (Get-FullPath $RepositoryRoot).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar)
    $prefix = $root + [System.IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.Equals($root, [System.StringComparison]::OrdinalIgnoreCase) -and
        -not $fullPath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description must remain inside the repository: $fullPath"
    }
    return $fullPath
}

function Assert-Property {
    param(
        [Parameter(Mandatory = $true)][psobject]$Object,
        [Parameter(Mandatory = $true)][string]$Name
    )

    if ($null -eq $Object.PSObject.Properties[$Name]) {
        throw "Build result is missing required property '$Name'. Rebuild with scripts/build_esp32.ps1."
    }
}

function Assert-SafeSerialPort {
    param([Parameter(Mandatory = $true)][string]$Value)

    $windowsPort = '^COM[1-9][0-9]{0,2}$'
    $unixPort = '^/dev/(tty(USB|ACM|S)[A-Za-z0-9._-]*|cu\.[A-Za-z0-9._-]+)$'
    if ($Value -notmatch $windowsPort -and $Value -notmatch $unixPort) {
        throw "Serial port must be an explicit COM port or supported /dev serial path: $Value"
    }
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$manifestPath = Join-Path $repositoryRoot 'toolchains\manifest.json'
if (-not $BuildResultPath) {
    $BuildResultPath = Join-Path $repositoryRoot 'out\esp32\build-result.json'
}
$BuildResultPath = Assert-PathWithinRepository -Path $BuildResultPath `
    -RepositoryRoot $repositoryRoot -Description 'Build result path'
if (-not (Test-Path -LiteralPath $BuildResultPath -PathType Leaf)) {
    throw "Build result not found: $BuildResultPath"
}
Assert-SafeSerialPort -Value $Port

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$buildResult = Get-Content -LiteralPath $BuildResultPath -Raw | ConvertFrom-Json
foreach ($property in @(
        'schemaVersion', 'success', 'toolchainMode', 'profile', 'fqbn',
        'buildProfile', 'profileOptimizationFlags', 'partitionScheme',
        'partitionAppBytes', 'partitionCsv', 'partitionSha256',
        'arduinoCli', 'arduinoConfig', 'arduinoCliVersion', 'arduinoCore',
        'fabglVersion', 'fabglCommitVerified', 'compilerWarnings',
        'compilerCompatibilityFlags', 'compilerCppExtraFlags', 'compilerCExtraFlags',
        'binary', 'binaryBytes', 'binarySha256',
        'psramProfile', 'uploadPerformed')) {
    Assert-Property -Object $buildResult -Name $property
}

$expectedProfile = [string]$manifest.profile.id
$expectedCore = [string]$manifest.profile.arduinoCore.version
$expectedFabgl = [string]$manifest.profile.fabgl.version
$expectedCli = @($manifest.artifacts | Where-Object id -eq 'arduino-cli-windows-x86_64')[0]
$referenceFqbn = [string]$manifest.profile.fqbn
$expectedOptimizationFlags = $null
$expectedDefinition = $null
$expectedDebugLevel = 'none'
switch ([string]$buildResult.buildProfile) {
    'Debug' {
        $expectedOptimizationFlags = '-Og -g3 -fno-omit-frame-pointer'
        $expectedDefinition = '-DFABGL_STUDIO_PROFILE_DEBUG=1'
        $expectedDebugLevel = 'debug'
    }
    'Release' {
        $expectedOptimizationFlags = '-O2 -g1'
        $expectedDefinition = '-DNDEBUG -DFABGL_STUDIO_PROFILE_RELEASE=1'
    }
    'SizeOptimized' {
        $expectedOptimizationFlags = '-Os -g0 -ffunction-sections -fdata-sections'
        $expectedDefinition = '-DNDEBUG -DFABGL_STUDIO_PROFILE_SIZE_OPTIMIZED=1'
    }
    'PerformanceOptimized' {
        $expectedOptimizationFlags = '-O3 -g0 -funroll-loops'
        $expectedDefinition = '-DNDEBUG -DFABGL_STUDIO_PROFILE_PERFORMANCE_OPTIMIZED=1'
    }
    default {
        throw "Build result has an unsupported ESP32 build profile: $($buildResult.buildProfile)"
    }
}
$expectedFqbn = $referenceFqbn.Replace('DebugLevel=none', "DebugLevel=$expectedDebugLevel")
if ($buildResult.psramProfile -eq 'enabled-experimental') {
    $expectedFqbn = $expectedFqbn.Replace('PSRAM=disabled', 'PSRAM=enabled')
}

if ($ConfirmBoardProfile -ne $expectedProfile -or $buildResult.profile -ne $expectedProfile) {
    throw "Board confirmation and build profile must both equal '$expectedProfile'."
}
if ($buildResult.schemaVersion -ne 2 -or
    $buildResult.success -isnot [bool] -or -not $buildResult.success) {
    throw 'Build result does not represent a successful schemaVersion 2 build.'
}
if ($buildResult.uploadPerformed -isnot [bool] -or $buildResult.uploadPerformed) {
    throw 'Build result is invalid or already records an upload operation.'
}
if ($buildResult.arduinoCore -ne $expectedCore -or
    $buildResult.fabglVersion -ne $expectedFabgl -or
    $buildResult.fabglCommitVerified -isnot [bool] -or
    -not $buildResult.fabglCommitVerified) {
    throw 'Build result does not verify the locked Arduino core and Olimex FabGL source.'
}
if ($buildResult.arduinoCliVersion -notmatch "Version:\s*$([regex]::Escape($expectedCli.version))(\s|$)") {
    throw "Build result does not verify locked Arduino CLI $($expectedCli.version)."
}
if ($buildResult.compilerWarnings -ne [string]$manifest.profile.compiler.warnings -or
    $buildResult.compilerCompatibilityFlags -ne [string]$manifest.profile.compiler.cppExtraFlags -or
    $buildResult.profileOptimizationFlags -ne $expectedOptimizationFlags -or
    $buildResult.compilerCppExtraFlags -ne
        "$($manifest.profile.compiler.cppExtraFlags) $expectedOptimizationFlags $expectedDefinition" -or
    $buildResult.compilerCExtraFlags -ne "$expectedOptimizationFlags $expectedDefinition") {
    throw 'Build result compiler contract does not match the manifest.'
}
if ($buildResult.fqbn -ne $expectedFqbn) {
    throw 'Build result FQBN does not match its build and PSRAM profiles.'
}
if ($buildResult.psramProfile -notin @('disabled-reference', 'enabled-experimental')) {
    throw 'Build result PSRAM label does not agree with its FQBN.'
}
if ($buildResult.partitionScheme -ne 'huge_app' -or
    [uint64]$buildResult.partitionAppBytes -ne 0x300000) {
    throw 'Build result does not use the verified huge_app 0x300000-byte app partition.'
}
$partitionCsv = Assert-PathWithinRepository -Path ([string]$buildResult.partitionCsv) `
    -RepositoryRoot $repositoryRoot -Description 'Partition table'
if (-not (Test-Path -LiteralPath $partitionCsv -PathType Leaf) -or
    (Get-FileHash -LiteralPath $partitionCsv -Algorithm SHA256).Hash.ToLowerInvariant() -ne
        [string]$buildResult.partitionSha256) {
    throw 'Build result partition table is missing or no longer matches its SHA-256.'
}

$binary = Assert-PathWithinRepository -Path ([string]$buildResult.binary) `
    -RepositoryRoot $repositoryRoot -Description 'Firmware binary'
if (-not $binary.EndsWith('.ino.bin', [System.StringComparison]::OrdinalIgnoreCase) -or
    -not (Test-Path -LiteralPath $binary -PathType Leaf)) {
    throw "Primary firmware binary is missing or has an unexpected name: $binary"
}
$binaryInfo = Get-Item -LiteralPath $binary
$actualHash = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant()
$expectedHash = [string]$buildResult.binarySha256
if ($expectedHash -notmatch '^[0-9a-f]{64}$' -or
    [uint64]$binaryInfo.Length -ne [uint64]$buildResult.binaryBytes -or
    [uint64]$binaryInfo.Length -gt [uint64]$buildResult.partitionAppBytes -or
    $actualHash -ne $expectedHash) {
    throw 'Firmware binary size or SHA-256 no longer matches build-result.json.'
}

$arduinoCli = Get-FullPath ([string]$buildResult.arduinoCli) $repositoryRoot
if (-not (Test-Path -LiteralPath $arduinoCli -PathType Leaf)) {
    throw "Arduino CLI recorded by the build no longer exists: $arduinoCli"
}
$arguments = @('--no-color')
if ($null -ne $buildResult.arduinoConfig -and
    -not [string]::IsNullOrWhiteSpace([string]$buildResult.arduinoConfig)) {
    $arduinoConfig = Get-FullPath ([string]$buildResult.arduinoConfig) $repositoryRoot
    if (-not (Test-Path -LiteralPath $arduinoConfig -PathType Leaf)) {
        throw "Arduino configuration recorded by the build no longer exists: $arduinoConfig"
    }
    $arguments += @('--config-file', $arduinoConfig)
}
$arguments += @(
    'upload',
    '--port', $Port,
    '--fqbn', [string]$buildResult.fqbn,
    '--input-file', $binary
)

$plan = [ordered]@{
    schemaVersion = 1
    dryRun = [bool]$DryRun
    executed = $false
    profile = $expectedProfile
    port = $Port
    program = $arduinoCli
    arguments = $arguments
    buildResult = $BuildResultPath
    binary = $binary
    binaryBytes = [uint64]$binaryInfo.Length
    binarySha256 = $actualHash
    fqbn = [string]$buildResult.fqbn
}
if ($DryRun) {
    $plan | ConvertTo-Json -Depth 5
    return
}

Write-Host "Uploading only the verified binary $binary"
Write-Host "Confirmed board profile: $expectedProfile"
Write-Host "Human-selected serial port: $Port"
& $arduinoCli @arguments
if ($LASTEXITCODE -ne 0) {
    throw "ESP32 upload failed with exit code $LASTEXITCODE."
}
$plan.executed = $true
$plan.uploadedAtUtc = [DateTime]::UtcNow.ToString('o')
$uploadResultPath = Join-Path (Split-Path -Parent $BuildResultPath) 'upload-result.json'
$plan | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $uploadResultPath -Encoding UTF8
$plan | ConvertTo-Json -Depth 5
