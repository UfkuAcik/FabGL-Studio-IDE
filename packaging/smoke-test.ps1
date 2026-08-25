[CmdletBinding()]
param(
    [string]$StageRoot,
    [string]$PackagePath,
    [string]$InstallerPath,
    [switch]$RunGuiSmoke,
    [switch]$RequireInstaller,
    [ValidateRange(2, 30)]
    [int]$GuiSmokeSeconds = 5
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-ExistingFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Description
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description is missing: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Assert-Sha256File {
    param([Parameter(Mandatory = $true)][string]$Artifact)

    $checksumPath = Resolve-ExistingFile -Path "$Artifact.sha256" `
        -Description 'SHA-256 file'
    $checksumText = (Get-Content -LiteralPath $checksumPath -Raw).Trim()
    if ($checksumText -notmatch '^([0-9A-Fa-f]{64})(?:\s+\*?(.+))?$') {
        throw "Invalid SHA-256 file: $checksumPath"
    }
    $expected = $Matches[1].ToLowerInvariant()
    if ($Matches.Count -ge 3 -and -not [string]::IsNullOrWhiteSpace($Matches[2])) {
        $listedName = [System.IO.Path]::GetFileName($Matches[2].Trim())
        if ($listedName -ne [System.IO.Path]::GetFileName($Artifact)) {
            throw "SHA-256 file names another artifact: $checksumPath"
        }
    }
    $actual = (Get-FileHash -LiteralPath $Artifact -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $expected) {
        throw "SHA-256 mismatch for $Artifact. Expected $expected, got $actual."
    }
    return $actual
}

function Resolve-StagedProgram {
    param([Parameter(Mandatory = $true)][string]$Name)

    foreach ($fileName in @("$Name.exe", $Name)) {
        $candidate = Join-Path (Join-Path $StageRoot 'bin') $fileName
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "Required staged program is missing: $Name"
}

function Invoke-StagedHelp {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Program
    )

    & $Program --help
    if ($LASTEXITCODE -ne 0) {
        throw "$Name --help failed with exit code $LASTEXITCODE."
    }
}

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory = $true)][string]$Program,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Description
    )

    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

function Invoke-SdkConsumerSmoke {
    $cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1
    $compilerCommand = Get-Command g++.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1
    $makeCommand = Get-Command mingw32-make.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -eq $cmakeCommand -or $null -eq $compilerCommand -or
        $null -eq $makeCommand) {
        throw 'The SDK consumer smoke test requires CMake and the locked MinGW tools on PATH.'
    }
    $consumerSource = Join-Path $repositoryRoot 'tests\sdk_consumer'
    if (-not (Test-Path -LiteralPath (Join-Path $consumerSource 'CMakeLists.txt') `
            -PathType Leaf)) {
        throw "SDK consumer source is missing: $consumerSource"
    }
    $smokeRoot = Join-Path $repositoryRoot 'out\smoke'
    New-Item -ItemType Directory -Path $smokeRoot -Force | Out-Null
    $consumerBuild = Join-Path $smokeRoot ('sdk-consumer-' +
        [guid]::NewGuid().ToString('N'))
    try {
        Invoke-NativeChecked -Program $cmakeCommand.Source -Arguments @(
            '-S', $consumerSource,
            '-B', $consumerBuild,
            '-G', 'MinGW Makefiles',
            '-DCMAKE_BUILD_TYPE=Release',
            "-DCMAKE_PREFIX_PATH=$StageRoot",
            "-DCMAKE_CXX_COMPILER=$($compilerCommand.Source)",
            "-DCMAKE_MAKE_PROGRAM=$($makeCommand.Source)") `
            -Description 'installed SDK consumer configure'
        Invoke-NativeChecked -Program $cmakeCommand.Source -Arguments @(
            '--build', $consumerBuild, '--parallel', '2') `
            -Description 'installed SDK consumer build'
        $consumer = Resolve-ExistingFile `
            -Path (Join-Path $consumerBuild 'fabgl_sdk_consumer.exe') `
            -Description 'SDK consumer executable'
        Invoke-NativeChecked -Program $consumer -Arguments @() `
            -Description 'installed SDK consumer execution'
    }
    finally {
        if (Test-Path -LiteralPath $consumerBuild) {
            $resolvedSmokeRoot = [System.IO.Path]::GetFullPath($smokeRoot).TrimEnd('\', '/') +
                [System.IO.Path]::DirectorySeparatorChar
            $resolvedConsumerBuild = [System.IO.Path]::GetFullPath($consumerBuild)
            if (-not $resolvedConsumerBuild.StartsWith(
                    $resolvedSmokeRoot,
                    [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "Refusing to remove an unsafe SDK smoke directory: $resolvedConsumerBuild"
            }
            Remove-Item -LiteralPath $resolvedConsumerBuild -Recurse -Force
        }
    }
}

function Invoke-OffscreenGuiSmoke {
    param([Parameter(Mandatory = $true)][string]$Studio)

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Studio
    $startInfo.WorkingDirectory = Split-Path -Parent $Studio
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.EnvironmentVariables['QT_QPA_PLATFORM'] = 'offscreen'
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw 'FabGLStudio.exe could not be started for the offscreen smoke test.'
    }
    try {
        if ($process.WaitForExit($GuiSmokeSeconds * 1000)) {
            $stdout = $process.StandardOutput.ReadToEnd()
            $stderr = $process.StandardError.ReadToEnd()
            throw ("FabGLStudio.exe exited during the offscreen GUI smoke window " +
                "with code $($process.ExitCode).`n$stdout`n$stderr")
        }
        $process.Kill()
        $process.WaitForExit()
    }
    finally {
        if (-not $process.HasExited) {
            $process.Kill()
            $process.WaitForExit()
        }
        $process.Dispose()
    }
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $StageRoot) {
    $StageRoot = Join-Path $repositoryRoot 'out\install\desktop-release'
} elseif (-not [System.IO.Path]::IsPathRooted($StageRoot)) {
    $StageRoot = Join-Path $repositoryRoot $StageRoot
}
if (-not (Test-Path -LiteralPath $StageRoot -PathType Container)) {
    throw "Desktop install tree not found: $StageRoot"
}
$StageRoot = (Resolve-Path -LiteralPath $StageRoot).Path

if (-not $PackagePath) {
    $defaultPackageDirectory = Join-Path $repositoryRoot 'out\packages\desktop'
    $archives = @(Get-ChildItem -LiteralPath $defaultPackageDirectory -File `
        -ErrorAction SilentlyContinue | Where-Object {
            $_.Name -like 'FabGL-Studio-*-Windows-*.zip' -and
            $_.Name -notlike '*-source.zip'
        })
    if ($archives.Count -ne 1) {
        throw "Expected exactly one desktop portable ZIP; found $($archives.Count)."
    }
    $PackagePath = $archives[0].FullName
} elseif (-not [System.IO.Path]::IsPathRooted($PackagePath)) {
    $PackagePath = Join-Path $repositoryRoot $PackagePath
}
$PackagePath = Resolve-ExistingFile -Path $PackagePath -Description 'portable ZIP'

