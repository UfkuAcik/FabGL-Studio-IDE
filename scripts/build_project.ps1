[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ProjectPath,

    [ValidateSet('Pc', 'Esp32')]
    [string]$Target = 'Pc',

    [string]$ProjectCli,
    [string]$Player,
    [string]$OutputRoot,
    [string]$SdkRoot,
    [string]$CMake = 'cmake',
    [ValidateSet('Ninja', 'MinGW Makefiles', 'Unix Makefiles')]
    [string]$Generator = 'Ninja',
    [string]$CxxCompiler,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [ValidateRange(1, 256)]
    [int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount),
    [ValidateRange(1, 36000)]
    [int]$SmokeFrames = 2,

    [ValidateSet('Debug', 'Release', 'SizeOptimized', 'PerformanceOptimized')]
    [string]$Esp32BuildProfile = 'Release',
    [string]$ArduinoCli,
    [string]$ArduinoConfig,
    [string]$FabglLibrary,
    [switch]$UseSystemToolchain,
    [switch]$EnablePsram,
    [switch]$SoakDiagnostics,
    [switch]$Clean,
    [switch]$VerboseBuild,

    [switch]$Upload,
    [string]$Port,
    [ValidateSet('olimex-esp32-sbc-fabgl-revb')]
    [string]$ConfirmBoardProfile,
    [switch]$Monitor,
    [ValidateRange(1200, 3000000)]
    [int]$MonitorBaud = 115200,
    [switch]$RuntimeDiagnostics,
    [ValidateSet('all', 'vga', 'keyboard', 'mouse', 'audio', 'sd', 'psram', 'frame-rate')]
    [string]$DiagnosticCheck = 'all',
    [ValidateRange(3, 300)]
    [int]$DiagnosticDurationSeconds = 12,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$resultKind = 'FabGLStudioProjectBuildResult'
$resultSchema = 1

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

function Get-PathComparison {
    if ($env:OS -eq 'Windows_NT') {
        return [System.StringComparison]::OrdinalIgnoreCase
    }
    return [System.StringComparison]::Ordinal
}

function Assert-PathInside {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root,
        [string]$Description = 'path'
    )

    $fullPath = Get-FullPath $Path
    $fullRoot = (Get-FullPath $Root).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar)
    $prefix = $fullRoot + [System.IO.Path]::DirectorySeparatorChar
    $comparison = Get-PathComparison
    if (-not $fullPath.Equals($fullRoot, $comparison) -and
        -not $fullPath.StartsWith($prefix, $comparison)) {
        throw "$Description must remain inside its trusted root: $fullPath"
    }
    return $fullPath
}

function Assert-NoReparsePointPath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [string]$Description = 'path'
    )

    $fullPath = Get-FullPath $Path
    $volumeRoot = [System.IO.Path]::GetPathRoot($fullPath)
    if ([string]::IsNullOrWhiteSpace($volumeRoot)) {
        throw "$Description has no filesystem root: $fullPath"
    }
    $relative = $fullPath.Substring($volumeRoot.Length)
    $current = $volumeRoot
    foreach ($segment in $relative.Split([char[]]@('\', '/'),
            [System.StringSplitOptions]::RemoveEmptyEntries)) {
        $current = Join-Path $current $segment
        if (-not (Test-Path -LiteralPath $current)) {
            break
        }
        $item = Get-Item -LiteralPath $current -Force
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Description cannot traverse a symbolic link, junction, or reparse point: $current"
        }
    }
    return $fullPath
}

