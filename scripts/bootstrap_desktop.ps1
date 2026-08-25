[CmdletBinding()]
param(
    [string]$ManifestPath,
    [switch]$Force,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Get-FullPath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [string]$BasePath = (Get-Location).Path
    )

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BasePath $Path))
}

function Assert-PathWithin {
    param(
        [Parameter(Mandatory = $true)][string]$Candidate,
        [Parameter(Mandatory = $true)][string]$Root,
        [string]$Description = 'path',
        [switch]$RequireChild
    )

    $candidateFull = Get-FullPath $Candidate
    $rootFull = (Get-FullPath $Root).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar)
    $prefix = $rootFull + [System.IO.Path]::DirectorySeparatorChar
    $isRoot = $candidateFull.Equals($rootFull, [System.StringComparison]::OrdinalIgnoreCase)
    if (($RequireChild -and $isRoot) -or
        (-not $isRoot -and
         -not $candidateFull.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase))) {
        throw "$Description must remain inside the repository: $candidateFull"
    }
    return $candidateFull
}

function Assert-ManifestContract {
    param([Parameter(Mandatory = $true)]$Manifest)

    if ($Manifest.schemaVersion -ne 1) {
        throw "Unsupported desktop toolchain manifest schema: $($Manifest.schemaVersion)"
    }
    if ([string]$Manifest.profile.host -ne 'windows' -or
        [string]$Manifest.profile.architecture -ne 'x86_64') {
        throw 'The desktop lock must target Windows x86-64.'
    }
    if ([string]$Manifest.qt.version -ne '6.8.3' -or
        [string]$Manifest.qt.host -ne 'windows' -or
        [string]$Manifest.qt.target -ne 'desktop' -or
        [string]$Manifest.qt.architecture -ne 'win64_mingw') {
        throw 'The desktop lock must use Qt 6.8.3 win64_mingw.'
    }
    if ([string]$Manifest.compiler.version -ne '13.1.0' -or
        [string]$Manifest.compiler.targetTriple -ne 'x86_64-w64-mingw32' -or
        [string]$Manifest.compiler.aqtTool -ne 'tools_mingw1310' -or
        [string]$Manifest.compiler.aqtVariant -ne 'qt.tools.win64_mingw1310') {
        throw 'The desktop lock must use the Qt MinGW 13.1.0 x86-64 compiler.'
    }
    if ([string]$Manifest.python.aqtinstall.package -ne 'aqtinstall' -or
        [string]$Manifest.python.aqtinstall.version -ne '3.3.0' -or
        [string]$Manifest.python.aqtinstall.indexUrl -ne 'https://pypi.org/simple') {
        throw 'The reviewed desktop bootstrap supports only aqtinstall 3.3.0.'
    }
    if ([string]$Manifest.profile.generator -ne 'MinGW Makefiles') {
        throw 'The desktop lock must use the MinGW Makefiles generator.'
    }
    if ([string]$Manifest.packaging.nsis.version -ne '3.12' -or
        [string]$Manifest.packaging.nsis.sha256 -notmatch '^[0-9a-f]{64}$') {
        throw 'The desktop lock must include the reviewed NSIS 3.12 package.'
    }
    if (@($Manifest.qt.archives).Count -ne 1 -or
        [string]@($Manifest.qt.archives)[0] -ne 'qtbase') {
        throw 'The desktop Qt archive lock must contain qtbase and no option-like values.'
    }
    $mandatoryQtFiles = @(
        'bin/qmake.exe',
        'bin/qtpaths.exe',
        'bin/windeployqt.exe',
        'bin/Qt6Core.dll',
        'bin/Qt6Gui.dll',
        'bin/Qt6Widgets.dll',
        'lib/cmake/Qt6/Qt6Config.cmake',
        'plugins/platforms/qwindows.dll',
        'plugins/platforms/qoffscreen.dll')
    $declaredQtFiles = @($Manifest.qt.requiredFiles | ForEach-Object {
        ([string]$_).Replace('\', '/')
    })
    if (@($mandatoryQtFiles | Where-Object { $declaredQtFiles -notcontains $_ }).Count -ne 0) {
        throw 'The desktop lock is missing required Qt files or archive names.'
    }
    foreach ($relativePath in @(
            [string]$Manifest.python.aqtinstall.installDirectory,
            [string]$Manifest.qt.installRoot,
            [string]$Manifest.qt.sdkDirectory,
            [string]$Manifest.compiler.directory,
            [string]$Manifest.compiler.cxx,
            [string]$Manifest.compiler.make,
            [string]$Manifest.build.buildDirectoryPattern,
            [string]$Manifest.build.installDirectoryPattern,
            [string]$Manifest.build.packageDirectory,
            [string]$Manifest.storage.cacheDirectory,
            [string]$Manifest.packaging.nsis.installDirectory) +
            @($Manifest.qt.requiredFiles) + @($Manifest.packaging.nsis.requiredFiles)) {
        if ([string]::IsNullOrWhiteSpace($relativePath) -or
            [System.IO.Path]::IsPathRooted($relativePath) -or
            $relativePath.Replace('\', '/').Split('/') -contains '..') {
            throw "Unsafe relative path in desktop toolchain manifest: $relativePath"
        }
    }
    if ([uint64]$Manifest.storage.minimumFreeBytes -lt 1GB) {
        throw 'Desktop toolchain disk preflight must reserve at least 1 GiB.'
    }
    $indexUri = [System.Uri]([string]$Manifest.python.aqtinstall.indexUrl)
    $qtSourceUri = [System.Uri]([string]$Manifest.qt.source)
    $compilerSourceUri = [System.Uri]([string]$Manifest.compiler.source)
    $nsisSourceUri = [System.Uri]([string]$Manifest.packaging.nsis.url)
    if ($indexUri.Scheme -ne 'https' -or $qtSourceUri.Scheme -ne 'https' -or
        $compilerSourceUri.Scheme -ne 'https' -or $nsisSourceUri.Scheme -ne 'https') {
        throw 'Desktop toolchain sources must use HTTPS.'
    }
}

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory = $true)][string]$Program,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [string]$Description = 'native command'
    )

    $previousErrorActionPreference = $ErrorActionPreference
    try {
        # Windows PowerShell promotes native stderr to ErrorRecord objects.
        # aqt writes normal progress messages to stderr, so preserve the
        # process exit code as the authority without turning progress into an
        # early PowerShell exception.
        $ErrorActionPreference = 'Continue'
        & $Program @Arguments
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($exitCode -ne 0) {
        throw "$Description failed with exit code $exitCode."
    }
}

