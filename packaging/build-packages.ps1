[CmdletBinding()]
param(
    [ValidateSet('release', 'dev')]
    [string]$Preset = 'release',
    [switch]$SkipBuild,
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildDirectory = Join-Path $repositoryRoot "out\build\$Preset"
$installDirectory = Join-Path $repositoryRoot "out\install\$Preset"
$packageDirectory = Join-Path $repositoryRoot 'out\packages'

if (-not $SkipBuild) {
    & cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    & cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
}

if (-not $SkipTests) {
    & ctest --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed; refusing to package.' }
}

if (-not (Test-Path -LiteralPath (Join-Path $buildDirectory 'CPackConfig.cmake'))) {
    throw "CPack configuration is missing: $buildDirectory"
}

& cmake --install $buildDirectory --prefix $installDirectory
if ($LASTEXITCODE -ne 0) { throw 'Install staging failed.' }

New-Item -ItemType Directory -Force -Path $packageDirectory | Out-Null
& cpack --config (Join-Path $buildDirectory 'CPackConfig.cmake') -G ZIP -B $packageDirectory
if ($LASTEXITCODE -ne 0) { throw 'Portable ZIP generation failed.' }

$makeNsis = Get-Command makensis -ErrorAction SilentlyContinue
if ($null -ne $makeNsis) {
    & cpack --config (Join-Path $buildDirectory 'CPackConfig.cmake') -G NSIS -B $packageDirectory
    if ($LASTEXITCODE -ne 0) { throw 'NSIS package generation failed.' }
} else {
    Write-Host 'NSIS not found; portable ZIP was generated and installer was skipped.'
}

Get-ChildItem -LiteralPath $packageDirectory -File |
    Where-Object { $_.Extension -in @('.zip', '.exe') } |
    Select-Object Name, Length, LastWriteTime