function Resolve-Program {
    param(
        [string]$ExplicitPath,
        [Parameter(Mandatory = $true)][string]$ExecutableName,
        [Parameter(Mandatory = $true)][string]$RelativeBuildPath,
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string]$Description
    )

    if ($ExplicitPath) {
        $candidate = Get-FullPath $ExplicitPath $RepositoryRoot
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            throw "$Description executable not found: $candidate"
        }
        return Assert-NoReparsePointPath -Path $candidate -Description "$Description executable"
    }

    $command = Get-Command $ExecutableName -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -ne $command) {
        return Assert-NoReparsePointPath -Path $command.Source -Description "$Description executable"
    }

    $candidates = New-Object 'System.Collections.Generic.List[System.IO.FileInfo]'
    $buildRoot = Join-Path $RepositoryRoot 'out\build'
    if (Test-Path -LiteralPath $buildRoot -PathType Container) {
        $buildDirectories = @(Get-ChildItem -LiteralPath $buildRoot -Directory -Force |
                Where-Object {
                    ($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0
                } | Select-Object -First 128)
        foreach ($directory in $buildDirectories) {
            $candidate = Join-Path $directory.FullName $RelativeBuildPath
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                $candidates.Add((Get-Item -LiteralPath $candidate -Force))
            }
        }
    }

    $portableCandidate = Get-FullPath (Join-Path $RepositoryRoot "..\..\bin\$ExecutableName")
    if (Test-Path -LiteralPath $portableCandidate -PathType Leaf) {
        $candidates.Add((Get-Item -LiteralPath $portableCandidate -Force))
    }
    $repositoryCandidate = Join-Path $RepositoryRoot "bin\$ExecutableName"
    if (Test-Path -LiteralPath $repositoryCandidate -PathType Leaf) {
        $candidates.Add((Get-Item -LiteralPath $repositoryCandidate -Force))
    }

    $selected = @($candidates | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1)
    if ($selected.Count -eq 0) {
        throw "$Description was not found. Build the desktop tools or pass its explicit path."
    }
    return Assert-NoReparsePointPath -Path $selected[0].FullName `
        -Description "$Description executable"
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$Program,
        [Parameter(Mandatory = $true)][object[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Description
    )

    Write-Host "$Description"
    $previousPreference = $ErrorActionPreference
    try {
        # Windows PowerShell may promote a native program's stderr to an ErrorRecord.
        # The exit code remains the authoritative success signal.
        $ErrorActionPreference = 'Continue'
        $output = @(& $Program @Arguments 2>&1 | ForEach-Object { [string]$_ })
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }
    foreach ($line in $output) {
        Write-Host $line
    }
    if ($exitCode -ne 0) {
        $tail = @($output | Select-Object -Last 20) -join [Environment]::NewLine
        throw "$Description failed with exit code $exitCode.$([Environment]::NewLine)$tail"
    }
    return $output
}

function Write-Result {
    param(
        [Parameter(Mandatory = $true)][System.Collections.IDictionary]$Result,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $directory = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
        New-Item -ItemType Directory -Path $directory | Out-Null
    }
    Assert-NoReparsePointPath -Path $directory -Description 'project build result directory' | Out-Null
    $temporaryPath = "$Path.part"
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText(
        $temporaryPath,
        ($Result | ConvertTo-Json -Depth 12) + [Environment]::NewLine,
        $utf8)
    Move-Item -LiteralPath $temporaryPath -Destination $Path -Force
}

if (($Upload -or $Monitor -or $RuntimeDiagnostics) -and $Target -ne 'Esp32') {
    throw '-Upload, -Monitor, and -RuntimeDiagnostics are available only for the Esp32 target.'
}
if ($SoakDiagnostics -and $Target -ne 'Esp32') {
    throw '-SoakDiagnostics is available only for the Esp32 target.'
}
if ($RuntimeDiagnostics -and -not $Upload) {
    throw '-RuntimeDiagnostics is a post-upload check and requires -Upload.'
}
if ($Upload -or $Monitor -or $RuntimeDiagnostics) {
    if ([string]::IsNullOrWhiteSpace($Port)) {
        throw '-Upload, -Monitor, or -RuntimeDiagnostics requires an explicit -Port.'
    }
    if ($ConfirmBoardProfile -ne 'olimex-esp32-sbc-fabgl-revb') {
        throw "-Upload, -Monitor, or -RuntimeDiagnostics requires -ConfirmBoardProfile 'olimex-esp32-sbc-fabgl-revb'."
    }
}
elseif ($Port -or $ConfirmBoardProfile) {
    throw '-Port and -ConfirmBoardProfile are accepted only with -Upload or -Monitor.'
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$ProjectPath = Get-FullPath $ProjectPath $repositoryRoot
if (-not $ProjectPath.EndsWith('.fglproject', [System.StringComparison]::OrdinalIgnoreCase) -or
    -not (Test-Path -LiteralPath $ProjectPath -PathType Leaf)) {
    throw "ProjectPath must identify an existing .fglproject file: $ProjectPath"
}
Assert-NoReparsePointPath -Path $ProjectPath -Description 'project manifest' | Out-Null

$manifest = Get-Content -LiteralPath $ProjectPath -Raw | ConvertFrom-Json
if ($null -eq $manifest.PSObject.Properties['projectGuid'] -or
    [string]$manifest.projectGuid -notmatch
        '^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-5][0-9a-fA-F]{3}-[89aAbB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}$') {
    throw 'Project manifest has no valid RFC 4122 projectGuid.'
}
$projectGuid = ([string]$manifest.projectGuid).ToLowerInvariant()
$projectRoot = Split-Path -Parent $ProjectPath
foreach ($asset in @($manifest.assets)) {
    $assetPath = Get-FullPath (Join-Path $projectRoot ([string]$asset.path)) $projectRoot
    Assert-PathInside -Path $assetPath -Root $projectRoot -Description 'project asset' | Out-Null
    Assert-NoReparsePointPath -Path $assetPath -Description 'project asset' | Out-Null
    if (-not (Test-Path -LiteralPath $assetPath -PathType Leaf)) {
        throw "Project asset is missing: $assetPath"
    }
}
$targetSlug = $Target.ToLowerInvariant()
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repositoryRoot "out\project-builds\$projectGuid\$targetSlug"
}
$OutputRoot = Assert-PathInside -Path $OutputRoot -Root $repositoryRoot `
    -Description 'project build output'
