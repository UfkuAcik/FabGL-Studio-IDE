[CmdletBinding()]
param(
    [string]$ManifestPath,
    [switch]$Force,
    [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Assert-PathWithin {
    param(
        [Parameter(Mandatory = $true)][string]$Candidate,
        [Parameter(Mandatory = $true)][string]$Root,
        [string]$Description = 'path'
    )
    $candidateFull = Get-FullPath $Candidate
    $rootFull = (Get-FullPath $Root).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar)
    $prefix = $rootFull + [System.IO.Path]::DirectorySeparatorChar
    if ($candidateFull.Equals($rootFull, [System.StringComparison]::OrdinalIgnoreCase) -or
        -not $candidateFull.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description must be a child of the repository: $candidateFull"
    }
    return $candidateFull
}

function Get-NsisStatus {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)]$Lock
    )
    $issues = [System.Collections.Generic.List[string]]::new()
    foreach ($relative in @($Lock.requiredFiles)) {
        $candidate = Join-Path $Root ([string]$relative)
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            $issues.Add("required NSIS file is missing: $relative")
        }
    }
    $maker = Join-Path $Root 'makensis.exe'
    $version = $null
    if ($issues.Count -eq 0) {
        $versionOutput = @(& $maker /VERSION 2>&1)
        if ($LASTEXITCODE -ne 0 -or $versionOutput.Count -eq 0) {
            $issues.Add('makensis version probe failed')
        } else {
            $version = $versionOutput[0].ToString().Trim().TrimStart('v')
            if ($version -ne [string]$Lock.version) {
                $issues.Add("NSIS version mismatch: expected $($Lock.version), found $version")
            }
        }
    }
    return [pscustomobject]@{
        valid = $issues.Count -eq 0
        issues = @($issues)
        version = $version
        root = $Root
        makensis = $maker
    }
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $repositoryRoot 'toolchains\desktop-manifest.json'
} elseif (-not [System.IO.Path]::IsPathRooted($ManifestPath)) {
    $ManifestPath = Join-Path $repositoryRoot $ManifestPath
}
$ManifestPath = Assert-PathWithin -Candidate $ManifestPath -Root $repositoryRoot `
    -Description 'desktop manifest'
if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
    throw "Desktop manifest was not found: $ManifestPath"
}
$manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
$lock = $manifest.packaging.nsis
if ([int]$manifest.schemaVersion -ne 1 -or [string]$lock.version -ne '3.12' -or
    [string]$lock.sha256 -notmatch '^[0-9a-f]{64}$') {
    throw 'Unsupported or incomplete NSIS lock in the desktop manifest.'
}
$source = [System.Uri]([string]$lock.url)
if ($source.Scheme -ne 'https') {
    throw 'The NSIS source must use HTTPS.'
}
foreach ($relative in @([string]$lock.installDirectory) + @($lock.requiredFiles)) {
    if ([string]::IsNullOrWhiteSpace($relative) -or
        [System.IO.Path]::IsPathRooted($relative) -or
        $relative.Replace('\', '/').Split('/') -contains '..') {
        throw "Unsafe relative NSIS path in desktop manifest: $relative"
    }
}

$toolchainsRoot = Assert-PathWithin -Candidate (Join-Path $repositoryRoot '.toolchains') `
    -Root $repositoryRoot -Description 'toolchains root'
$cacheRoot = Assert-PathWithin -Candidate (Join-Path $repositoryRoot '.downloads\desktop') `
    -Root $repositoryRoot -Description 'desktop cache root'
$destination = Assert-PathWithin `
    -Candidate (Join-Path $repositoryRoot ([string]$lock.installDirectory)) `
    -Root $repositoryRoot -Description 'NSIS installation'
$status = Get-NsisStatus -Root $destination -Lock $lock
$needsInstall = -not $status.valid -or $Force

if ($DryRun) {
    [ordered]@{
        schemaVersion = 1
        dryRun = $true
        version = [string]$lock.version
        source = [string]$lock.url
        destination = $destination
        valid = [bool]$status.valid
        issues = @($status.issues)
        wouldInstall = [bool]$needsInstall
        hashVerificationDisabled = $false
    } | ConvertTo-Json -Depth 5
    return
}

if ($needsInstall) {
    New-Item -ItemType Directory -Path $toolchainsRoot,$cacheRoot -Force | Out-Null
    $installer = Assert-PathWithin `
        -Candidate (Join-Path $cacheRoot "nsis-$($lock.version)-setup.exe") `
        -Root $repositoryRoot -Description 'NSIS installer cache'
    $validCache = Test-Path -LiteralPath $installer -PathType Leaf
    if ($validCache) {
        $validCache = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant() `
            -eq [string]$lock.sha256
    }
    if (-not $validCache) {
        $partial = Assert-PathWithin -Candidate "$installer.part" -Root $repositoryRoot `
            -Description 'NSIS partial download'
        if (Test-Path -LiteralPath $partial) {
            Remove-Item -LiteralPath $partial -Force
        }
        Invoke-WebRequest -UseBasicParsing -UserAgent 'curl/8.0.1' -Uri $source -OutFile $partial
        $actualHash = (Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -ne [string]$lock.sha256) {
            Remove-Item -LiteralPath $partial -Force
            throw "NSIS installer checksum mismatch: $actualHash"
        }
        Move-Item -LiteralPath $partial -Destination $installer -Force
    }

    $staging = Assert-PathWithin `
        -Candidate (Join-Path $toolchainsRoot ('.nsis-staging-' + [guid]::NewGuid().ToString('N'))) `
        -Root $repositoryRoot -Description 'NSIS staging directory'
    try {
        $process = Start-Process -FilePath $installer -ArgumentList @('/S', "/D=$staging") `
            -Wait -PassThru -WindowStyle Hidden
        if ($process.ExitCode -ne 0) {
            throw "NSIS installer failed with exit code $($process.ExitCode)."
        }
        $stagedStatus = Get-NsisStatus -Root $staging -Lock $lock
        if (-not $stagedStatus.valid) {
            throw "Staged NSIS failed validation: $($stagedStatus.issues -join '; ')"
        }
        $backup = Assert-PathWithin -Candidate ($destination + '.backup-' +
            [guid]::NewGuid().ToString('N')) -Root $repositoryRoot -Description 'NSIS backup'
        $backupPresent = $false
        if (Test-Path -LiteralPath $destination) {
            Move-Item -LiteralPath $destination -Destination $backup
            $backupPresent = $true
        }
        try {
            Move-Item -LiteralPath $staging -Destination $destination
        } catch {
            if ($backupPresent -and -not (Test-Path -LiteralPath $destination)) {
                Move-Item -LiteralPath $backup -Destination $destination
                $backupPresent = $false
            }
            throw
        }
        if ($backupPresent -and (Test-Path -LiteralPath $backup)) {
            Remove-Item -LiteralPath $backup -Recurse -Force
        }
    } finally {
        if (Test-Path -LiteralPath $staging) {
            Remove-Item -LiteralPath $staging -Recurse -Force
        }
    }
}

$status = Get-NsisStatus -Root $destination -Lock $lock
if (-not $status.valid) {
    throw "NSIS validation failed: $($status.issues -join '; ')"
}
[ordered]@{
    schemaVersion = 1
    version = $status.version
    root = $status.root
    makensis = $status.makensis
    manifest = $ManifestPath
    manifestSha256 = (Get-FileHash -LiteralPath $ManifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    hashVerificationDisabled = $false
} | ConvertTo-Json -Depth 5
