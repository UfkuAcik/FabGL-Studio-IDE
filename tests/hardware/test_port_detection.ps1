$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$detector = Join-Path $repositoryRoot 'scripts\detect_serial_ports.ps1'
$fixture = Join-Path $PSScriptRoot 'fixtures\serial-ports.json'

$defaultResult = (& $detector -FixturePath $fixture | Out-String) | ConvertFrom-Json
if ($defaultResult.schemaVersion -ne 1 -or $defaultResult.uploadPerformed -or
    $defaultResult.portOpened -or $defaultResult.count -ne 2) {
    throw 'Default serial detection safety contract failed.'
}
$candidate = @($defaultResult.ports | Where-Object port -eq 'COM7')
if ($candidate.Count -ne 1 -or -not $candidate[0].boardCandidate -or
    $candidate[0].confidence -ne 'high' -or -not $candidate[0].requiresUserConfirmation) {
    throw 'CH340 board candidate classification failed.'
}
$unknown = @($defaultResult.ports | Where-Object port -eq 'COM9')
if ($unknown.Count -ne 1 -or $unknown[0].boardCandidate -or
    $unknown[0].confidence -ne 'unknown') {
    throw 'Unknown serial bridge classification failed.'
}

$withBluetooth = (& $detector -FixturePath $fixture -IncludeBluetooth | Out-String) |
    ConvertFrom-Json
if ($withBluetooth.count -ne 3) {
    throw 'Explicit Bluetooth inclusion did not return all fixture ports.'
}
$bluetooth = @($withBluetooth.ports | Where-Object port -eq 'COM4')
if ($bluetooth.Count -ne 1 -or -not $bluetooth[0].bluetooth -or
    $bluetooth[0].boardCandidate) {
    throw 'Bluetooth port classification failed.'
}

Write-Output 'serial port detection fixture: PASS'
