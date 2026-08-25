[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?$')]
    [string]$Version,
    [string]$OutputDirectory,
    [string]$SourceRepository,
    [switch]$IncludeWorkingTree
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-PathWithinRepository {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$RepositoryRoot
    )
    $root = [System.IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\', '/')
    $candidate = [System.IO.Path]::GetFullPath($Path)
    if (-not $candidate.StartsWith(
            $root + [System.IO.Path]::DirectorySeparatorChar,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Source package output must remain inside the repository: $candidate"
    }
    return $candidate
}

function Invoke-GitChecked {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    & git.exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "git failed with exit code $LASTEXITCODE."
    }
}

function Invoke-GitCapture {
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    $output = @(& git.exe -C $RepositoryRoot @Arguments)
    if ($LASTEXITCODE -ne 0) {
        throw "git failed with exit code $LASTEXITCODE."
    }
    return ($output -join "`n").Trim()
}

function Get-WorkingTreeFiles {
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot)

    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = 'git.exe'
    $start.WorkingDirectory = $RepositoryRoot
    $start.Arguments = '-c core.quotepath=false ls-files -z --cached --others --exclude-standard'
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [System.Text.UTF8Encoding]::new($false)
    $start.StandardErrorEncoding = [System.Text.UTF8Encoding]::new($false)
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $start
    $started = $process.Start()
    if (-not $started) {
        throw 'Could not start git while enumerating source files.'
    }
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    [void]$process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "git source enumeration failed with exit code $($process.ExitCode): $stderr"
    }
    $files = @($stdout.Split([char]0, [System.StringSplitOptions]::RemoveEmptyEntries))
    [void][Array]::Sort($files, [System.StringComparer]::Ordinal)
    $process.Dispose()
    return $files
}

function Assert-NoReparsePath {
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string]$RelativePath
    )
    $current = $RepositoryRoot
    foreach ($segment in $RelativePath.Split([char[]]@('/', '\'),
            [System.StringSplitOptions]::RemoveEmptyEntries)) {
        $current = Join-Path $current $segment
        if (-not (Test-Path -LiteralPath $current)) {
            throw "Source package input disappeared while packaging: $RelativePath"
        }
        $attributes = [System.IO.File]::GetAttributes($current)
        if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Source package input cannot traverse a link or reparse point: $RelativePath"
        }
    }
}

function New-WorkingTreeArchive {
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string]$ArchivePath,
        [Parameter(Mandatory = $true)][string]$Prefix,
        [Parameter(Mandatory = $true)][string]$Revision,
        [Parameter(Mandatory = $true)][bool]$Dirty
    )
    $enumeratedFiles = @(Get-WorkingTreeFiles -RepositoryRoot $RepositoryRoot)
    if (@($enumeratedFiles | Where-Object { $_ -isnot [string] }).Count -ne 0) {
        throw 'Git source enumeration returned a non-text path.'
    }
    # PowerShell 5 can surface one empty pipeline record after a redirected native
    # process. Git cannot represent an empty path, so discard only that exact value.
    $relativeFiles = @($enumeratedFiles | Where-Object { $_.Length -ne 0 })
    if ($relativeFiles.Count -eq 0 -or $relativeFiles.Count -gt 20000) {
        throw "Working-tree source file count is outside limits: $($relativeFiles.Count)"
    }
    Write-Verbose "Source package enumerated $($relativeFiles.Count) files; first=<$($relativeFiles[0])>."

    $validated = [System.Collections.Generic.List[object]]::new()
    [uint64]$totalBytes = 0
    $sourceIndex = 0
    foreach ($rawRelative in $relativeFiles) {
        ++$sourceIndex
        $relative = $rawRelative.Replace('\', '/')
        if ([string]::IsNullOrWhiteSpace($relative) -or
            [System.IO.Path]::IsPathRooted($relative) -or
            ($relative.Split('/') -contains '..')) {
            $rawType = if ($null -eq $rawRelative) { 'null' } else {
                $rawRelative.GetType().FullName
            }
            throw "Git returned an unsafe source package path at index $sourceIndex ($rawType): <$relative>"
        }
        $absolute = Assert-PathWithinRepository -Path (Join-Path $RepositoryRoot $relative) `
            -RepositoryRoot $RepositoryRoot
        Assert-NoReparsePath -RepositoryRoot $RepositoryRoot -RelativePath $relative
        if (-not (Test-Path -LiteralPath $absolute -PathType Leaf)) {
            throw "Source package input is not a regular file: $relative"
        }
        $length = [uint64](Get-Item -LiteralPath $absolute).Length
        if ($length -gt 64MB -or $totalBytes -gt 512MB - $length) {
            throw 'Working-tree source package exceeds its per-file or total size limit.'
        }
        $totalBytes += $length
        $validated.Add([pscustomobject]@{
                relative = $relative
                absolute = $absolute
            })
    }

    Add-Type -AssemblyName System.IO.Compression
    if (Test-Path -LiteralPath $ArchivePath) {
        Remove-Item -LiteralPath $ArchivePath -Force
    }
    $archiveStream = [System.IO.File]::Open(
        $ArchivePath, [System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
    try {
        $archive = [System.IO.Compression.ZipArchive]::new(
            $archiveStream, [System.IO.Compression.ZipArchiveMode]::Create, $false,
            [System.Text.UTF8Encoding]::new($false))
        try {
            $stableTimestamp = [System.DateTimeOffset]::new(
                2000, 1, 1, 0, 0, 0, [System.TimeSpan]::Zero)
            foreach ($file in $validated) {
                $entry = $archive.CreateEntry(
                    "$Prefix/$($file.relative)",
                    [System.IO.Compression.CompressionLevel]::Optimal)
                $entry.LastWriteTime = $stableTimestamp
                $input = [System.IO.File]::OpenRead($file.absolute)
                $output = $entry.Open()
                try {
                    $input.CopyTo($output)
                }
                finally {
                    $output.Dispose()
                    $input.Dispose()
                }
            }
            $metadata = [ordered]@{
                schemaVersion = 1
                kind = 'FabGLStudioSourcePackage'
                sourceRevision = $Revision
                workingTreeIncluded = $true
                sourceDirty = $Dirty
                fileCount = $validated.Count
            } | ConvertTo-Json -Compress
            $metadataEntry = $archive.CreateEntry(
                "$Prefix/SOURCE_PACKAGE.json",
                [System.IO.Compression.CompressionLevel]::Optimal)
            $metadataEntry.LastWriteTime = $stableTimestamp
            $metadataOutput = $metadataEntry.Open()
            try {
                $metadataBytes = [System.Text.UTF8Encoding]::new($false).GetBytes("$metadata`n")
                $metadataOutput.Write($metadataBytes, 0, $metadataBytes.Length)
            }
            finally {
                $metadataOutput.Dispose()
            }
        }
        finally {
            $archive.Dispose()
        }
    }
    finally {
        $archiveStream.Dispose()
    }
    return $validated.Count
}

