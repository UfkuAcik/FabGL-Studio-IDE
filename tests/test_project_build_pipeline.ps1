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

function Invoke-ExpectedFailure {
    param(
        [Parameter(Mandatory = $true)][scriptblock]$Action,
        [Parameter(Mandatory = $true)][string]$Description
    )
    try {
        & $Action
    }
    catch {
        return
    }
    throw "Expected failure did not occur: $Description"
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$pipeline = Join-Path $repositoryRoot 'scripts\build_project.ps1'
$project = Join-Path $repositoryRoot 'examples\empty\Empty.fglproject'

$pcText = @(& $pipeline -ProjectPath $project -Target Pc -DryRun) -join [Environment]::NewLine
$pc = $pcText | ConvertFrom-Json
Assert-True ($pc.schemaVersion -eq 1) 'PC plan schema mismatch.'
Assert-True ([string]$pc.kind -eq 'FabGLStudioProjectBuildResult') 'PC plan kind mismatch.'
Assert-True ([bool]$pc.success -and [bool]$pc.dryRun) 'PC dry-run must succeed.'
Assert-True ([string]$pc.target -eq 'Pc') 'PC target mismatch.'
Assert-True ([int]$pc.pc.frames -eq 2 -and [bool]$pc.pc.headless) `
    'PC dry-run must plan a bounded headless smoke test.'
Assert-True ([bool]$pc.assetPipeline.planned -and
    [string]$pc.assetPipeline.target -eq 'pc') `
    'PC dry-run must plan source import, optimization, visual compilation, and asset packing.'
Assert-True ($null -eq $pc.upload) 'PC dry-run cannot contain an upload plan.'

$esp32Text = @(& $pipeline -ProjectPath $project -Target Esp32 -Upload -RuntimeDiagnostics `
        -Port COM5 -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb -SoakDiagnostics `
        -DiagnosticCheck frame-rate -DiagnosticDurationSeconds 9 -DryRun) `
    -join [Environment]::NewLine
$esp32 = $esp32Text | ConvertFrom-Json
Assert-True ([bool]$esp32.success -and [bool]$esp32.dryRun) 'ESP32 dry-run must succeed.'
Assert-True ([string]$esp32.target -eq 'Esp32') 'ESP32 target mismatch.'
Assert-True ([string]$esp32.esp32.profile -eq 'Release') 'ESP32 profile mismatch.'
Assert-True ([bool]$esp32.upload.requested -and -not [bool]$esp32.upload.performed) `
    'Dry-run upload must be explicit and must not execute.'
Assert-True ([string]$esp32.upload.port -eq 'COM5') 'Dry-run upload port mismatch.'
Assert-True ([string]$esp32.upload.boardProfile -eq 'olimex-esp32-sbc-fabgl-revb') `
    'Dry-run board confirmation mismatch.'
Assert-True ([bool]$esp32.assetPipeline.planned -and
    [string]$esp32.assetPipeline.target -eq 'esp32') `
    'ESP32 dry-run must plan the target-specific asset pipeline.'
Assert-True ([bool]$esp32.esp32.soakDiagnostics) `
    'ESP32 soak diagnostics must remain an explicit build-plan property.'
Assert-True ([bool]$esp32.portDetection.planned -and [bool]$esp32.portDetection.readOnly) `
    'ESP32 upload must plan read-only port detection before opening the explicit port.'
Assert-True ([bool]$esp32.monitor.requested -and [bool]$esp32.monitor.bounded -and
    -not [bool]$esp32.monitor.performed) `
    'Runtime diagnostics must plan a bounded post-upload serial monitor without opening it in dry-run.'
Assert-True ([bool]$esp32.runtimeDiagnostics.requested -and
    -not [bool]$esp32.runtimeDiagnostics.performed -and
    [string]$esp32.runtimeDiagnostics.diagnosticCheck -eq 'frame-rate' -and
    [int]$esp32.runtimeDiagnostics.durationSeconds -eq 9 -and
    -not [bool]$esp32.runtimeDiagnostics.fixtureMode) `
    'ESP32 runtime diagnostic plan mismatch.'

Invoke-ExpectedFailure -Description 'PC target upload' -Action {
    & $pipeline -ProjectPath $project -Target Pc -Upload -Port COM5 `
        -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb -DryRun | Out-Null
}
Invoke-ExpectedFailure -Description 'upload without an explicit port' -Action {
    & $pipeline -ProjectPath $project -Target Esp32 -Upload `
        -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb -DryRun | Out-Null
}
Invoke-ExpectedFailure -Description 'port without an upload or monitor action' -Action {
    & $pipeline -ProjectPath $project -Target Esp32 -Port COM5 `
        -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb -DryRun | Out-Null
}
Invoke-ExpectedFailure -Description 'runtime diagnostics without guarded upload' -Action {
    & $pipeline -ProjectPath $project -Target Esp32 -RuntimeDiagnostics -Port COM5 `
        -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb -DryRun | Out-Null
}

$reparseNonce = [Guid]::NewGuid().ToString('N')
$reparseTarget = Join-Path $repositoryRoot "out\project-pipeline-reparse-target-$reparseNonce"
$reparseLink = Join-Path $repositoryRoot "out\project-pipeline-reparse-link-$reparseNonce"
New-Item -ItemType Directory -Path $reparseTarget -Force | Out-Null
try {
    New-Item -ItemType Junction -Path $reparseLink -Target $reparseTarget | Out-Null
    Invoke-ExpectedFailure -Description 'output through a filesystem reparse point' -Action {
        & $pipeline -ProjectPath $project -Target Pc `
            -OutputRoot (Join-Path $reparseLink 'nested') -DryRun | Out-Null
    }
}
finally {
    if (Test-Path -LiteralPath $reparseLink) {
        Remove-Item -LiteralPath $reparseLink -Force
    }
    if (Test-Path -LiteralPath $reparseTarget) {
        Remove-Item -LiteralPath $reparseTarget -Force
    }
}

Write-Host 'FabGL Studio unified project pipeline contract passed.'