function Get-PythonInfo {
    $command = Get-Command python -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $command) {
        throw 'Python was not found on PATH. Install a supported Python 3 release first.'
    }
    $program = $command.Source
    $versionOutput = @(& $program -c 'import platform; print(platform.python_version())' 2>&1)
    if ($LASTEXITCODE -ne 0 -or $versionOutput.Count -eq 0) {
        throw "Python version probe failed: $($versionOutput -join [Environment]::NewLine)"
    }
    return [pscustomobject]@{
        Program = $program
        Version = [version]$versionOutput[0].ToString().Trim()
    }
}

function Get-AqtStatus {
    param(
        [Parameter(Mandatory = $true)][string]$Python,
        [Parameter(Mandatory = $true)][string]$PackageDirectory,
        [Parameter(Mandatory = $true)][string]$ExpectedVersion
    )

    $result = [ordered]@{ valid = $false; version = $null; issues = @() }
    if (-not (Test-Path -LiteralPath $PackageDirectory -PathType Container)) {
        $result.issues = @('repository-local aqtinstall package directory is missing')
        return [pscustomobject]$result
    }

    $previousPythonPath = $env:PYTHONPATH
    try {
        $env:PYTHONPATH = if ([string]::IsNullOrWhiteSpace($previousPythonPath)) {
            $PackageDirectory
        } else {
            $PackageDirectory + [System.IO.Path]::PathSeparator + $previousPythonPath
        }
        $output = @(& $Python -c "import importlib.metadata as m; print(m.version('aqtinstall'))" 2>&1)
        if ($LASTEXITCODE -ne 0 -or $output.Count -eq 0) {
            $result.issues = @("aqtinstall probe failed: $($output -join ' ')")
            return [pscustomobject]$result
        }
        $result.version = $output[0].ToString().Trim()
        if ($result.version -ne $ExpectedVersion) {
            $result.issues = @("aqtinstall version mismatch: expected $ExpectedVersion, found $($result.version)")
            return [pscustomobject]$result
        }
        $previousErrorActionPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            $versionOutput = @(& $Python -m aqt version 2>&1)
            $versionExitCode = $LASTEXITCODE
        }
        finally {
            $ErrorActionPreference = $previousErrorActionPreference
        }
        if ($versionExitCode -ne 0 -or ($versionOutput -join ' ') -notmatch
            "aqtinstall\(aqt\) v$([regex]::Escape($ExpectedVersion))(\s|$)") {
            $result.issues = @("aqtinstall executable probe failed: $($versionOutput -join ' ')")
            return [pscustomobject]$result
        }
        $result.valid = $true
        return [pscustomobject]$result
    }
    finally {
        $env:PYTHONPATH = $previousPythonPath
    }
}

