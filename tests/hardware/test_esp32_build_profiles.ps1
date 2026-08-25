[CmdletBinding()]
param()

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

function Invoke-ProfilePlan {
    param(
        [Parameter(Mandatory = $true)][string]$Script,
        [Parameter(Mandatory = $true)][string]$Profile,
        [Parameter(Mandatory = $true)][string]$OutputRoot
    )

    $text = @(& $Script -DryRun -BuildProfile $Profile -OutputRoot $OutputRoot 2>&1) -join
        [Environment]::NewLine
    return $text | ConvertFrom-Json
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$buildScript = Join-Path $repositoryRoot 'scripts\build_esp32.ps1'
$outputRoot = Join-Path $repositoryRoot 'out\esp32-profile-contract-test'

$expected = [ordered]@{
    Debug = @('-Og', '-g3', 'FABGL_STUDIO_PROFILE_DEBUG=1', 'DebugLevel=debug')
    Release = @('-O2', '-g1', 'FABGL_STUDIO_PROFILE_RELEASE=1', 'DebugLevel=none')
    SizeOptimized = @('-Os', '-g0', 'FABGL_STUDIO_PROFILE_SIZE_OPTIMIZED=1', 'DebugLevel=none')
    PerformanceOptimized = @('-O3', '-funroll-loops',
        'FABGL_STUDIO_PROFILE_PERFORMANCE_OPTIMIZED=1', 'DebugLevel=none')
}
$plans = @{}
foreach ($profile in $expected.Keys) {
    $plan = Invoke-ProfilePlan -Script $buildScript -Profile $profile -OutputRoot $outputRoot
    $plans[$profile] = $plan
    Assert-True ($plan.schemaVersion -eq 1 -and $plan.success -and $plan.dryRun) `
        "$profile did not produce a successful dry-run plan."
    Assert-True ($plan.buildProfile -eq $profile) "$profile label was not preserved."
    Assert-True ($plan.partitionScheme -eq 'huge_app' -and
        [uint64]$plan.partitionAppBytes -eq 0x300000) `
        "$profile does not use the verified huge_app partition contract."
    Assert-True (-not $plan.exportPerformed -and -not $plan.compilePerformed -and
        -not $plan.uploadPerformed) "$profile dry-run performed a forbidden operation."
    foreach ($token in $expected[$profile]) {
        $haystack = "$($plan.compilerCppExtraFlags) $($plan.fqbn)"
        Assert-True ($haystack.Contains($token)) "$profile plan is missing '$token'."
    }
}

$uniqueCompilerFlags = @($plans.Values | ForEach-Object compilerCppExtraFlags |
        Sort-Object -Unique)
Assert-True ($uniqueCompilerFlags.Count -eq 4) `
    'ESP32 build profiles must produce four distinct compiler contracts.'

$projectPath = Join-Path $repositoryRoot 'examples\empty\Empty.fglproject'
$projectPlanText = @(& $buildScript -DryRun -BuildProfile Release -ProjectPath $projectPath `
        -OutputRoot $outputRoot 2>&1) -join [Environment]::NewLine
$projectPlan = $projectPlanText | ConvertFrom-Json
Assert-True ($projectPlan.project -eq $projectPath) 'ProjectPath was not preserved in the plan.'
Assert-True ($projectPlan.sketch -like '*\out\esp32-profile-contract-test\staged\Empty') `
    'ProjectPath did not map to the isolated staged sketch directory.'
Assert-True (-not (Test-Path -LiteralPath $projectPlan.sketch)) `
    'ProjectPath dry-run unexpectedly created the staged sketch.'

$soakPlanText = @(& $buildScript -DryRun -BuildProfile Release -ProjectPath $projectPath `
        -SoakDiagnostics -OutputRoot $outputRoot 2>&1) -join [Environment]::NewLine
$soakPlan = $soakPlanText | ConvertFrom-Json
Assert-True ([bool]$soakPlan.soakDiagnostics) 'Soak diagnostic plan was not labelled.'
Assert-True ([string]$soakPlan.compilerCppExtraFlags -like
    '*-DFABGL_STUDIO_SOAK_DIAGNOSTICS=1*') `
    'Soak diagnostic plan did not enable the bounded firmware workload.'

$payloadRequired = $false
try {
    & $buildScript -DryRun -SoakDiagnostics -OutputRoot $outputRoot 2>&1 | Out-Null
}
catch {
    $payloadRequired = $true
}
Assert-True $payloadRequired 'Soak diagnostics accepted a build without a project payload.'

$outsideRoot = Join-Path (Split-Path -Parent $repositoryRoot) 'unsafe-esp32-output'
$outsideRejected = $false
try {
    & $buildScript -DryRun -OutputRoot $outsideRoot 2>&1 | Out-Null
}
catch {
    $outsideRejected = $true
}
Assert-True $outsideRejected 'Output traversal outside the repository was not rejected.'

$rootRejected = $false
try {
    & $buildScript -DryRun -OutputRoot $repositoryRoot 2>&1 | Out-Null
}
catch {
    $rootRejected = $true
}
Assert-True $rootRejected 'Using the repository root as destructive build output was not rejected.'

$reparseTarget = Join-Path $repositoryRoot 'out\esp32-profile-reparse-target'
$reparseLink = Join-Path $repositoryRoot 'out\esp32-profile-reparse-link'
New-Item -ItemType Directory -Path $reparseTarget -Force | Out-Null
try {
    New-Item -ItemType Junction -Path $reparseLink -Target $reparseTarget | Out-Null
    $reparseRejected = $false
    try {
        & $buildScript -DryRun -OutputRoot (Join-Path $reparseLink 'nested') 2>&1 | Out-Null
    }
    catch {
        $reparseRejected = $true
    }
    Assert-True $reparseRejected 'Output through a filesystem reparse point was not rejected.'
}
finally {
    if (Test-Path -LiteralPath $reparseLink) {
        Remove-Item -LiteralPath $reparseLink -Force
    }
    if (Test-Path -LiteralPath $reparseTarget) {
        Remove-Item -LiteralPath $reparseTarget -Force
    }
}

$mutualExclusionRejected = $false
try {
    & $buildScript -DryRun -ProjectPath $projectPath `
        -SketchDirectory (Join-Path $repositoryRoot 'platforms\fabgl\firmware') `
        -OutputRoot $outputRoot 2>&1 | Out-Null
}
catch {
    $mutualExclusionRejected = $true
}
Assert-True $mutualExclusionRejected '-ProjectPath and -SketchDirectory were not exclusive.'

Write-Host 'ESP32 build profile and dry-run safety tests passed.'
