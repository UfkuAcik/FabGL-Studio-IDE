[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [ValidateRange(1, 64)]
    [int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount),
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')]
    [string]$Version,
    [string]$ManifestPath,
    [string]$PackageDirectory,
    [switch]$Clean,
    [switch]$RunGuiSmoke,
    [switch]$RequireInstaller
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
        [Parameter(Mandatory = $true)][string]$Description,
        [switch]$RequireChild
    )

    $resolvedRoot = (Get-FullPath $Root).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar)
    $resolvedCandidate = Get-FullPath $Candidate
    $rootWithSeparator = $resolvedRoot + [System.IO.Path]::DirectorySeparatorChar
    $isRoot = $resolvedCandidate.Equals(
        $resolvedRoot, [System.StringComparison]::OrdinalIgnoreCase)
    $isChild = $resolvedCandidate.StartsWith(
        $rootWithSeparator, [System.StringComparison]::OrdinalIgnoreCase)
    if (-not $isChild -and (-not $isRoot -or $RequireChild)) {
        throw "$Description must remain inside the repository: $resolvedCandidate"
    }
    return $resolvedCandidate
}

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory = $true)][string]$Program,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Description
    )

    Write-Host ("{0}: {1} {2}" -f $Description, $Program,
        (($Arguments | ForEach-Object {
            if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ }
        }) -join ' '))
    $previousErrorActionPreference = $ErrorActionPreference
    try {
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

function Install-DirectoryAtomically {
    param(
        [Parameter(Mandatory = $true)][string]$StagingDirectory,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string]$AllowedRoot
    )

    $staging = Assert-PathWithin -Candidate $StagingDirectory -Root $AllowedRoot `
        -Description 'install staging directory' -RequireChild
    $target = Assert-PathWithin -Candidate $Destination -Root $AllowedRoot `
        -Description 'install destination' -RequireChild
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
            -Description 'install backup' -RequireChild
        Remove-Item -LiteralPath $backup -Recurse -Force
    }
}

