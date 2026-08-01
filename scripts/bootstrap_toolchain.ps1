[CmdletBinding()]
param(
    [string]$ManifestPath,
    [string]$CacheRoot,
    [string]$InstallRoot,
    [string]$OfflineSourceDirectory,
    [switch]$Force,
    [switch]$SkipBoardManagerInstall
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $Path))
}

function Assert-PathWithin {
    param(
        [Parameter(Mandatory = $true)][string]$Candidate,
        [Parameter(Mandatory = $true)][string]$Root,
        [string]$Description = 'path'
    )

    $candidateFull = Get-FullPath $Candidate
    $rootFull = (Get-FullPath $Root).TrimEnd([System.IO.Path]::DirectorySeparatorChar,
                                             [System.IO.Path]::AltDirectorySeparatorChar)
    $prefix = $rootFull + [System.IO.Path]::DirectorySeparatorChar
    if (-not $candidateFull.Equals($rootFull, [System.StringComparison]::OrdinalIgnoreCase) -and
        -not $candidateFull.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description must stay inside the repository: $candidateFull"
    }
    return $candidateFull
}

function Test-ArtifactFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)]$Artifact,
        [switch]$Quiet
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $false
    }
    $item = Get-Item -LiteralPath $Path
    if ([uint64]$item.Length -ne [uint64]$Artifact.size) {
        if (-not $Quiet) {
            Write-Warning "Size mismatch for $($Artifact.id): expected $($Artifact.size), got $($item.Length)."
        }
        return $false
    }
    $actualHash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $Artifact.sha256.ToLowerInvariant()) {
        if (-not $Quiet) {
            Write-Warning "SHA-256 mismatch for $($Artifact.id)."
        }
        return $false
    }
    return $true
}

function Get-LockedArchive {
    param(
        [Parameter(Mandatory = $true)]$Artifact,
        [Parameter(Mandatory = $true)][string]$CacheDirectory,
        [string]$OfflineDirectory
    )

    $cachePath = Join-Path $CacheDirectory $Artifact.fileName
    if (Test-ArtifactFile -Path $cachePath -Artifact $Artifact -Quiet) {
        Write-Host "Verified cached artifact: $($Artifact.id)"
        return $cachePath
    }

    if (Test-Path -LiteralPath $cachePath) {
        $cachePath = Assert-PathWithin -Candidate $cachePath -Root $repositoryRoot -Description 'invalid cache file'
        Remove-Item -LiteralPath $cachePath -Force
    }

    # One stable .part name makes an interrupted transfer recoverable on the
    # next run. A verified file is promoted with an atomic rename only.
    $partPath = "$cachePath.part"
    if (Test-Path -LiteralPath $partPath) {
        $partPath = Assert-PathWithin -Candidate $partPath -Root $repositoryRoot -Description 'partial download'
        Remove-Item -LiteralPath $partPath -Force
    }

    try {
        if ($OfflineDirectory) {
            $offlinePath = Join-Path $OfflineDirectory $Artifact.fileName
            if (-not (Test-Path -LiteralPath $offlinePath -PathType Leaf)) {
                throw "Offline artifact is missing: $offlinePath"
            }
            Copy-Item -LiteralPath $offlinePath -Destination $partPath
        }
        else {
            $uri = [System.Uri]$Artifact.url
            if ($uri.Scheme -ne 'https') {
                throw "Only HTTPS artifact URLs are accepted: $($Artifact.url)"
            }
            Write-Host "Downloading $($Artifact.id) from $($Artifact.url)"
            Invoke-WebRequest -Uri $uri -OutFile $partPath -UseBasicParsing
        }

        if (-not (Test-ArtifactFile -Path $partPath -Artifact $Artifact)) {
            throw "Integrity verification failed for $($Artifact.id). The file will not be installed."
        }
        Move-Item -LiteralPath $partPath -Destination $cachePath
    }
    finally {
        if (Test-Path -LiteralPath $partPath) {
            Remove-Item -LiteralPath $partPath -Force
        }
    }

    return $cachePath
}