if ($OutputRoot.Equals($repositoryRoot, (Get-PathComparison))) {
    throw 'Project build output cannot be the repository root.'
}
Assert-NoReparsePointPath -Path $OutputRoot -Description 'project build output' | Out-Null
$resultPath = Join-Path $OutputRoot 'project-build-result.json'

$projectCliName = if ($env:OS -eq 'Windows_NT') { 'fabgl_project_cli.exe' } else { 'fabgl_project_cli' }
$playerName = if ($env:OS -eq 'Windows_NT') { 'fabgl_player_pc.exe' } else { 'fabgl_player_pc' }
$projectCliPath = $null
$playerPath = $null
if (-not $DryRun) {
    $projectCliPath = Resolve-Program -ExplicitPath $ProjectCli -ExecutableName $projectCliName `
        -RelativeBuildPath "tools\project_cli\$projectCliName" -RepositoryRoot $repositoryRoot `
        -Description 'fabgl_project_cli'
    if ($Target -eq 'Pc') {
        $playerPath = Resolve-Program -ExplicitPath $Player -ExecutableName $playerName `
            -RelativeBuildPath "apps\player_pc\$playerName" -RepositoryRoot $repositoryRoot `
            -Description 'fabgl_player_pc'
    }
}

$result = [ordered]@{
    schemaVersion = $resultSchema
    kind = $resultKind
    success = $false
    dryRun = [bool]$DryRun
    project = $ProjectPath
    projectGuid = $projectGuid
    target = $Target
    startedAtUtc = [DateTime]::UtcNow.ToString('o')
    completedAtUtc = $null
    validation = $null
    packageValidation = $null
    assetPipeline = $null
    nativeScripts = $null
    pc = $null
    esp32 = $null
    portDetection = $null
    upload = $null
    monitor = [ordered]@{
        requested = [bool]($Monitor -or $RuntimeDiagnostics)
        performed = $false
        bounded = [bool]$RuntimeDiagnostics
        port = if ($Monitor -or $RuntimeDiagnostics) { $Port } else { $null }
        baud = if ($Monitor -or $RuntimeDiagnostics) { $MonitorBaud } else { $null }
    }
    runtimeDiagnostics = $null
    error = $null
}

