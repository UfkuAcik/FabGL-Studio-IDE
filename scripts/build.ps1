[CmdletBinding()]
param(
    [ValidateSet('dev', 'release', 'core-only')]
    [string]$Preset = 'dev',
    [switch]$Package
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Push-Location $repositoryRoot
try {
    & cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }

    & cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }

    & ctest --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "Tests failed with exit code $LASTEXITCODE" }

    if ($Package) {
        if ($Preset -ne 'release') {
            throw 'Packaging is supported only with -Preset release.'
        }
        $packageDirectory = Join-Path $repositoryRoot 'out\packages'
        New-Item -ItemType Directory -Force -Path $packageDirectory | Out-Null
        & cpack --config (Join-Path $repositoryRoot 'out\build\release\CPackConfig.cmake') -B $packageDirectory
        if ($LASTEXITCODE -ne 0) { throw "Packaging failed with exit code $LASTEXITCODE" }
    }
}
finally {
    Pop-Location
}
