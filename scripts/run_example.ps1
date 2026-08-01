[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Project,
    [switch]$Headless,
    [string]$Output
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$projectPath = (Resolve-Path $Project).Path
$manifest = Get-Content -Raw -LiteralPath $projectPath -Encoding UTF8 | ConvertFrom-Json
if ($manifest.kind -ne 'FabGLStudioProject' -or $manifest.formatVersion -ne 1) {
    throw 'The selected file is not a supported FabGL Studio project.'
}

$demo = [string]$manifest.previewDemo
$supported = @('empty', '2d', 'platformer', 'topdown', 'top-down', 'raycast', 'fps',
               'racer', 'lowpoly', 'tps', 'ui', 'audio', 'animation', 'streaming')
if ($supported -notcontains $demo) {
    throw "Project previewDemo '$demo' is not available in this player build."
}

$player = Join-Path $repositoryRoot 'out\build\dev\apps\player_pc\fabgl_player_pc.exe'
if (-not (Test-Path -LiteralPath $player)) {
    & (Join-Path $repositoryRoot 'scripts\build.ps1') -Preset dev
    if ($LASTEXITCODE -ne 0) { throw 'Player build failed.' }
}

$arguments = @('--demo', $demo)
if ($Headless) { $arguments += @('--headless') }
if ($Output) { $arguments += @('--output', $Output) }
& $player @arguments
exit $LASTEXITCODE