function Test-ZipEntrySafe {
    param(
        [Parameter(Mandatory = $true)]$Entry,
        [Parameter(Mandatory = $true)][string]$StagingRoot
    )

    $entryName = $Entry.FullName.Replace('\', '/')
    if ([string]::IsNullOrWhiteSpace($entryName)) {
        return $false
    }
    if ($entryName.StartsWith('/') -or $entryName -match '^[A-Za-z]:' -or
        [System.IO.Path]::IsPathRooted($entryName)) {
        return $false
    }
    $segments = $entryName.Split('/')
    if ($segments -contains '..') {
        return $false
    }

    # Unix file type 0xA000 is a symbolic link. Links are not needed by the
    # pinned Windows ZIPs and are rejected before extraction.
    $unixType = (($Entry.ExternalAttributes -shr 16) -band 0xF000)
    if ($unixType -eq 0xA000) {
        return $false
    }

    $target = [System.IO.Path]::GetFullPath((Join-Path $StagingRoot $entryName))
    $root = [System.IO.Path]::GetFullPath($StagingRoot).TrimEnd([System.IO.Path]::DirectorySeparatorChar) +
        [System.IO.Path]::DirectorySeparatorChar
    return $target.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)
}

function Install-LockedZip {
    param(
        [Parameter(Mandatory = $true)]$Artifact,
        [Parameter(Mandatory = $true)][string]$ArchivePath,
        [Parameter(Mandatory = $true)][string]$ToolchainRoot,
        [switch]$Replace
    )

    $destination = Assert-PathWithin -Candidate (Join-Path $ToolchainRoot $Artifact.installDirectory) `
        -Root $repositoryRoot -Description 'toolchain destination'
    $markerPath = Join-Path $destination '.fabglstudio-install.json'
    if ((Test-Path -LiteralPath $markerPath -PathType Leaf) -and -not $Replace) {
        $marker = Get-Content -LiteralPath $markerPath -Raw | ConvertFrom-Json
        if ($marker.id -eq $Artifact.id -and $marker.version -eq $Artifact.version -and
            $marker.sha256 -eq $Artifact.sha256) {
            Write-Host "Already installed: $($Artifact.id)"
            return $destination
        }
        throw "Install marker does not match the lock for $($Artifact.id): $destination"
    }
    if ((Test-Path -LiteralPath $destination) -and -not $Replace) {
        throw "Unmanaged destination already exists; use -Force only after reviewing it: $destination"
    }

    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $destinationParent = Split-Path -Parent $destination
    New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null
    $stagingRoot = Join-Path $destinationParent ('.staging-' + [guid]::NewGuid().ToString('N'))
    $backupPath = $null
    New-Item -ItemType Directory -Path $stagingRoot | Out-Null

    try {
        # Keep the verified archive exclusively open from validation through
        # extraction so another process cannot replace it between the two.
        $archiveStream = [System.IO.File]::Open(
            $ArchivePath,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::None)
        $zip = [System.IO.Compression.ZipArchive]::new(
            $archiveStream,
            [System.IO.Compression.ZipArchiveMode]::Read,
            $false)
        try {
            foreach ($entry in $zip.Entries) {
                if (-not (Test-ZipEntrySafe -Entry $entry -StagingRoot $stagingRoot)) {
                    throw "Unsafe ZIP entry rejected in $($Artifact.id): $($entry.FullName)"
                }
            }

            $createdDirectories = New-Object 'System.Collections.Generic.HashSet[string]' `
                ([System.StringComparer]::OrdinalIgnoreCase)
            [void]$createdDirectories.Add([System.IO.Path]::GetFullPath($stagingRoot))
            foreach ($entry in $zip.Entries) {
                $entryName = $entry.FullName.Replace('/', [System.IO.Path]::DirectorySeparatorChar)
                $targetPath = [System.IO.Path]::GetFullPath((Join-Path $stagingRoot $entryName))
                if ([string]::IsNullOrEmpty($entry.Name)) {
                    if ($createdDirectories.Add($targetPath)) {
                        [void][System.IO.Directory]::CreateDirectory($targetPath)
                    }
                    continue
                }
                $parent = Split-Path -Parent $targetPath
                if ($createdDirectories.Add($parent)) {
                    [void][System.IO.Directory]::CreateDirectory($parent)
                }
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile(
                    $entry, $targetPath, $false)
            }
        }
        finally {
            $zip.Dispose()
            $archiveStream.Dispose()
        }

        $payloadRoot = $stagingRoot
        if ([bool]$Artifact.stripSingleRoot) {
            $children = @(Get-ChildItem -LiteralPath $stagingRoot -Force)
            if ($children.Count -ne 1 -or -not $children[0].PSIsContainer) {
                throw "Expected one archive root directory for $($Artifact.id)."
            }
            $payloadRoot = $children[0].FullName
        }

        $marker = [ordered]@{
            schemaVersion = 1
            id = $Artifact.id
            version = $Artifact.version
            commit = $Artifact.commit
            sha256 = $Artifact.sha256
            installedUtc = [DateTime]::UtcNow.ToString('o')
        }
        $marker | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $payloadRoot '.fabglstudio-install.json') -Encoding UTF8

        if (Test-Path -LiteralPath $destination) {
            $destination = Assert-PathWithin -Candidate $destination -Root $ToolchainRoot -Description 'existing toolchain destination'
            $backupPath = "$destination.backup-$([guid]::NewGuid().ToString('N'))"
            Move-Item -LiteralPath $destination -Destination $backupPath
        }

        try {
            Move-Item -LiteralPath $payloadRoot -Destination $destination
        }
        catch {
            if ($backupPath -and (Test-Path -LiteralPath $backupPath) -and
                -not (Test-Path -LiteralPath $destination)) {
                Move-Item -LiteralPath $backupPath -Destination $destination
                $backupPath = $null
            }
            throw
        }

        if ($backupPath -and (Test-Path -LiteralPath $backupPath)) {
            $backupPath = Assert-PathWithin -Candidate $backupPath -Root $ToolchainRoot -Description 'toolchain backup'
            Remove-Item -LiteralPath $backupPath -Recurse -Force
            $backupPath = $null
        }
        Write-Host "Installed $($Artifact.id) atomically at $destination"
        return $destination
    }
    finally {
        if (Test-Path -LiteralPath $stagingRoot) {
            $stagingRoot = Assert-PathWithin -Candidate $stagingRoot -Root $ToolchainRoot -Description 'toolchain staging directory'
            Remove-Item -LiteralPath $stagingRoot -Recurse -Force
        }
    }
}