if ($DryRun) {
    $result.success = $true
    $result.completedAtUtc = [DateTime]::UtcNow.ToString('o')
    $result.validation = [ordered]@{ planned = $true; projectCli = $ProjectCli }
    $result.packageValidation = [ordered]@{ planned = $true }
    $result.assetPipeline = [ordered]@{
        planned = $true
        output = (Join-Path $OutputRoot 'prepared')
        target = $targetSlug
    }
    if ($Target -eq 'Pc') {
        $result.pc = [ordered]@{
            planned = $true
            player = $Player
            frames = $SmokeFrames
            headless = $true
        }
    }
    else {
        $result.esp32 = [ordered]@{
            planned = $true
            profile = $Esp32BuildProfile
            clean = [bool]$Clean
            psram = [bool]$EnablePsram
            soakDiagnostics = [bool]$SoakDiagnostics
        }
        $result.portDetection = [ordered]@{
            planned = [bool]$Upload
            readOnly = $true
            selectedPort = if ($Upload) { $Port } else { $null }
        }
        $result.upload = [ordered]@{
            requested = [bool]$Upload
            performed = $false
            port = if ($Upload) { $Port } else { $null }
            boardProfile = if ($Upload) { $ConfirmBoardProfile } else { $null }
        }
        $result.runtimeDiagnostics = [ordered]@{
            requested = [bool]$RuntimeDiagnostics
            performed = $false
            diagnosticCheck = if ($RuntimeDiagnostics) { $DiagnosticCheck } else { $null }
            durationSeconds = if ($RuntimeDiagnostics) { $DiagnosticDurationSeconds } else { $null }
            fixtureMode = $false
        }
    }
    $result | ConvertTo-Json -Depth 12
    return
}