function Get-DesktopSdkStatus {
    param(
        [Parameter(Mandatory = $true)][string]$SdkRoot,
        [Parameter(Mandatory = $true)]$Manifest
    )

    $issues = [System.Collections.Generic.List[string]]::new()
    $qtRoot = Get-FullPath (Join-Path $SdkRoot ([string]$Manifest.qt.sdkDirectory))
    $compilerRoot = Get-FullPath (Join-Path $SdkRoot ([string]$Manifest.compiler.directory))
    foreach ($requiredFile in @($Manifest.qt.requiredFiles)) {
        $candidate = Join-Path $qtRoot ([string]$requiredFile)
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            $issues.Add("required Qt file is missing: $requiredFile")
        }
    }
    $compiler = Join-Path $compilerRoot ([string]$Manifest.compiler.cxx)
    $make = Join-Path $compilerRoot ([string]$Manifest.compiler.make)
    foreach ($requiredTool in @($compiler, $make)) {
        if (-not (Test-Path -LiteralPath $requiredTool -PathType Leaf)) {
            $issues.Add("required compiler tool is missing: $requiredTool")
        }
    }

    if ($issues.Count -eq 0) {
        $qtBin = Join-Path $qtRoot 'bin'
        $compilerBin = Join-Path $compilerRoot 'bin'
        $previousPath = $env:PATH
        try {
            $env:PATH = $qtBin + [System.IO.Path]::PathSeparator + $compilerBin +
                [System.IO.Path]::PathSeparator + $previousPath
            $qtpaths = Join-Path $qtBin 'qtpaths.exe'
            $qmake = Join-Path $qtBin 'qmake.exe'

            $qtVersionOutput = @(& $qtpaths --qt-version 2>&1)
            if ($LASTEXITCODE -ne 0 -or $qtVersionOutput.Count -eq 0 -or
                $qtVersionOutput[0].ToString().Trim() -ne [string]$Manifest.qt.version) {
                $issues.Add("Qt version probe did not report $($Manifest.qt.version)")
            }

            $prefixOutput = @(& $qmake -query QT_INSTALL_PREFIX 2>&1)
            if ($LASTEXITCODE -ne 0 -or $prefixOutput.Count -eq 0) {
                $issues.Add('qmake could not report QT_INSTALL_PREFIX')
            } else {
                $reportedPrefix = Get-FullPath $prefixOutput[0].ToString().Trim()
                if (-not $reportedPrefix.Equals($qtRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
                    $issues.Add("Qt installation was patched for another location: $reportedPrefix")
                }
            }

            $specOutput = @(& $qmake -query QMAKE_XSPEC 2>&1)
            if ($LASTEXITCODE -ne 0 -or $specOutput.Count -eq 0 -or
                $specOutput[0].ToString().Trim() -ne 'win32-g++') {
                $issues.Add('Qt ABI is not the required win32-g++ ABI')
            }

            $compilerVersionOutput = @(& $compiler -dumpfullversion -dumpversion 2>&1)
            if ($LASTEXITCODE -ne 0 -or $compilerVersionOutput.Count -eq 0 -or
                $compilerVersionOutput[0].ToString().Trim() -ne [string]$Manifest.compiler.version) {
                $issues.Add("MinGW version probe did not report $($Manifest.compiler.version)")
            }
            $targetOutput = @(& $compiler -dumpmachine 2>&1)
            if ($LASTEXITCODE -ne 0 -or $targetOutput.Count -eq 0 -or
                $targetOutput[0].ToString().Trim() -ne [string]$Manifest.compiler.targetTriple) {
                $issues.Add("MinGW target probe did not report $($Manifest.compiler.targetTriple)")
            }
        }
        finally {
            $env:PATH = $previousPath
        }
    }

    return [pscustomobject]@{
        valid = $issues.Count -eq 0
        issues = @($issues)
        root = $SdkRoot
        qtRoot = $qtRoot
        compilerRoot = $compilerRoot
        compiler = $compiler
        make = $make
    }
}

