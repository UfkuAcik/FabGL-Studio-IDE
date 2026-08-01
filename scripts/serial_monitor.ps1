[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Port,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1200, 3000000)]
    [int]$Baud,

    [Parameter(Mandatory = $true)]
    [ValidateSet('olimex-esp32-sbc-fabgl-revb')]
    [string]$ConfirmBoardProfile,

    [string]$ArduinoCli,
    [string]$ArduinoConfig,
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
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($ConfirmBoardProfile -ne [string]$manifest.profile.id) {
    throw "Board confirmation must equal '$($manifest.profile.id)'."
}
Assert-SafeSerialPort -Value $Port

$bootstrapResultPath = Join-Path $repositoryRoot '.toolchains\bootstrap-result.json'
$bootstrapResult = $null
if (Test-Path -LiteralPath $bootstrapResultPath -PathType Leaf) {
    $bootstrapResult = Get-Content -LiteralPath $bootstrapResultPath -Raw | ConvertFrom-Json
}
if (-not $ArduinoCli) {
    if ($null -eq $bootstrapResult -or
        [string]::IsNullOrWhiteSpace([string]$bootstrapResult.arduinoCli)) {
        throw 'Managed Arduino CLI is not bootstrapped; pass an explicit -ArduinoCli only for an intentional local monitor session.'
    }
    $ArduinoCli = [string]$bootstrapResult.arduinoCli
}
$ArduinoCli = Get-FullPath $ArduinoCli $repositoryRoot
if (-not (Test-Path -LiteralPath $ArduinoCli -PathType Leaf)) {
    throw "Arduino CLI executable not found: $ArduinoCli"
}

if (-not $ArduinoConfig -and $null -ne $bootstrapResult -and
    $null -ne $bootstrapResult.PSObject.Properties['arduinoConfig']) {
    $ArduinoConfig = [string]$bootstrapResult.arduinoConfig
}
$arguments = @('--no-color')
if ($ArduinoConfig) {
    $ArduinoConfig = Get-FullPath $ArduinoConfig $repositoryRoot
    if (-not (Test-Path -LiteralPath $ArduinoConfig -PathType Leaf)) {
        throw "Arduino CLI configuration not found: $ArduinoConfig"
    }
    $arguments += @('--config-file', $ArduinoConfig)
}
$arguments += @(
    'monitor',
    '--port', $Port,
    '--config', "baudrate=$Baud"
)
if ($arguments -contains 'upload' -or
    $arguments -contains '--upload' -or
    $arguments -contains '-u') {
    throw 'Internal safety check rejected an upload operation in the serial monitor.'
}

$plan = [ordered]@{
    schemaVersion = 1
    dryRun = [bool]$DryRun
    executed = $false
    profile = [string]$manifest.profile.id
    port = $Port
    baud = $Baud
    program = $ArduinoCli
    arguments = $arguments
    uploadPerformed = $false
}
if ($DryRun) {
    $plan | ConvertTo-Json -Depth 5
    return
}

Write-Host "Opening serial monitor on $Port at $Baud baud. This operation cannot upload firmware."
& $ArduinoCli @arguments
$monitorExitCode = $LASTEXITCODE
if ($monitorExitCode -ne 0) {
    throw "Serial monitor exited with code $monitorExitCode."
}
$plan.executed = $true
$plan | ConvertTo-Json -Depth 5
