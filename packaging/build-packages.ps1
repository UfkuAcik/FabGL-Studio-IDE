[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [ValidateRange(1, 64)]
    [int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount),
    [string]$PackageDirectory,
    [switch]$Clean,
    [switch]$RunGuiSmoke,
    [switch]$RequireInstaller
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$buildScript = Join-Path (Resolve-Path (Join-Path $PSScriptRoot '..')).Path `
    'scripts\build_desktop.ps1'
& $buildScript @PSBoundParameters