function Install-DirectoryAtomically {
    param(
        [Parameter(Mandatory = $true)][string]$StagingDirectory,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string]$AllowedRoot
    )

    $staging = Assert-PathWithin -Candidate $StagingDirectory -Root $AllowedRoot `
        -Description 'staging directory' -RequireChild
    $target = Assert-PathWithin -Candidate $Destination -Root $AllowedRoot `
        -Description 'installation directory' -RequireChild
    $backup = "$target.backup-$([guid]::NewGuid().ToString('N'))"
    $backupPresent = $false
    if (Test-Path -LiteralPath $target) {
        Move-Item -LiteralPath $target -Destination $backup
        $backupPresent = $true
    }
    try {
        Move-Item -LiteralPath $staging -Destination $target
    }
    catch {
        if ($backupPresent -and -not (Test-Path -LiteralPath $target) -and
            (Test-Path -LiteralPath $backup)) {
            Move-Item -LiteralPath $backup -Destination $target
            $backupPresent = $false
        }
        throw
    }
    if ($backupPresent -and (Test-Path -LiteralPath $backup)) {
        $backup = Assert-PathWithin -Candidate $backup -Root $AllowedRoot `
            -Description 'installation backup' -RequireChild
        Remove-Item -LiteralPath $backup -Recurse -Force
    }
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $ManifestPath) {
    $ManifestPath = Join-Path $repositoryRoot 'toolchains\desktop-manifest.json'
} elseif (-not [System.IO.Path]::IsPathRooted($ManifestPath)) {
    $ManifestPath = Join-Path $repositoryRoot $ManifestPath
}
$ManifestPath = Assert-PathWithin -Candidate $ManifestPath -Root $repositoryRoot `
    -Description 'desktop toolchain manifest' -RequireChild
if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
    throw "Desktop toolchain manifest not found: $ManifestPath"
}
$manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
Assert-ManifestContract -Manifest $manifest

if ($env:OS -ne 'Windows_NT') {
    throw 'The locked desktop bootstrap currently supports only Windows x86-64.'
}
if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString() -ne 'X64') {
    throw 'The locked desktop bootstrap requires a Windows x86-64 host.'
}

$python = Get-PythonInfo
$minimumPython = [version]([string]$manifest.python.minimumVersion)
if ($python.Version -lt $minimumPython -or $python.Version.Major -ge 4) {
    throw "Python $minimumPython or newer Python 3 is required; found $($python.Version)."
}

$toolchainsRoot = Assert-PathWithin -Candidate (Join-Path $repositoryRoot '.toolchains') `
    -Root $repositoryRoot -Description 'toolchains root' -RequireChild
$pythonPackages = Assert-PathWithin `
    -Candidate (Join-Path $repositoryRoot ([string]$manifest.python.aqtinstall.installDirectory)) `
    -Root $repositoryRoot -Description 'aqtinstall directory' -RequireChild
$sdkRoot = Assert-PathWithin `
    -Candidate (Join-Path $repositoryRoot ([string]$manifest.qt.installRoot)) `
    -Root $repositoryRoot -Description 'desktop SDK root' -RequireChild
$cacheRoot = Assert-PathWithin `
    -Candidate (Join-Path $repositoryRoot ([string]$manifest.storage.cacheDirectory)) `
    -Root $repositoryRoot -Description 'desktop cache root' -RequireChild

$aqtStatus = Get-AqtStatus -Python $python.Program -PackageDirectory $pythonPackages `
    -ExpectedVersion ([string]$manifest.python.aqtinstall.version)