function ConvertTo-YamlPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return "'" + ((Get-FullPath $Path).Replace('\', '/').Replace("'", "''")) + "'"
}

function Write-ArduinoConfiguration {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$DataDirectory,
        [Parameter(Mandatory = $true)][string]$DownloadsDirectory,
        [Parameter(Mandatory = $true)][string]$UserDirectory,
        [Parameter(Mandatory = $true)][string]$IndexUrl
    )

    $content = @(
        'board_manager:'
        '  additional_urls:'
        "    - $IndexUrl"
        '  enable_unsafe_install: false'
        'directories:'
        "  data: $(ConvertTo-YamlPath $DataDirectory)"
        "  downloads: $(ConvertTo-YamlPath $DownloadsDirectory)"
        "  user: $(ConvertTo-YamlPath $UserDirectory)"
        'library:'
        '  enable_unsafe_install: false'
    ) -join [Environment]::NewLine
    Set-Content -LiteralPath $Path -Value $content -Encoding UTF8
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $ManifestPath) {
    $ManifestPath = Join-Path $repositoryRoot 'toolchains\manifest.json'
}
$ManifestPath = Get-FullPath $ManifestPath
if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
    throw "Toolchain manifest not found: $ManifestPath"
}
$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1) {
    throw "Unsupported toolchain manifest schema: $($manifest.schemaVersion)"
}

