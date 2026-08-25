$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Assert-True {
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$capture = Join-Path $repositoryRoot 'scripts\capture_hardware_diagnostics.ps1'
$passFixture = Join-Path $PSScriptRoot 'fixtures\sample-pass.log'
$failFixture = Join-Path $PSScriptRoot 'fixtures\sample-fail.log'
$nonce = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path $repositoryRoot "out\hardware-diagnostic-contract-$nonce"
$dryRunRoot = Join-Path $testRoot 'dry-run'

try {
    $dryRunText = @(& $capture -Port COM5 -Baud 115200 `
            -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb `
            -OutputDirectory $dryRunRoot -DiagnosticCheck vga -DurationSeconds 3 -DryRun) `
        -join [Environment]::NewLine
    $dryRun = $dryRunText | ConvertFrom-Json
    Assert-True ([bool]$dryRun.dryRun -and -not [bool]$dryRun.portOpened -and
        -not [bool]$dryRun.uploadPerformed) `
        'Hardware diagnostic dry-run opened a port or claimed an upload.'
    Assert-True (-not (Test-Path -LiteralPath $dryRunRoot)) `
        'Hardware diagnostic dry-run created an output directory.'

    $vgaRoot = Join-Path $testRoot 'vga-fixture'
    $vgaText = @(& $capture -Port COM5 -Baud 115200 `
            -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb `
            -OutputDirectory $vgaRoot -DiagnosticCheck vga -DurationSeconds 3 `
            -FixturePath $passFixture) -join [Environment]::NewLine
    $vga = $vgaText | ConvertFrom-Json
    Assert-True ([bool]$vga.fixtureMode -and -not [bool]$vga.portOpened -and
        [string]$vga.automatedResult -eq 'PASS') `
        'The VGA fixture did not produce a safe fixture-labelled automated result.'
    Assert-True (-not [bool]$vga.hardwareVerified -and
        [bool]$vga.manualVerificationPending -and
        [bool]$vga.checks.vga.manualVerificationPending) `
        'VGA structured checks were incorrectly promoted to physical hardware verification.'

    $audioRoot = Join-Path $testRoot 'audio-fixture'
    $audioText = @(& $capture -Port COM5 -Baud 115200 `
            -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb `
            -OutputDirectory $audioRoot -DiagnosticCheck audio -DurationSeconds 3 `
            -FixturePath $passFixture) -join [Environment]::NewLine
    $audio = $audioText | ConvertFrom-Json
    Assert-True ([string]$audio.automatedResult -eq 'PASS' -and
        [bool]$audio.checks.audio.manualVerificationPending -and
        -not [bool]$audio.hardwareVerified) `
        'Audible output was incorrectly reported as physically verified.'

    $failedRoot = Join-Path $testRoot 'failed-sd-fixture'
    & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $capture `
        -Port COM5 -Baud 115200 `
        -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb `
        -OutputDirectory $failedRoot -DiagnosticCheck sd -DurationSeconds 3 `
        -FixturePath $failFixture | Out-Null
    Assert-True ($LASTEXITCODE -ne 0) `
        'A firmware FAIL record incorrectly returned a successful diagnostic process.'
    $failed = Get-Content -LiteralPath `
        (Join-Path $failedRoot 'hardware-diagnostic-result.json') -Raw | ConvertFrom-Json
    Assert-True ([string]$failed.automatedResult -eq 'FAIL' -and
        [string]$failed.checks.sd.automatedResult -eq 'FAIL' -and
        -not [bool]$failed.hardwareVerified) `
        'A firmware FAIL record was hidden or promoted to physical PASS.'
}
finally {
    if (Test-Path -LiteralPath $testRoot) {
        $resolvedTestRoot = [System.IO.Path]::GetFullPath($testRoot)
        $resolvedOutRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot 'out'))
        if (-not $resolvedTestRoot.StartsWith(
                $resolvedOutRoot + [System.IO.Path]::DirectorySeparatorChar,
                [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove hardware diagnostic test path outside out/: $resolvedTestRoot"
        }
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}

Write-Host 'FabGL Studio bounded hardware diagnostic contract passed.'