$sdkStatus = Get-DesktopSdkStatus -SdkRoot $sdkRoot -Manifest $manifest
$needsAqt = -not $aqtStatus.valid -or $Force
$needsSdk = -not $sdkStatus.valid -or $Force

$pipArguments = @(
    '-m', 'pip', 'install', '--disable-pip-version-check', '--no-input',
    '--no-warn-script-location', '--only-binary=:all:',
    '--index-url', [string]$manifest.python.aqtinstall.indexUrl,
    '--target', '<staging-directory>',
    "$([string]$manifest.python.aqtinstall.package)==$([string]$manifest.python.aqtinstall.version)"
)
$qtArguments = @(
    '-m', 'aqt', 'install-qt', '--outputdir', '<staging-sdk>', '--keep',
    '--archive-dest', '<archive-cache>', [string]$manifest.qt.host,
    [string]$manifest.qt.target, [string]$manifest.qt.version,
    [string]$manifest.qt.architecture, '--archives'
) + @($manifest.qt.archives | ForEach-Object { [string]$_ })
$compilerArguments = @(
    '-m', 'aqt', 'install-tool', '--outputdir', '<staging-sdk>', '--keep',
    '--archive-dest', '<archive-cache>', [string]$manifest.qt.host,
    [string]$manifest.qt.target, [string]$manifest.compiler.aqtTool,
    [string]$manifest.compiler.aqtVariant
)

if ($DryRun) {
    [ordered]@{
        schemaVersion = 1
        dryRun = $true
        profile = [string]$manifest.profile.id
        manifest = $ManifestPath
        python = $python.Program
        pythonVersion = $python.Version.ToString()
        aqtValid = [bool]$aqtStatus.valid
        aqtIssues = @($aqtStatus.issues)
        sdkValid = [bool]$sdkStatus.valid
        sdkIssues = @($sdkStatus.issues)
        wouldInstallAqt = [bool]$needsAqt
        wouldInstallSdk = [bool]$needsSdk
        pip = @{ program = $python.Program; arguments = $pipArguments }
        qt = @{ program = $python.Program; arguments = $qtArguments }
        compiler = @{ program = $python.Program; arguments = $compilerArguments }
        hashVerificationDisabled = $false
    } | ConvertTo-Json -Depth 8
    return
}

New-Item -ItemType Directory -Path $toolchainsRoot,$cacheRoot -Force | Out-Null
if ($needsAqt -or $needsSdk) {
    $drive = [System.IO.DriveInfo]::new([System.IO.Path]::GetPathRoot($toolchainsRoot))
    $requiredBytes = [uint64]$manifest.storage.minimumFreeBytes
    if ([uint64]$drive.AvailableFreeSpace -lt $requiredBytes) {
        throw ('Insufficient disk space for the locked desktop SDK. Required: {0:N2} GiB; available: {1:N2} GiB.' -f
            ($requiredBytes / 1GB), ($drive.AvailableFreeSpace / 1GB))
    }
    Write-Host ('Desktop SDK disk preflight passed: {0:N2} GiB available.' -f
        ($drive.AvailableFreeSpace / 1GB))
}