try {
    $validationOutput = @(Invoke-Checked -Program $projectCliPath `
            -Arguments @('validate', $ProjectPath) -Description 'Validating project and startup scene')
    $result.validation = [ordered]@{
        success = $true
        output = $validationOutput
    }

    $packageOutput = @(Invoke-Checked -Program $projectCliPath `
            -Arguments @('package', 'validate', $ProjectPath) `
            -Description 'Validating local package graph and content')
    $result.packageValidation = [ordered]@{
        success = $true
        output = $packageOutput
    }

    $preparedRoot = Join-Path $OutputRoot 'prepared'
    $prepareOutput = @(Invoke-Checked -Program $projectCliPath `
            -Arguments @('prepare', $ProjectPath, $preparedRoot, $targetSlug) `
            -Description 'Importing, optimizing, validating, compiling visual graphs, and packing assets')
    $preparedProject = Join-Path $preparedRoot 'project\Prepared.fglproject'
    $preparedPack = Join-Path $preparedRoot 'project-assets.fglpack'
    foreach ($preparedFile in @($preparedProject, $preparedPack)) {
        if (-not (Test-Path -LiteralPath $preparedFile -PathType Leaf)) {
            throw "Project preparation did not produce its required artifact: $preparedFile"
        }
        Assert-NoReparsePointPath -Path $preparedFile -Description 'prepared project artifact' |
            Out-Null
    }
    $result.assetPipeline = [ordered]@{
        success = $true
        output = $preparedRoot
        target = $targetSlug
        preparedProject = $preparedProject
        pack = $preparedPack
        packBytes = [uint64](Get-Item -LiteralPath $preparedPack).Length
        packSha256 = (Get-FileHash -LiteralPath $preparedPack -Algorithm SHA256).Hash.ToLowerInvariant()
        log = $prepareOutput
    }

    if ($Target -eq 'Pc') {
        $scriptsRoot = Join-Path $projectRoot 'Scripts'
        $modulePath = $null
        $managedScriptGlue = Join-Path $scriptsRoot 'FabGLStudioScripts.cmake'
        if (Test-Path -LiteralPath $managedScriptGlue -PathType Leaf) {
            Assert-NoReparsePointPath -Path $scriptsRoot -Description 'project Scripts directory' | Out-Null
            $scriptBuildPath = Join-Path $PSScriptRoot 'build_project_scripts.ps1'
            $scriptArguments = @(
                '-ProjectPath', $ProjectPath,
                '-CMake', $CMake,
                '-Generator', $Generator,
                '-Configuration', $Configuration,
                '-Jobs', [string]$Jobs)
            if ($SdkRoot) { $scriptArguments += @('-SdkRoot', $SdkRoot) }
            if ($CxxCompiler) { $scriptArguments += @('-CxxCompiler', $CxxCompiler) }
            Invoke-Checked -Program $scriptBuildPath -Arguments $scriptArguments `
                -Description 'Building trusted native gameplay module' | Out-Null

            $manifestGuid = ([string]$manifest.projectGuid).ToLowerInvariant()
            $moduleResultPath = Join-Path $repositoryRoot `
                "out\project-scripts\$manifestGuid\build-result.json"
            if (-not (Test-Path -LiteralPath $moduleResultPath -PathType Leaf)) {
                throw "Native script build did not produce its result: $moduleResultPath"
            }
            $moduleResult = Get-Content -LiteralPath $moduleResultPath -Raw | ConvertFrom-Json
            if ($moduleResult.schemaVersion -ne 2 -or -not [bool]$moduleResult.success -or
                [string]$moduleResult.projectGuid -ne $manifestGuid) {
                throw 'Native script build result has an incompatible schema, status, or project GUID.'
            }
            $modulePath = Get-FullPath ([string]$moduleResult.module) $repositoryRoot
            if (-not (Test-Path -LiteralPath $modulePath -PathType Leaf)) {
                throw "Native gameplay module is missing: $modulePath"
            }
            $moduleSha256 = (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($moduleSha256 -ne [string]$moduleResult.moduleSha256) {
                throw 'Native gameplay module no longer matches its recorded SHA-256.'
            }
            $result.nativeScripts = [ordered]@{
                built = $true
                result = $moduleResultPath
                module = $modulePath
                moduleSha256 = $moduleSha256
                sourceCount = [int]$moduleResult.sourceCount
            }
        }
        else {
            $result.nativeScripts = [ordered]@{ built = $false; sourceCount = 0 }
        }

        $playerArguments = @('--project', $preparedProject, '--headless', '--frames', [string]$SmokeFrames)
        if ($modulePath) {
            $playerArguments += @('--script-module', $modulePath)
        }
        $playerOutput = @(Invoke-Checked -Program $playerPath -Arguments $playerArguments `
                -Description 'Running the data-driven PC player smoke test')
        $result.pc = [ordered]@{
            success = $true
            player = $playerPath
            frames = $SmokeFrames
            headless = $true
            output = $playerOutput
            preparedProject = $preparedProject
        }
    }
    else {
        $esp32OutputRoot = Join-Path $OutputRoot 'esp32'
        $esp32BuildPath = Join-Path $PSScriptRoot 'build_esp32.ps1'
        $esp32Arguments = @(
            '-ProjectPath', $preparedProject,
            '-ProjectCli', $projectCliPath,
            '-OutputRoot', $esp32OutputRoot,
            '-BuildProfile', $Esp32BuildProfile,
            '-Jobs', [string]$Jobs)
        if ($ArduinoCli) { $esp32Arguments += @('-ArduinoCli', $ArduinoCli) }
        if ($ArduinoConfig) { $esp32Arguments += @('-ArduinoConfig', $ArduinoConfig) }
        if ($FabglLibrary) { $esp32Arguments += @('-FabglLibrary', $FabglLibrary) }
        if ($UseSystemToolchain) { $esp32Arguments += '-UseSystemToolchain' }
        if ($EnablePsram) { $esp32Arguments += '-EnablePsram' }
        if ($SoakDiagnostics) { $esp32Arguments += '-SoakDiagnostics' }
        if ($Clean) { $esp32Arguments += '-Clean' }
        if ($VerboseBuild) { $esp32Arguments += '-VerboseBuild' }
        Invoke-Checked -Program $esp32BuildPath -Arguments $esp32Arguments `
            -Description 'Exporting and compiling the ESP32 project firmware' | Out-Null

        $esp32ResultPath = Join-Path $esp32OutputRoot 'build-result.json'
        if (-not (Test-Path -LiteralPath $esp32ResultPath -PathType Leaf)) {
            throw "ESP32 build did not produce its result: $esp32ResultPath"
        }
        $esp32Result = Get-Content -LiteralPath $esp32ResultPath -Raw | ConvertFrom-Json
        if ($esp32Result.schemaVersion -ne 2 -or -not [bool]$esp32Result.success -or
            -not [bool]$esp32Result.exportPerformed) {
            throw 'ESP32 project build result has an incompatible schema, status, or export state.'
        }
        $result.esp32 = [ordered]@{
            success = $true
            result = $esp32ResultPath
            profile = [string]$esp32Result.profile
            buildProfile = [string]$esp32Result.buildProfile
            binary = [string]$esp32Result.binary
            binaryBytes = [uint64]$esp32Result.binaryBytes
            binarySha256 = [string]$esp32Result.binarySha256
            programStorageBytes = [uint64]$esp32Result.programStorageBytes
            programStorageUsagePercent = [double]$esp32Result.programStorageUsagePercent
            globalStaticRamBytes = [uint64]$esp32Result.globalStaticRamBytes
            dynamicRamRemainingBytes = [uint64]$esp32Result.dynamicRamRemainingBytes
            map = [string]$esp32Result.map
            mapBytes = if ($null -eq $esp32Result.mapBytes) { $null } else {
                [uint64]$esp32Result.mapBytes
            }
            mapSha256 = [string]$esp32Result.mapSha256
            payload = [string]$esp32Result.payload
            payloadSha256 = [string]$esp32Result.payloadSha256
        }

        if ($Upload) {
            $detectorPath = Join-Path $PSScriptRoot 'detect_serial_ports.ps1'
            $detectionOutput = @(Invoke-Checked -Program $detectorPath -Arguments @() `
                    -Description 'Detecting serial ports without opening them')
            $detectionText = $detectionOutput -join [Environment]::NewLine
            if ($detectionText.Length -gt 1MB -or $detectionText.IndexOf([char]0) -ge 0) {
                throw 'Serial-port detector returned an oversized or NUL-containing report.'
            }
            $detection = $detectionText | ConvertFrom-Json
            if ($detection.schemaVersion -ne 1 -or
                [string]$detection.operation -ne 'read-only-port-detection' -or
                [bool]$detection.uploadPerformed -or [bool]$detection.portOpened) {
                throw 'Serial-port detector returned an unsafe or unsupported report.'
            }
            $selectedDetections = @($detection.ports | Where-Object {
                    [string]$_.port -eq $Port
                })
            $result.portDetection = [ordered]@{
                performed = $true
                readOnly = $true
                selectedPort = $Port
                selectedPortDetected = $selectedDetections.Count -eq 1
                selectedPortBoardCandidate = $selectedDetections.Count -eq 1 -and
                    [bool]$selectedDetections[0].boardCandidate
                candidateCount = @($detection.ports | Where-Object boardCandidate).Count
                report = $detection
            }
            if ($selectedDetections.Count -eq 0) {
                Write-Warning "Explicit port '$Port' was not present in the read-only detector report; continuing only because the port and exact board profile were manually confirmed."
            }
        }

        $result.upload = [ordered]@{
            requested = [bool]$Upload
            performed = $false
            port = if ($Upload) { $Port } else { $null }
            boardProfile = if ($Upload) { $ConfirmBoardProfile } else { $null }
            result = $null
        }
        if ($Upload) {
            $uploadPath = Join-Path $PSScriptRoot 'upload_esp32.ps1'
            Invoke-Checked -Program $uploadPath -Arguments @(
                '-Port', $Port,
                '-ConfirmBoardProfile', $ConfirmBoardProfile,
                '-BuildResultPath', $esp32ResultPath) `
                -Description 'Uploading the verified ESP32 project firmware' | Out-Null
            $uploadResultPath = Join-Path $esp32OutputRoot 'upload-result.json'
            if (-not (Test-Path -LiteralPath $uploadResultPath -PathType Leaf)) {
                throw "ESP32 upload did not produce its result: $uploadResultPath"
            }
            $uploadResult = Get-Content -LiteralPath $uploadResultPath -Raw | ConvertFrom-Json
            if (-not [bool]$uploadResult.executed -or [string]$uploadResult.port -ne $Port) {
                throw 'ESP32 upload result does not confirm the requested port and execution.'
            }
            $result.upload.performed = $true
            $result.upload.result = $uploadResultPath
        }

        $result.runtimeDiagnostics = [ordered]@{
            requested = [bool]$RuntimeDiagnostics
            performed = $false
            diagnosticCheck = if ($RuntimeDiagnostics) { $DiagnosticCheck } else { $null }
            durationSeconds = if ($RuntimeDiagnostics) { $DiagnosticDurationSeconds } else { $null }
            result = $null
            automatedResult = $null
            manualVerificationPending = $null
            hardwareVerified = $false
        }
        if ($RuntimeDiagnostics) {
            $diagnosticPath = Join-Path $PSScriptRoot 'capture_hardware_diagnostics.ps1'
            $diagnosticOutputRoot = Join-Path $esp32OutputRoot "hardware-diagnostics\$DiagnosticCheck"
            Invoke-Checked -Program $diagnosticPath -Arguments @(
                '-Port', $Port,
                '-Baud', [string]$MonitorBaud,
                '-ConfirmBoardProfile', $ConfirmBoardProfile,
                '-OutputDirectory', $diagnosticOutputRoot,
                '-DiagnosticCheck', $DiagnosticCheck,
                '-DurationSeconds', [string]$DiagnosticDurationSeconds) `
                -Description "Capturing bounded '$DiagnosticCheck' runtime diagnostics" | Out-Null
            $diagnosticResultPath = Join-Path $diagnosticOutputRoot `
                'hardware-diagnostic-result.json'
            if (-not (Test-Path -LiteralPath $diagnosticResultPath -PathType Leaf)) {
                throw "Runtime diagnostics did not produce its result: $diagnosticResultPath"
            }
            $diagnosticResult = Get-Content -LiteralPath $diagnosticResultPath -Raw |
                ConvertFrom-Json
            if ($diagnosticResult.schemaVersion -ne 1 -or
                [string]$diagnosticResult.operation -ne 'bounded-hardware-diagnostic-capture' -or
                [bool]$diagnosticResult.fixtureMode -or -not [bool]$diagnosticResult.portOpened -or
                [bool]$diagnosticResult.uploadPerformed -or
                [string]$diagnosticResult.profile -ne $ConfirmBoardProfile -or
                [string]$diagnosticResult.port -ne $Port -or
                [string]$diagnosticResult.diagnosticCheck -ne $DiagnosticCheck -or
                [string]$diagnosticResult.automatedResult -ne 'PASS' -or
                [bool]$diagnosticResult.hardwareVerified) {
                throw 'Runtime diagnostic result is unsafe, mismatched, or did not pass its automated checks.'
            }
            $result.runtimeDiagnostics.performed = $true
            $result.runtimeDiagnostics.result = $diagnosticResultPath
            $result.runtimeDiagnostics.automatedResult = 'PASS'
            $result.runtimeDiagnostics.manualVerificationPending =
                [bool]$diagnosticResult.manualVerificationPending
            $result.monitor.performed = $true
        }
    }

    $result.success = $true
    $result.completedAtUtc = [DateTime]::UtcNow.ToString('o')
    Write-Result -Result $result -Path $resultPath
}
catch {
    $result.error = [string]$_.Exception.Message
    $result.completedAtUtc = [DateTime]::UtcNow.ToString('o')
    Write-Result -Result $result -Path $resultPath
    throw
}

Write-Host "Project build result: $resultPath"
$result | ConvertTo-Json -Depth 12

if ($Monitor) {
    # The result is committed before opening the intentionally interactive monitor.
    $monitorPath = Join-Path $PSScriptRoot 'serial_monitor.ps1'
    $monitorArguments = @(
        '-Port', $Port,
        '-Baud', [string]$MonitorBaud,
        '-ConfirmBoardProfile', $ConfirmBoardProfile)
    if ($ArduinoCli) { $monitorArguments += @('-ArduinoCli', $ArduinoCli) }
    if ($ArduinoConfig) { $monitorArguments += @('-ArduinoConfig', $ArduinoConfig) }
    Invoke-Checked -Program $monitorPath -Arguments $monitorArguments `
        -Description 'Opening the read-only ESP32 serial monitor' | Out-Null
    $result.monitor.performed = $true
    $result.completedAtUtc = [DateTime]::UtcNow.ToString('o')
    Write-Result -Result $result -Path $resultPath
}