function Find-MakeNsis {
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot)

    foreach ($candidate in @(
        (Join-Path $RepositoryRoot '.toolchains\NSIS\makensis.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'NSIS\makensis.exe'),
        (Join-Path $env:ProgramFiles 'NSIS\makensis.exe')
    )) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and
            (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Get-FullPath $candidate)
        }
    }
    $command = Get-Command makensis.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -ne $command) {
        return $command.Source
    }
    return $null
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
if ([int]$manifest.schemaVersion -ne 1 -or
    [string]$manifest.profile.host -ne 'windows' -or
    [string]$manifest.profile.architecture -ne 'x86_64' -or
    [string]$manifest.profile.generator -ne 'MinGW Makefiles') {
    throw 'Unsupported desktop toolchain manifest contract.'
}
if ($env:OS -ne 'Windows_NT' -or
    [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString() -ne 'X64') {
    throw 'The locked desktop build supports only Windows x86-64.'
}

$bootstrap = Join-Path $repositoryRoot 'scripts\bootstrap_desktop.ps1'
& $bootstrap -ManifestPath $ManifestPath
if ($RequireInstaller) {
    $nsisBootstrap = Join-Path $repositoryRoot 'scripts\bootstrap_nsis.ps1'
    & $nsisBootstrap -ManifestPath $ManifestPath
}

$configurationName = $Configuration.ToLowerInvariant()
$buildRelative = [string]$manifest.build.buildDirectoryPattern
$buildRelative = $buildRelative.Replace('{configuration}', $configurationName)
$installRelative = [string]$manifest.build.installDirectoryPattern
$installRelative = $installRelative.Replace('{configuration}', $configurationName)
$buildDirectory = Assert-PathWithin -Candidate (Join-Path $repositoryRoot $buildRelative) `
    -Root $repositoryRoot -Description 'desktop build directory' -RequireChild
$installDirectory = Assert-PathWithin -Candidate (Join-Path $repositoryRoot $installRelative) `
    -Root $repositoryRoot -Description 'desktop install directory' -RequireChild
if (-not $PackageDirectory) {
    $PackageDirectory = Join-Path $repositoryRoot ([string]$manifest.build.packageDirectory)
} elseif (-not [System.IO.Path]::IsPathRooted($PackageDirectory)) {
    $PackageDirectory = Join-Path $repositoryRoot $PackageDirectory
}
$PackageDirectory = Assert-PathWithin -Candidate $PackageDirectory -Root $repositoryRoot `
    -Description 'desktop package directory' -RequireChild

$sdkRoot = Assert-PathWithin `
    -Candidate (Join-Path $repositoryRoot ([string]$manifest.qt.installRoot)) `
    -Root $repositoryRoot -Description 'desktop SDK root' -RequireChild
$qtRoot = Get-FullPath (Join-Path $sdkRoot ([string]$manifest.qt.sdkDirectory))
$compilerRoot = Get-FullPath (Join-Path $sdkRoot ([string]$manifest.compiler.directory))
$compiler = Get-FullPath (Join-Path $compilerRoot ([string]$manifest.compiler.cxx))
$make = Get-FullPath (Join-Path $compilerRoot ([string]$manifest.compiler.make))
$qtBin = Join-Path $qtRoot 'bin'
$qoffscreen = Join-Path $qtRoot 'plugins\platforms\qoffscreen.dll'
foreach ($requiredFile in @($compiler, $make, (Join-Path $qtBin 'qmake.exe'),
        (Join-Path $qtBin 'windeployqt.exe'), $qoffscreen)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "The validated desktop SDK is incomplete: $requiredFile"
    }
}

$cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($null -eq $cmakeCommand) {
    throw 'CMake was not found on PATH.'
}
$cmake = $cmakeCommand.Source
$cmakeVersionText = (& $cmake --version | Select-Object -First 1)
if ($cmakeVersionText -notmatch 'cmake version\s+([0-9]+(?:\.[0-9]+){1,3})') {
    throw "Could not determine the CMake version: $cmakeVersionText"
}
$cmakeVersion = [version]$Matches[1]
$minimumCMakeVersion = [version]([string]$manifest.build.minimumCMakeVersion)
if ($cmakeVersion -lt $minimumCMakeVersion) {
    throw "CMake $minimumCMakeVersion or newer is required; found $cmakeVersion."
}
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
$cpack = Join-Path (Split-Path -Parent $cmake) 'cpack.exe'
foreach ($cmakeTool in @($ctest, $cpack)) {
    if (-not (Test-Path -LiteralPath $cmakeTool -PathType Leaf)) {
        throw "Required CMake tool is missing: $cmakeTool"
    }
}

$makeNsis = Find-MakeNsis -RepositoryRoot $repositoryRoot
if (-not [string]::IsNullOrWhiteSpace($makeNsis)) {
    $nsisVersionOutput = @(& $makeNsis /VERSION 2>&1)
    $expectedNsisVersion = 'v' + [string]$manifest.packaging.nsis.version
    if ($LASTEXITCODE -ne 0 -or $nsisVersionOutput.Count -eq 0 -or
        $nsisVersionOutput[0].ToString().Trim() -ne $expectedNsisVersion) {
        if ($RequireInstaller) {
            throw "NSIS $($manifest.packaging.nsis.version) is required; the version probe failed."
        }
        Write-Warning 'The available NSIS version does not match the desktop lock; installer packaging is disabled.'
        $makeNsis = $null
    }
}
if ($RequireInstaller -and [string]::IsNullOrWhiteSpace($makeNsis)) {
    throw 'NSIS is required for this build, but makensis.exe was not found.'
}
$nsisEnabled = -not [string]::IsNullOrWhiteSpace($makeNsis)

$previousPath = $env:PATH
$previousQtPlatform = $env:QT_QPA_PLATFORM
$previousOffscreenPlugin = $env:FGL_QOFFSCREEN_PLUGIN
try {
    $pathParts = @($qtBin, (Join-Path $compilerRoot 'bin'))
    if ($nsisEnabled) {
        $pathParts += (Split-Path -Parent $makeNsis)
    }
    $env:PATH = ($pathParts -join [System.IO.Path]::PathSeparator) +
        [System.IO.Path]::PathSeparator + $previousPath

    $configureArguments = @(
        '-S', $repositoryRoot,
        '-B', $buildDirectory,
        '-G', [string]$manifest.profile.generator,
        "-DCMAKE_BUILD_TYPE=$Configuration",
        "-DCMAKE_MAKE_PROGRAM=$make",
        "-DCMAKE_CXX_COMPILER=$compiler",
        "-DCMAKE_PREFIX_PATH=$qtRoot",
        '-DFGL_BUILD_STUDIO=ON',
        '-DFGL_BUILD_PLAYER=ON',
        '-DFGL_BUILD_TOOLS=ON',
        '-DFGL_BUILD_TESTS=ON',
        '-DFGL_BUILD_EXAMPLES=ON',
        '-DFGL_WARNINGS_AS_ERRORS=ON',
        ("-DFGL_PACKAGE_NSIS=" + $(if ($nsisEnabled) { 'ON' } else { 'OFF' }))
    )
    if (-not [string]::IsNullOrWhiteSpace($Version)) {
        $configureArguments += "-DFGL_VERSION_OVERRIDE=$Version"
    }
    if ($Clean) {
        $configureArguments += '--fresh'
    }
    Invoke-NativeChecked -Program $cmake -Arguments $configureArguments `
        -Description 'CMake desktop configure'

    $cachePath = Join-Path $buildDirectory 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
        throw "CMake configure did not create a cache: $cachePath"
    }
    $cacheText = Get-Content -LiteralPath $cachePath -Raw
    if ($cacheText -notmatch '(?m)^Qt6_DIR:PATH=' -or
        $cacheText -notmatch '(?m)^FGL_BUILD_STUDIO:BOOL=ON\r?$') {
        throw 'CMake did not configure the required Qt Studio build.'
    }
    if (-not [string]::IsNullOrWhiteSpace($Version) -and
        $cacheText -notmatch ("(?m)^FGL_VERSION_OVERRIDE:STRING=" +
            [regex]::Escape($Version) + "\r?$")) {
        throw "CMake did not apply the requested release version $Version."
    }

    Invoke-NativeChecked -Program $cmake -Arguments @(
        '--build', $buildDirectory, '--target', 'fabgl_studio',
        '--parallel', $Jobs.ToString()) -Description 'required Qt Studio target build'
    $studioBuild = Join-Path $buildDirectory 'apps\studio\FabGLStudio.exe'
    if (-not (Test-Path -LiteralPath $studioBuild -PathType Leaf)) {
        throw "The fabgl_studio target completed without FabGLStudio.exe: $studioBuild"
    }
    Invoke-NativeChecked -Program $cmake -Arguments @(
        '--build', $buildDirectory, '--parallel', $Jobs.ToString()) `
        -Description 'complete desktop build'

    $env:QT_QPA_PLATFORM = 'offscreen'
    Invoke-NativeChecked -Program $ctest -Arguments @(
        '--test-dir', $buildDirectory, '-C', $Configuration,
        '--output-on-failure', '--parallel', $Jobs.ToString()) `
        -Description 'desktop test suite'

    $installParent = Split-Path -Parent $installDirectory
    New-Item -ItemType Directory -Path $installParent -Force | Out-Null
    $installStaging = Join-Path $installParent ('.desktop-install-staging-' +
        [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $installStaging | Out-Null
    try {
        Invoke-NativeChecked -Program $cmake -Arguments @(
            '--install', $buildDirectory, '--prefix', $installStaging,
            '--config', $Configuration) -Description 'desktop install staging'
        $platformDirectory = Join-Path $installStaging 'bin\platforms'
        New-Item -ItemType Directory -Path $platformDirectory -Force | Out-Null
        Copy-Item -LiteralPath $qoffscreen -Destination (
            Join-Path $platformDirectory 'qoffscreen.dll') -Force
        Install-DirectoryAtomically -StagingDirectory $installStaging `
            -Destination $installDirectory -AllowedRoot $repositoryRoot
    }
    finally {
        if (Test-Path -LiteralPath $installStaging) {
            $installStaging = Assert-PathWithin -Candidate $installStaging `
                -Root $repositoryRoot -Description 'desktop install staging' -RequireChild
            Remove-Item -LiteralPath $installStaging -Recurse -Force
        }
    }

    New-Item -ItemType Directory -Path $PackageDirectory -Force | Out-Null
    $packageParent = Split-Path -Parent $PackageDirectory
    $packageStaging = Join-Path $packageParent ('.desktop-package-staging-' +
        [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $packageStaging | Out-Null
    try {
        $env:FGL_QOFFSCREEN_PLUGIN = $qoffscreen
        $cpackConfig = Join-Path $buildDirectory 'CPackConfig.cmake'
        $offscreenInstallScript = Join-Path $repositoryRoot `
            'packaging\install-offscreen-plugin.cmake'
        foreach ($cpackInput in @($cpackConfig, $offscreenInstallScript)) {
            if (-not (Test-Path -LiteralPath $cpackInput -PathType Leaf)) {
                throw "Required packaging input is missing: $cpackInput"
            }
        }
        $cpackCommonArguments = @(
            '--config', $cpackConfig,
            '-D', "CPACK_INSTALL_SCRIPTS=$offscreenInstallScript",
            '-B', $packageStaging)
        Invoke-NativeChecked -Program $cpack -Arguments (
            @('-G', 'ZIP') + $cpackCommonArguments) `
            -Description 'portable ZIP package'
        if ($nsisEnabled) {
            Invoke-NativeChecked -Program $cpack -Arguments (
                @('-G', 'NSIS') + $cpackCommonArguments) `
                -Description 'NSIS installer package'
        }

        $portableArchives = @(Get-ChildItem -LiteralPath $packageStaging -File |
            Where-Object {
                $_.Name -like 'FabGL-Studio-*-Windows-*.zip' -and
                $_.Name -notlike '*-source.zip'
            })
        if ($portableArchives.Count -ne 1) {
            throw "Expected exactly one Windows portable ZIP; found $($portableArchives.Count)."
        }
        $installerPackages = @(Get-ChildItem -LiteralPath $packageStaging -File |
            Where-Object { $_.Name -like 'FabGL-Studio-*-Windows-*.exe' })
        if ($nsisEnabled -and $installerPackages.Count -ne 1) {
            throw "Expected exactly one NSIS installer; found $($installerPackages.Count)."
        }
        if (-not $nsisEnabled -and $installerPackages.Count -ne 0) {
            throw 'An unexpected installer was produced while NSIS was disabled.'
        }

        $artifacts = @($portableArchives)
        if ($nsisEnabled) {
            $artifacts += $installerPackages
        }
        foreach ($artifact in @($artifacts)) {
            $checksum = "$($artifact.FullName).sha256"
            if (-not (Test-Path -LiteralPath $checksum -PathType Leaf)) {
                throw "Package checksum is missing: $checksum"
            }
            foreach ($source in @($artifact.FullName, $checksum)) {
                $destination = Join-Path $PackageDirectory (
                    [System.IO.Path]::GetFileName($source))
                Move-Item -LiteralPath $source -Destination $destination -Force
            }
        }
    }
    finally {
        if (Test-Path -LiteralPath $packageStaging) {
            $packageStaging = Assert-PathWithin -Candidate $packageStaging `
                -Root $repositoryRoot -Description 'desktop package staging' -RequireChild
            Remove-Item -LiteralPath $packageStaging -Recurse -Force
        }
    }

    $portablePath = Join-Path $PackageDirectory $portableArchives[0].Name
    $installerPath = $null
    if ($nsisEnabled) {
        $installerPath = Join-Path $PackageDirectory $installerPackages[0].Name
    }
    $smokeScript = Join-Path $repositoryRoot 'packaging\smoke-test.ps1'
    $smokeArguments = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $smokeScript,
        '-StageRoot', $installDirectory,
        '-PackagePath', $portablePath
    )
    if ($RunGuiSmoke) {
        $smokeArguments += '-RunGuiSmoke'
    }
    if ($nsisEnabled) {
        $smokeArguments += @('-InstallerPath', $installerPath)
    }
    if ($RequireInstaller) {
        $smokeArguments += '-RequireInstaller'
    }
    Invoke-NativeChecked -Program 'powershell.exe' -Arguments $smokeArguments `
        -Description 'desktop package smoke test'

    $result = [ordered]@{
        schemaVersion = 1
        profile = [string]$manifest.profile.id
        configuration = $Configuration
        buildDirectory = $buildDirectory
        installDirectory = $installDirectory
        portablePackage = $portablePath
        portableSha256 = (Get-FileHash -LiteralPath $portablePath -Algorithm SHA256).Hash.ToLowerInvariant()
        installerPackage = $installerPath
        installerSha256 = if ($installerPath) {
            (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash.ToLowerInvariant()
        } else { $null }
        guiSmokeTested = [bool]$RunGuiSmoke
        testsSkipped = $false
        studioRequired = $true
        completedUtc = [DateTime]::UtcNow.ToString('o')
    }
    $resultPath = Join-Path $PackageDirectory 'desktop-build-result.json'
    $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $resultPath -Encoding UTF8
    $result | ConvertTo-Json -Depth 5
}
finally {
    $env:PATH = $previousPath
    $env:QT_QPA_PLATFORM = $previousQtPlatform
    $env:FGL_QOFFSCREEN_PLUGIN = $previousOffscreenPlugin
}