if ($needsAqt) {
    $pythonStaging = Join-Path $toolchainsRoot ('.python-packages-staging-' +
        [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $pythonStaging | Out-Null
    try {
        $installArguments = @($pipArguments)
        $installArguments[$installArguments.IndexOf('<staging-directory>')] = $pythonStaging
        Invoke-NativeChecked -Program $python.Program -Arguments $installArguments `
            -Description 'aqtinstall package installation'
        $installedAqt = Get-AqtStatus -Python $python.Program -PackageDirectory $pythonStaging `
            -ExpectedVersion ([string]$manifest.python.aqtinstall.version)
        if (-not $installedAqt.valid) {
            throw "Installed aqt package failed validation: $($installedAqt.issues -join '; ')"
        }
        Install-DirectoryAtomically -StagingDirectory $pythonStaging `
            -Destination $pythonPackages -AllowedRoot $toolchainsRoot
    }
    finally {
        if (Test-Path -LiteralPath $pythonStaging) {
            $pythonStaging = Assert-PathWithin -Candidate $pythonStaging -Root $toolchainsRoot `
                -Description 'aqt staging directory' -RequireChild
            Remove-Item -LiteralPath $pythonStaging -Recurse -Force
        }
    }
    $aqtStatus = Get-AqtStatus -Python $python.Program -PackageDirectory $pythonPackages `
        -ExpectedVersion ([string]$manifest.python.aqtinstall.version)
}

if ($needsSdk) {
    $sdkStaging = Join-Path $toolchainsRoot ('.desktop-sdk-staging-' +
        [guid]::NewGuid().ToString('N'))
    $archiveCache = Join-Path $cacheRoot 'archives'
    New-Item -ItemType Directory -Path $sdkStaging,$archiveCache -Force | Out-Null
    $previousPythonPath = $env:PYTHONPATH
    try {
        $env:PYTHONPATH = if ([string]::IsNullOrWhiteSpace($previousPythonPath)) {
            $pythonPackages
        } else {
            $pythonPackages + [System.IO.Path]::PathSeparator + $previousPythonPath
        }
        $resolvedQtArguments = @($qtArguments | ForEach-Object {
            if ($_ -eq '<staging-sdk>') { $sdkStaging }
            elseif ($_ -eq '<archive-cache>') { $archiveCache }
            else { $_ }
        })
        $resolvedCompilerArguments = @($compilerArguments | ForEach-Object {
            if ($_ -eq '<staging-sdk>') { $sdkStaging }
            elseif ($_ -eq '<archive-cache>') { $archiveCache }
            else { $_ }
        })
        Push-Location $cacheRoot
        try {
            Invoke-NativeChecked -Program $python.Program -Arguments $resolvedQtArguments `
                -Description 'Qt SDK installation'
            Invoke-NativeChecked -Program $python.Program -Arguments $resolvedCompilerArguments `
                -Description 'MinGW tool installation'
        }
        finally {
            Pop-Location
        }
        $stagedStatus = Get-DesktopSdkStatus -SdkRoot $sdkStaging -Manifest $manifest
        if (-not $stagedStatus.valid) {
            throw "Staged desktop SDK failed validation: $($stagedStatus.issues -join '; ')"
        }
        Install-DirectoryAtomically -StagingDirectory $sdkStaging -Destination $sdkRoot `
            -AllowedRoot $toolchainsRoot
    }
    finally {
        $env:PYTHONPATH = $previousPythonPath
        if (Test-Path -LiteralPath $sdkStaging) {
            $sdkStaging = Assert-PathWithin -Candidate $sdkStaging -Root $toolchainsRoot `
                -Description 'desktop SDK staging directory' -RequireChild
            Remove-Item -LiteralPath $sdkStaging -Recurse -Force
        }
    }
}

$sdkStatus = Get-DesktopSdkStatus -SdkRoot $sdkRoot -Manifest $manifest
if (-not $aqtStatus.valid -or -not $sdkStatus.valid) {
    throw "Desktop SDK validation failed: $(@($aqtStatus.issues) + @($sdkStatus.issues) -join '; ')"
}

$result = [ordered]@{
    schemaVersion = 1
    profile = [string]$manifest.profile.id
    manifest = $ManifestPath
    manifestSha256 = (Get-FileHash -LiteralPath $ManifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    installedUtc = [DateTime]::UtcNow.ToString('o')
    python = $python.Program
    pythonVersion = $python.Version.ToString()
    aqtVersion = [string]$manifest.python.aqtinstall.version
    sdkRoot = $sdkRoot
    qtRoot = $sdkStatus.qtRoot
    compilerRoot = $sdkStatus.compilerRoot
    compiler = $sdkStatus.compiler
    make = $sdkStatus.make
    hashVerificationDisabled = $false
}
$resultPath = Join-Path $toolchainsRoot 'desktop-bootstrap-result.json'
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $resultPath -Encoding UTF8
$result | ConvertTo-Json -Depth 5