$requiredStageFiles = @(
    'bin\FabGLStudio.exe',
    'bin\Qt6Core.dll',
    'bin\Qt6Gui.dll',
    'bin\Qt6Widgets.dll',
    'bin\platforms\qwindows.dll',
    'bin\platforms\qoffscreen.dll',
    'include\fabgl\scene\scene.h',
    'include\fabgl\rendering\framebuffer.h',
    'include\fabgl\frameworks\platformer.h',
    'include\fabgl\assets\asset_importer.h',
    'lib\libfabgl_engine.a',
    'lib\libfabgl_renderers.a',
    'lib\libfabgl_frameworks.a',
    'lib\libfabgl_asset_pipeline.a',
    'lib\cmake\FabGLStudio\FabGLStudioConfig.cmake',
    'lib\cmake\FabGLStudio\FabGLStudioConfigVersion.cmake',
    'lib\cmake\FabGLStudio\FabGLStudioTargets.cmake',
    'share\doc\FabGLStudio\README.md',
    'share\doc\FabGLStudio\LICENSE',
    'share\doc\FabGLStudio\NOTICE',
    'share\doc\FabGLStudio\THIRD_PARTY_LICENSES.md',
    'share\doc\FabGLStudio\USER_GUIDE.md',
    'share\doc\FabGLStudio\BUILDING.md',
    'share\doc\FabGLStudio\TOOLCHAIN.md',
    'share\doc\FabGLStudio\docs\FINAL_REPORT.md',
    'share\fabgl-studio\toolchains\manifest.json',
    'share\fabgl-studio\toolchains\desktop-manifest.json',
    'share\fabgl-studio\scripts\bootstrap_desktop.ps1',
    'share\fabgl-studio\scripts\bootstrap_nsis.ps1',
    'share\fabgl-studio\scripts\build_desktop.ps1',
    'share\fabgl-studio\scripts\build_project_scripts.ps1',
    'share\fabgl-studio\scripts\bootstrap_toolchain.ps1',
    'share\fabgl-studio\scripts\build_esp32.ps1',
    'share\fabgl-studio\scripts\detect_serial_ports.ps1',
    'share\fabgl-studio\scripts\upload_esp32.ps1',
    'share\fabgl-studio\scripts\serial_monitor.ps1',
    'share\fabgl-studio\examples\empty\Empty.fglproject',
    'share\fabgl-studio\examples\empty\Scenes\Main.fglscene',
    'share\fabgl-studio\examples\platformer\Platformer.fglproject',
    'share\fabgl-studio\examples\platformer\Scenes\Main.fglscene',
    'share\fabgl-studio\examples\top_down\TopDown.fglproject',
    'share\fabgl-studio\examples\top_down\Scenes\Main.fglscene',
    'share\fabgl-studio\examples\raycast_fps\RaycastFPS.fglproject',
    'share\fabgl-studio\examples\raycast_fps\Scenes\Main.fglscene',
    'share\fabgl-studio\examples\pseudo3d_racer\Racer.fglproject',
    'share\fabgl-studio\examples\pseudo3d_racer\Scenes\Main.fglscene',
    'share\fabgl-studio\examples\tps_technology\TPS.fglproject',
    'share\fabgl-studio\examples\tps_technology\Scenes\Main.fglscene',
    'share\fabgl-studio\examples\ui_showcase\UIShowcase.fglproject',
    'share\fabgl-studio\examples\ui_showcase\Scenes\Main.fglscene',
    'share\fabgl-studio\examples\animation_showcase\AnimationShowcase.fglproject',
    'share\fabgl-studio\examples\animation_showcase\Scenes\Main.fglscene',
    'share\fabgl-studio\examples\audio_showcase\AudioShowcase.fglproject',
    'share\fabgl-studio\examples\audio_showcase\Scenes\Main.fglscene',
    'share\fabgl-studio\examples\asset_streaming\AssetStreaming.fglproject',
    'share\fabgl-studio\examples\asset_streaming\Scenes\Main.fglscene'
)
foreach ($relativePath in $requiredStageFiles) {
    $candidate = Join-Path $StageRoot $relativePath
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "Required staged payload is missing: $relativePath"
    }
}
$configurationTargets = @(Get-ChildItem -LiteralPath (
        Join-Path $StageRoot 'lib\cmake\FabGLStudio') -File `
        -Filter 'FabGLStudioTargets-*.cmake')
if ($configurationTargets.Count -lt 1) {
    throw 'The staged SDK has no configuration-specific imported targets file.'
}

$programs = [ordered]@{
    fabgl_player_pc = Resolve-StagedProgram -Name 'fabgl_player_pc'
    fabgl_project_cli = Resolve-StagedProgram -Name 'fabgl_project_cli'
    fabgl_asset_compiler = Resolve-StagedProgram -Name 'fabgl_asset_compiler'
    fabgl_toolchain_manager = Resolve-StagedProgram -Name 'fabgl_toolchain_manager'
}
foreach ($name in @('fabgl_player_pc', 'fabgl_project_cli', 'fabgl_asset_compiler')) {
    Invoke-StagedHelp -Name $name -Program $programs[$name]
}
$toolchainManifest = Join-Path $StageRoot `
    'share\fabgl-studio\toolchains\manifest.json'
& $programs.fabgl_toolchain_manager inspect --manifest $toolchainManifest --repo $StageRoot
if ($LASTEXITCODE -notin @(0, 2)) {
    throw "fabgl_toolchain_manager inspect failed with exit code $LASTEXITCODE."
}
$emptyExample = Join-Path $StageRoot `
    'share\fabgl-studio\examples\empty\Empty.fglproject'
& $programs.fabgl_project_cli validate $emptyExample
if ($LASTEXITCODE -ne 0) {
    throw 'The bundled Empty example did not pass project validation.'
}
Invoke-SdkConsumerSmoke

$portableSha256 = Assert-Sha256File -Artifact $PackagePath
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($PackagePath)
try {
    $entryNames = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $archive.Entries) {
        [void]$entryNames.Add($entry.FullName.Replace('\', '/'))
    }
    $studioSuffix = '/bin/FabGLStudio.exe'
    $studioEntries = @($entryNames | Where-Object {
        $_.EndsWith($studioSuffix, [System.StringComparison]::OrdinalIgnoreCase)
    })
    if ($studioEntries.Count -ne 1) {
        throw "Portable ZIP must contain exactly one bin/FabGLStudio.exe; found $($studioEntries.Count)."
    }
    $archivePrefix = $studioEntries[0].Substring(
        0, $studioEntries[0].Length - 'bin/FabGLStudio.exe'.Length)
    $requiredArchiveFiles = @(
        'bin/FabGLStudio.exe',
        'bin/fabgl_player_pc.exe',
        'bin/fabgl_project_cli.exe',
        'bin/fabgl_asset_compiler.exe',
        'bin/fabgl_toolchain_manager.exe',
        'bin/Qt6Core.dll',
        'bin/Qt6Gui.dll',
        'bin/Qt6Widgets.dll',
        'bin/platforms/qwindows.dll',
        'bin/platforms/qoffscreen.dll',
        'include/fabgl/scene/scene.h',
        'include/fabgl/rendering/framebuffer.h',
        'include/fabgl/frameworks/platformer.h',
        'include/fabgl/assets/asset_importer.h',
        'lib/libfabgl_engine.a',
        'lib/libfabgl_renderers.a',
        'lib/libfabgl_frameworks.a',
        'lib/libfabgl_asset_pipeline.a',
        'lib/cmake/FabGLStudio/FabGLStudioConfig.cmake',
        'lib/cmake/FabGLStudio/FabGLStudioConfigVersion.cmake',
        'lib/cmake/FabGLStudio/FabGLStudioTargets.cmake',
        'share/doc/FabGLStudio/README.md',
        'share/doc/FabGLStudio/LICENSE',
        'share/doc/FabGLStudio/NOTICE',
        'share/doc/FabGLStudio/THIRD_PARTY_LICENSES.md',
        'share/doc/FabGLStudio/USER_GUIDE.md',
        'share/doc/FabGLStudio/BUILDING.md',
        'share/doc/FabGLStudio/TOOLCHAIN.md',
        'share/doc/FabGLStudio/docs/FINAL_REPORT.md',
        'share/fabgl-studio/toolchains/manifest.json',
        'share/fabgl-studio/toolchains/desktop-manifest.json',
        'share/fabgl-studio/scripts/bootstrap_desktop.ps1',
        'share/fabgl-studio/scripts/bootstrap_nsis.ps1',
        'share/fabgl-studio/scripts/build_desktop.ps1',
        'share/fabgl-studio/scripts/build_project_scripts.ps1',
        'share/fabgl-studio/scripts/bootstrap_toolchain.ps1',
        'share/fabgl-studio/scripts/build_esp32.ps1',
        'share/fabgl-studio/scripts/detect_serial_ports.ps1',
        'share/fabgl-studio/scripts/upload_esp32.ps1',
        'share/fabgl-studio/scripts/serial_monitor.ps1',
        'share/fabgl-studio/examples/empty/Empty.fglproject',
        'share/fabgl-studio/examples/empty/Scenes/Main.fglscene',
        'share/fabgl-studio/examples/platformer/Platformer.fglproject',
        'share/fabgl-studio/examples/platformer/Scenes/Main.fglscene',
        'share/fabgl-studio/examples/top_down/TopDown.fglproject',
        'share/fabgl-studio/examples/top_down/Scenes/Main.fglscene',
        'share/fabgl-studio/examples/raycast_fps/RaycastFPS.fglproject',
        'share/fabgl-studio/examples/raycast_fps/Scenes/Main.fglscene',
        'share/fabgl-studio/examples/pseudo3d_racer/Racer.fglproject',
        'share/fabgl-studio/examples/pseudo3d_racer/Scenes/Main.fglscene',
        'share/fabgl-studio/examples/tps_technology/TPS.fglproject',
        'share/fabgl-studio/examples/tps_technology/Scenes/Main.fglscene',
        'share/fabgl-studio/examples/ui_showcase/UIShowcase.fglproject',
        'share/fabgl-studio/examples/ui_showcase/Scenes/Main.fglscene',
        'share/fabgl-studio/examples/animation_showcase/AnimationShowcase.fglproject',
        'share/fabgl-studio/examples/animation_showcase/Scenes/Main.fglscene',
        'share/fabgl-studio/examples/audio_showcase/AudioShowcase.fglproject',
        'share/fabgl-studio/examples/audio_showcase/Scenes/Main.fglscene',
        'share/fabgl-studio/examples/asset_streaming/AssetStreaming.fglproject',
        'share/fabgl-studio/examples/asset_streaming/Scenes/Main.fglscene'
    )
    $missingArchiveFiles = @($requiredArchiveFiles | Where-Object {
        -not $entryNames.Contains($archivePrefix + $_)
    })
    if ($missingArchiveFiles.Count -ne 0) {
        throw ('Portable ZIP is missing required payload: ' +
            ($missingArchiveFiles -join ', '))
    }
    $configurationTargetPrefix = $archivePrefix +
        'lib/cmake/FabGLStudio/FabGLStudioTargets-'
    $configurationTargetEntries = @($entryNames | Where-Object {
        $_.StartsWith($configurationTargetPrefix,
            [System.StringComparison]::OrdinalIgnoreCase) -and
        $_.EndsWith('.cmake', [System.StringComparison]::OrdinalIgnoreCase)
    })
    if ($configurationTargetEntries.Count -lt 1) {
        throw 'Portable ZIP has no configuration-specific SDK targets file.'
    }
}
finally {
    $archive.Dispose()
}

if ($RequireInstaller -and [string]::IsNullOrWhiteSpace($InstallerPath)) {
    throw 'An NSIS installer is required, but no installer path was provided.'
}
$installerSha256 = $null
if (-not [string]::IsNullOrWhiteSpace($InstallerPath)) {
    if (-not [System.IO.Path]::IsPathRooted($InstallerPath)) {
        $InstallerPath = Join-Path $repositoryRoot $InstallerPath
    }
    $InstallerPath = Resolve-ExistingFile -Path $InstallerPath -Description 'NSIS installer'
    if ([System.IO.Path]::GetExtension($InstallerPath) -ne '.exe') {
        throw "The installer must be an .exe file: $InstallerPath"
    }
    $installerSha256 = Assert-Sha256File -Artifact $InstallerPath
}

if ($RunGuiSmoke) {
    Invoke-OffscreenGuiSmoke -Studio (Join-Path $StageRoot 'bin\FabGLStudio.exe')
}

[ordered]@{
    stageRoot = $StageRoot
    portablePackage = $PackagePath
    portableSha256 = $portableSha256
    installerPackage = $InstallerPath
    installerSha256 = $installerSha256
    studioPresent = $true
    qtRuntimePresent = $true
    cliSmokePassed = $true
    examplesValidated = $true
    sdkConsumerPassed = $true
    guiSmokePassed = [bool]$RunGuiSmoke
} | ConvertTo-Json -Depth 4