if (-not $CacheRoot) {
    $CacheRoot = Join-Path $repositoryRoot $manifest.storage.cacheDirectory
}
if (-not $InstallRoot) {
    $InstallRoot = Join-Path $repositoryRoot $manifest.storage.installDirectory
}
$CacheRoot = Assert-PathWithin -Candidate $CacheRoot -Root $repositoryRoot -Description 'cache root'
$InstallRoot = Assert-PathWithin -Candidate $InstallRoot -Root $repositoryRoot -Description 'install root'
if ($OfflineSourceDirectory) {
    $OfflineSourceDirectory = Get-FullPath $OfflineSourceDirectory
    if (-not (Test-Path -LiteralPath $OfflineSourceDirectory -PathType Container)) {
        throw "Offline source directory not found: $OfflineSourceDirectory"
    }
}

if ($env:OS -ne 'Windows_NT') {
    throw 'This lock currently contains an Arduino CLI binary only for Windows x86-64.'
}
if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString() -ne 'X64') {
    throw 'This lock currently supports only Windows x86-64.'
}

$artifacts = @($manifest.artifacts | Where-Object {
    $_.platform -eq 'all' -or $_.platform -eq 'windows-x86_64'
})
foreach ($artifact in $artifacts) {
    if ([string]::IsNullOrWhiteSpace($artifact.id) -or
        [string]::IsNullOrWhiteSpace($artifact.version) -or
        [string]::IsNullOrWhiteSpace($artifact.commit) -or
        [string]::IsNullOrWhiteSpace($artifact.url) -or
        [string]::IsNullOrWhiteSpace($artifact.fileName) -or
        [uint64]$artifact.size -eq 0 -or
        $artifact.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or
        [string]::IsNullOrWhiteSpace($artifact.license)) {
        throw "Artifact lock is incomplete: $($artifact.id)"
    }
}

New-Item -ItemType Directory -Path $CacheRoot -Force | Out-Null
New-Item -ItemType Directory -Path $InstallRoot -Force | Out-Null
$missingArchiveBytes = [uint64]0
$missingInstallEstimate = [uint64]0
foreach ($artifact in $artifacts) {
    $cached = Join-Path $CacheRoot $artifact.fileName
    if (-not (Test-ArtifactFile -Path $cached -Artifact $artifact -Quiet)) {
        $missingArchiveBytes += [uint64]$artifact.size
    }
    $destination = Join-Path $InstallRoot $artifact.installDirectory
    $markerPath = Join-Path $destination '.fabglstudio-install.json'
    $installedMatches = $false
    if (-not $Force -and (Test-Path -LiteralPath $markerPath -PathType Leaf)) {
        try {
            $marker = Get-Content -LiteralPath $markerPath -Raw | ConvertFrom-Json
            $installedMatches = $marker.id -eq $artifact.id -and
                $marker.version -eq $artifact.version -and
                $marker.sha256 -eq $artifact.sha256
        }
        catch {
            $installedMatches = $false
        }
    }
    if (-not $installedMatches) {
        $missingInstallEstimate += [uint64]$artifact.size * 3
    }
}
$managedCorePreflight = Join-Path $InstallRoot "arduino-data\packages\esp32\hardware\esp32\$($manifest.profile.arduinoCore.version)"
$boardManagerInstallRequired = -not $SkipBoardManagerInstall -and
    -not (Test-Path -LiteralPath $managedCorePreflight -PathType Container)
if ($missingArchiveBytes -eq 0 -and $missingInstallEstimate -eq 0 -and
    -not $boardManagerInstallRequired) {
    # A cache-hit/marker-hit run needs only room for small config and result
    # files; do not reject an otherwise healthy idempotent probe.
    $requiredBytes = [uint64](128MB)
}
else {
    $workingSetEstimate = $missingArchiveBytes + $missingInstallEstimate + 512MB
    $minimumFreeBytes = [uint64]$manifest.storage.minimumFreeBytes
    if ($boardManagerInstallRequired -and
        $manifest.storage.PSObject.Properties.Name -contains 'boardManagerMinimumFreeBytes') {
        $minimumFreeBytes = [Math]::Max(
            $minimumFreeBytes,
            [uint64]$manifest.storage.boardManagerMinimumFreeBytes)
    }
    $requiredBytes = [Math]::Max($minimumFreeBytes,
        [uint64]$workingSetEstimate)
}
$driveRoot = [System.IO.Path]::GetPathRoot($InstallRoot)
$drive = New-Object System.IO.DriveInfo -ArgumentList $driveRoot
if ([uint64]$drive.AvailableFreeSpace -lt $requiredBytes) {
    throw ('Insufficient disk space. Required: {0:N2} GiB; available: {1:N2} GiB.' -f
        ($requiredBytes / 1GB), ($drive.AvailableFreeSpace / 1GB))
}
Write-Host ('Disk preflight passed: {0:N2} GiB available, {1:N2} GiB required.' -f
    ($drive.AvailableFreeSpace / 1GB), ($requiredBytes / 1GB))

