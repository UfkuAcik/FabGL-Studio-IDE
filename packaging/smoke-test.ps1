[CmdletBinding()]
param(
    [ValidateSet('release', 'dev')]
    [string]$Preset = 'release'
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$stageRoot = Join-Path $repositoryRoot "out\install\$Preset"
$binaryRoot = Join-Path $stageRoot 'bin'

if (-not (Test-Path -LiteralPath $binaryRoot)) {
    throw "Install tree not found: $stageRoot. Run packaging/build-packages.ps1 first."
}

$programs = @('fabgl_player_pc', 'fabgl_project_cli', 'fabgl_asset_compiler')
foreach ($program in $programs) {
    $candidate = Join-Path $binaryRoot ($program + '.exe')
    if (-not (Test-Path -LiteralPath $candidate)) {
        $candidate = Join-Path $binaryRoot $program
    }
    if (-not (Test-Path -LiteralPath $candidate)) {
        throw "Required staged program is missing: $program"
    }
    & $candidate --help
    if ($LASTEXITCODE -ne 0) { throw "$program --help failed." }
}

$toolchainManager = Join-Path $binaryRoot 'fabgl_toolchain_manager.exe'
if (-not (Test-Path -LiteralPath $toolchainManager)) {
    $toolchainManager = Join-Path $binaryRoot 'fabgl_toolchain_manager'
}
if (-not (Test-Path -LiteralPath $toolchainManager)) {
    throw 'Required staged program is missing: fabgl_toolchain_manager'
}
$toolchainManifest =
    Join-Path $stageRoot 'share\fabgl-studio\toolchains\manifest.json'
& $toolchainManager inspect --manifest $toolchainManifest --repo $stageRoot
if ($LASTEXITCODE -notin @(0, 2)) {
    throw 'fabgl_toolchain_manager inspect failed unexpectedly.'
}

$projectCli = Join-Path $binaryRoot 'fabgl_project_cli.exe'
if (-not (Test-Path -LiteralPath $projectCli)) {
    $projectCli = Join-Path $binaryRoot 'fabgl_project_cli'
}
$example = Join-Path $stageRoot 'share\fabgl-studio\examples\empty\Empty.fglproject'
if (-not (Test-Path -LiteralPath $example)) {
    throw "Bundled example is missing: $example"
}
& $projectCli validate $example
if ($LASTEXITCODE -ne 0) { throw 'Bundled Empty example did not validate.' }

$studio = Join-Path $binaryRoot 'FabGLStudio.exe'
if (Test-Path -LiteralPath $studio) {
    Write-Host 'FabGLStudio.exe is staged; interactive launch remains a manual smoke check.'
} else {
    Write-Warning 'Qt editor was not part of this build; graphical smoke check skipped.'
}

Write-Host 'Staged command-line smoke tests passed.'
