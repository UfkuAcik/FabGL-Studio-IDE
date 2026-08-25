[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$outRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot 'out'))
$fixture = [System.IO.Path]::GetFullPath((Join-Path $outRoot (
            'source-package-contract-' + [Guid]::NewGuid().ToString('N'))))
$outPrefix = $outRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $fixture.StartsWith($outPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe source package test fixture: $fixture"
}

function Invoke-Git {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    & git.exe @Arguments | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "git fixture command failed: $($Arguments -join ' ')"
    }
}

try {
    New-Item -ItemType Directory -Path $fixture | Out-Null
    Set-Content -LiteralPath (Join-Path $fixture 'CMakeLists.txt') -Encoding UTF8 -Value @'
cmake_minimum_required(VERSION 3.24)
set(FGL_PROJECT_VERSION "0.1.0")
project(FabGLStudio VERSION ${FGL_PROJECT_VERSION} LANGUAGES NONE)
'@
    Set-Content -LiteralPath (Join-Path $fixture '.gitignore') -Encoding UTF8 -Value "out/`n"
    Set-Content -LiteralPath (Join-Path $fixture 'tracked.txt') -Encoding UTF8 -Value "committed`n"
    Invoke-Git @('-C', $fixture, 'init')
    Invoke-Git @('-C', $fixture, 'config', 'user.email', 'source-package-test@example.invalid')
    Invoke-Git @('-C', $fixture, 'config', 'user.name', 'Source Package Test')
    Invoke-Git @('-C', $fixture, 'add', '--', '.gitignore', 'CMakeLists.txt', 'tracked.txt')
    Invoke-Git @('-C', $fixture, 'commit', '-m', 'fixture')

    Set-Content -LiteralPath (Join-Path $fixture 'tracked.txt') -Encoding UTF8 -Value "working tree`n"
    Set-Content -LiteralPath (Join-Path $fixture 'untracked.txt') -Encoding UTF8 -Value "included`n"
    $packageScript = Join-Path $repositoryRoot 'packaging\build-source-package.ps1'
    $packageDirectory = Join-Path $fixture 'out\working'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $packageScript `
        -Version 0.1.0 -SourceRepository $fixture -OutputDirectory $packageDirectory `
        -IncludeWorkingTree | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw 'Explicit working-tree source package creation failed.'
    }

    $archive = Join-Path $packageDirectory 'FabGL-Studio-0.1.0-source.zip'
    $checksum = "$archive.sha256"
    foreach ($artifact in @($archive, $checksum)) {
        if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
            throw "Source package artifact is missing: $artifact"
        }
    }
    $expectedHash = (Get-Content -LiteralPath $checksum -Raw).Trim().Split()[0]
    $actualHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($expectedHash -ne $actualHash) {
        throw 'Source package checksum does not match the working-tree archive.'
    }

    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $prefix = 'FabGL-Studio-0.1.0-source/'
        $entries = @{}
        foreach ($entry in $zip.Entries) {
            $entries[$entry.FullName] = $entry
            if ($entry.FullName -like "${prefix}.git/*" -or
                $entry.FullName -like "${prefix}out/*") {
                throw "Source archive leaked excluded state: $($entry.FullName)"
            }
        }
        foreach ($required in @('tracked.txt', 'untracked.txt', 'SOURCE_PACKAGE.json')) {
            if (-not $entries.ContainsKey("$prefix$required")) {
                throw "Source archive omitted $required."
            }
        }
        $reader = [System.IO.StreamReader]::new($entries["${prefix}tracked.txt"].Open())
        try { $tracked = $reader.ReadToEnd() } finally { $reader.Dispose() }
        if ($tracked.Trim() -ne 'working tree') {
            throw 'Source archive used HEAD instead of the modified working-tree file.'
        }
        $reader = [System.IO.StreamReader]::new($entries["${prefix}untracked.txt"].Open())
        try { $untracked = $reader.ReadToEnd() } finally { $reader.Dispose() }
        if ($untracked.Trim() -ne 'included') {
            throw 'Source archive did not preserve the untracked source file.'
        }
        $reader = [System.IO.StreamReader]::new($entries["${prefix}SOURCE_PACKAGE.json"].Open())
        try { $metadata = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
        if (-not [bool]$metadata.workingTreeIncluded -or -not [bool]$metadata.sourceDirty) {
            throw 'Source package metadata did not disclose its dirty working-tree snapshot.'
        }
    }
    finally {
        $zip.Dispose()
    }

    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'SilentlyContinue'
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $packageScript `
            -Version 0.1.0 -SourceRepository $fixture `
            -OutputDirectory (Join-Path $fixture 'out\head-only') 2>&1 | Out-Null
        $dirtyExitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($dirtyExitCode -eq 0) {
        throw 'HEAD-only source packaging accepted a dirty source tree.'
    }
    Write-Host 'Source package working-tree and clean-release contracts passed.'
}
finally {
    if (Test-Path -LiteralPath $fixture) {
        $resolved = [System.IO.Path]::GetFullPath($fixture)
        if (-not $resolved.StartsWith($outPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing unsafe source package fixture cleanup: $resolved"
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