$installed = @{}
foreach ($artifact in $artifacts) {
    $archive = Get-LockedArchive -Artifact $artifact -CacheDirectory $CacheRoot `
        -OfflineDirectory $OfflineSourceDirectory
    $installed[$artifact.id] = Install-LockedZip -Artifact $artifact -ArchivePath $archive `
        -ToolchainRoot $InstallRoot -Replace:$Force
}

$arduinoData = Join-Path $InstallRoot 'arduino-data'
$arduinoDownloads = Join-Path $repositoryRoot '.downloads\arduino'
$arduinoUser = Join-Path $InstallRoot 'arduino-user'
foreach ($directory in @($arduinoData, $arduinoDownloads, $arduinoUser)) {
    $directory = Assert-PathWithin -Candidate $directory -Root $repositoryRoot -Description 'Arduino directory'
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}
$configPath = Join-Path $InstallRoot 'arduino-cli.yaml'
Write-ArduinoConfiguration -Path $configPath -DataDirectory $arduinoData `
    -DownloadsDirectory $arduinoDownloads -UserDirectory $arduinoUser `
    -IndexUrl $manifest.boardManager.indexUrl

$cliDirectory = $installed['arduino-cli-windows-x86_64']
$cliPath = Get-ChildItem -LiteralPath $cliDirectory -Filter 'arduino-cli.exe' -File -Recurse |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $cliPath) {
    throw "arduino-cli.exe was not found in the verified installation: $cliDirectory"
}

$managedCore = Join-Path $arduinoData "packages\esp32\hardware\esp32\$($manifest.profile.arduinoCore.version)"
if (-not $SkipBoardManagerInstall -and -not (Test-Path -LiteralPath $managedCore -PathType Container)) {
    if ($OfflineSourceDirectory) {
        throw 'Offline archives were verified and installed, but the repo-scoped Board Manager core is not yet provisioned. Seed .toolchains/arduino-data from an approved offline bundle or run once without -OfflineSourceDirectory; no network fallback was attempted.'
    }

    Write-Host 'Refreshing the official Espressif package index in the repo-scoped Arduino data directory.'
    & $cliPath --config-file $configPath core update-index
    if ($LASTEXITCODE -ne 0) {
        throw "arduino-cli core update-index failed with exit code $LASTEXITCODE."
    }

    Write-Host "Installing locked Board Manager package $($manifest.boardManager.package)."
    & $cliPath --config-file $configPath core install $manifest.boardManager.package --no-overwrite
    if ($LASTEXITCODE -ne 0) {
        throw "arduino-cli core install failed with exit code $LASTEXITCODE."
    }
}

$result = [ordered]@{
    schemaVersion = 1
    profile = $manifest.profile.id
    manifest = $ManifestPath
    cache = $CacheRoot
    installRoot = $InstallRoot
    arduinoCli = $cliPath
    arduinoConfig = $configPath
    fabglLibrary = $installed['olimex-fabgl-source']
    core = if (Test-Path -LiteralPath $managedCore) { $managedCore } else { $null }
    uploadPerformed = $false
}
$resultPath = Join-Path $InstallRoot 'bootstrap-result.json'
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $resultPath -Encoding UTF8
$result | ConvertTo-Json -Depth 5
Write-Host 'Bootstrap complete. No board discovery, erase, flash, or upload operation was performed.'