$repositoryRoot = if ([string]::IsNullOrWhiteSpace($SourceRepository)) {
    (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
} else {
    (Resolve-Path -LiteralPath $SourceRepository).Path
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repositoryRoot 'out\packages\release'
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $repositoryRoot $OutputDirectory
}
$OutputDirectory = Assert-PathWithinRepository -Path $OutputDirectory `
    -RepositoryRoot $repositoryRoot
if ($null -eq (Get-Command git.exe -ErrorAction SilentlyContinue)) {
    throw 'Git was not found on PATH.'
}

$cmakeText = Get-Content -LiteralPath (Join-Path $repositoryRoot 'CMakeLists.txt') -Raw
if ($cmakeText -match '(?m)^\s*set\s*\(\s*FGL_PROJECT_VERSION\s+"([0-9]+\.[0-9]+\.[0-9]+)"\s*\)') {
    $projectVersion = $Matches[1]
} elseif ($cmakeText -match
    '(?s)project\s*\(\s*FabGLStudio\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
    $projectVersion = $Matches[1]
} else {
    throw 'The project version could not be read from CMakeLists.txt.'
}
if ($projectVersion -ne $Version) {
    throw "Release version $Version does not match CMake project version $projectVersion."
}

$sourceRevision = Invoke-GitCapture -RepositoryRoot $repositoryRoot `
    -Arguments @('rev-parse', '--verify', 'HEAD')
$status = Invoke-GitCapture -RepositoryRoot $repositoryRoot `
    -Arguments @('status', '--porcelain=v1', '--untracked-files=all')
$sourceDirty = -not [string]::IsNullOrWhiteSpace($status)
if ($sourceDirty -and -not $IncludeWorkingTree) {
    throw 'The source tree is dirty. Commit it for a release archive or pass -IncludeWorkingTree for an explicit local snapshot.'
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$archiveName = "FabGL-Studio-$Version-source.zip"
$archivePath = Join-Path $OutputDirectory $archiveName
$archivePath = Assert-PathWithinRepository -Path $archivePath `
    -RepositoryRoot $repositoryRoot
$fileCount = $null
if ($IncludeWorkingTree) {
    $fileCount = New-WorkingTreeArchive -RepositoryRoot $repositoryRoot `
        -ArchivePath $archivePath -Prefix "FabGL-Studio-$Version-source" `
        -Revision $sourceRevision -Dirty $sourceDirty
} else {
    Invoke-GitChecked -Arguments @(
        '-C', $repositoryRoot,
        'archive', '--format=zip',
        "--prefix=FabGL-Studio-$Version-source/",
        "--output=$archivePath",
        'HEAD')
    $trackedFiles = Invoke-GitCapture -RepositoryRoot $repositoryRoot `
        -Arguments @('ls-files')
    $fileCount = @($trackedFiles -split "`n" | Where-Object { $_ -ne '' }).Count
}
if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
    throw "Git did not create the source archive: $archivePath"
}
$hash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
$checksumPath = "$archivePath.sha256"
"$hash  $archiveName" | Set-Content -LiteralPath $checksumPath -Encoding ASCII

[ordered]@{
    sourcePackage = $archivePath
    sha256 = $hash
    checksumFile = $checksumPath
    sourceRevision = $sourceRevision
    workingTreeIncluded = [bool]$IncludeWorkingTree
    sourceDirty = $sourceDirty
    fileCount = $fileCount
} | ConvertTo-Json -Depth 3
