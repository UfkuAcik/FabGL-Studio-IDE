[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Port,

    [ValidateRange(1200, 3000000)]
    [int]$Baud = 115200,

    [Parameter(Mandatory = $true)]
    [ValidateSet('olimex-esp32-sbc-fabgl-revb')]
    [string]$ConfirmBoardProfile,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$OutputDirectory,

    [ValidateSet('all', 'vga', 'keyboard', 'mouse', 'audio', 'sd', 'psram', 'frame-rate')]
    [string]$DiagnosticCheck = 'all',

    [ValidateRange(3, 300)]
    [int]$DurationSeconds = 12,

    [string]$FixturePath,
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

function Assert-PathInsideRepository {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $fullPath = Get-FullPath $Path $RepositoryRoot
    $fullRoot = (Get-FullPath $RepositoryRoot).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar)
    $prefix = $fullRoot + [System.IO.Path]::DirectorySeparatorChar
    $comparison = if ($env:OS -eq 'Windows_NT') {
        [System.StringComparison]::OrdinalIgnoreCase
    }
    else {
        [System.StringComparison]::Ordinal
    }
    if (-not $fullPath.StartsWith($prefix, $comparison)) {
        throw "$Description must remain inside the FabGL Studio repository: $fullPath"
    }
    return $fullPath
}

function Assert-NoReparsePointPath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $fullPath = Get-FullPath $Path
    $volumeRoot = [System.IO.Path]::GetPathRoot($fullPath)
    $current = $volumeRoot
    foreach ($segment in $fullPath.Substring($volumeRoot.Length).Split(
            [char[]]@('\', '/'), [System.StringSplitOptions]::RemoveEmptyEntries)) {
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

function Assert-SafeSerialPort {
    param([Parameter(Mandatory = $true)][string]$Value)

    $windowsPort = '^COM[1-9][0-9]{0,2}$'
    $unixPort = '^/dev/(tty(USB|ACM|S)[A-Za-z0-9._-]*|cu\.[A-Za-z0-9._-]+)$'
    if ($Value -notmatch $windowsPort -and $Value -notmatch $unixPort) {
        throw "Serial port must be an explicit COM port or supported /dev serial path: $Value"
    }
}

function Convert-DetailFields {
    param([Parameter(Mandatory = $true)][string]$Detail)

    $fields = [ordered]@{}
    foreach ($entry in $Detail.Split(';', [System.StringSplitOptions]::RemoveEmptyEntries)) {
        $separator = $entry.IndexOf('=')
        if ($separator -gt 0) {
            $key = $entry.Substring(0, $separator)
            if ($key -match '^[A-Za-z][A-Za-z0-9_-]*$' -and -not $fields.Contains($key)) {
                $fields[$key] = $entry.Substring($separator + 1)
            }
        }
    }
    return $fields
}

function Find-Records {
    param(
        [Parameter(Mandatory = $true)][object[]]$Records,
        [Parameter(Mandatory = $true)][string[]]$Statuses,
        [Parameter(Mandatory = $true)][string[]]$Checks
    )

    return @($Records | Where-Object {
            $_.status -in $Statuses -and $_.check -in $Checks
        })
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$manifest = Get-Content -LiteralPath (Join-Path $repositoryRoot 'toolchains\manifest.json') `
    -Raw | ConvertFrom-Json
if ([string]$manifest.profile.id -ne $ConfirmBoardProfile) {
    throw "Board confirmation does not match the locked profile '$($manifest.profile.id)'."
}
Assert-SafeSerialPort -Value $Port

$resolvedOutput = Assert-PathInsideRepository -Path $OutputDirectory `
    -RepositoryRoot $repositoryRoot -Description 'Hardware diagnostic output'
Assert-NoReparsePointPath -Path $resolvedOutput -Description 'Hardware diagnostic output' |
    Out-Null

$fixtureMode = -not [string]::IsNullOrWhiteSpace($FixturePath)
if ($DryRun) {
    [ordered]@{
        schemaVersion = 1
        operation = 'bounded-hardware-diagnostic-capture'
        dryRun = $true
        fixtureMode = $fixtureMode
        portOpened = $false
        uploadPerformed = $false
        profile = $ConfirmBoardProfile
        port = $Port
        baud = $Baud
        diagnosticCheck = $DiagnosticCheck
        durationSeconds = $DurationSeconds
        outputDirectory = $resolvedOutput
    } | ConvertTo-Json -Depth 5
    return
}

$lines = [System.Collections.Generic.List[string]]::new()
$startedAtUtc = [DateTime]::UtcNow
if ($fixtureMode) {
    $resolvedFixture = Get-FullPath $FixturePath $repositoryRoot
    Assert-NoReparsePointPath -Path $resolvedFixture -Description 'Diagnostic fixture' | Out-Null
    if (-not (Test-Path -LiteralPath $resolvedFixture -PathType Leaf)) {
        throw "Diagnostic fixture not found: $resolvedFixture"
    }
    $fixtureInfo = Get-Item -LiteralPath $resolvedFixture -Force
    if ($fixtureInfo.Length -gt 16MB) {
        throw 'Diagnostic fixture exceeds the 16 MiB safety limit.'
    }
    foreach ($line in Get-Content -LiteralPath $resolvedFixture) {
        $lines.Add([string]$line)
    }
}
else {
    $serial = [System.IO.Ports.SerialPort]::new(
        $Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $serial.ReadTimeout = 500
    $serial.WriteTimeout = 500
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    $serial.NewLine = "`n"
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        Write-Host "Capturing bounded hardware diagnostics from $Port for $DurationSeconds seconds."
        $serial.Open()
        while ($stopwatch.Elapsed.TotalSeconds -lt $DurationSeconds) {
            try {
                $line = $serial.ReadLine().TrimEnd("`r")
            }
            catch [System.TimeoutException] {
                continue
            }
            if (-not [string]::IsNullOrWhiteSpace($line)) {
                if ($line.Length -gt 4096) {
                    throw 'Serial diagnostic line exceeds the 4096-character protocol limit.'
                }
                if ($lines.Count -ge 4096) {
                    throw 'Serial diagnostic capture exceeds the 4096-line protocol limit.'
                }
                $lines.Add($line)
                Write-Host $line
            }
        }
    }
    finally {
        if ($serial.IsOpen) {
            $serial.Close()
        }
        $serial.Dispose()
        $stopwatch.Stop()
    }
}

[System.IO.Directory]::CreateDirectory($resolvedOutput) | Out-Null
Assert-NoReparsePointPath -Path $resolvedOutput -Description 'Hardware diagnostic output' |
    Out-Null
$rawLogPath = Join-Path $resolvedOutput 'hardware-diagnostic.log'
$resultPath = Join-Path $resolvedOutput 'hardware-diagnostic-result.json'
[System.IO.File]::WriteAllLines($rawLogPath, $lines, [System.Text.UTF8Encoding]::new($false))

$records = [System.Collections.Generic.List[object]]::new()
$malformed = [System.Collections.Generic.List[string]]::new()
$lineNumber = 0
foreach ($line in $lines) {
    ++$lineNumber
    if (-not $line.StartsWith('FABGLSTUDIO|', [System.StringComparison]::Ordinal)) {
        continue
    }
    $parts = $line.Split([char[]]'|', 5, [System.StringSplitOptions]::None)
    if ($parts.Count -ne 5 -or $parts[0] -ne 'FABGLSTUDIO' -or $parts[1] -ne '1' -or
        $parts[2] -notin @('BOOT', 'PASS', 'FAIL', 'MANUAL', 'READY', 'METRIC') -or
        $parts[3] -notmatch '^[a-z][a-z0-9_]*$') {
        $malformed.Add("line $lineNumber")
        continue
    }
    $records.Add([pscustomobject][ordered]@{
            line = $lineNumber
            status = $parts[2]
            check = $parts[3]
            detail = Convert-DetailFields -Detail $parts[4]
            rawDetail = $parts[4]
        })
}

$definitions = [ordered]@{
    vga = [ordered]@{
        required = @('vga_init', 'renderer_2d')
        related = @('vga_init', 'renderer_2d', 'vga_visual')
        manual = @('vga_visual')
    }
    keyboard = [ordered]@{
        required = @('keyboard_detect')
        related = @('keyboard_detect', 'keyboard_event')
        manual = @('keyboard_event')
    }
    mouse = [ordered]@{
        required = @('mouse_detect')
        related = @('mouse_detect', 'mouse_event')
        manual = @('mouse_event')
    }
    audio = [ordered]@{
        required = @('audio_pipeline')
        related = @('audio_pipeline', 'audio_output')
        manual = @('audio_output')
    }
    sd = [ordered]@{
        required = @('sd_mount')
        related = @('sd_mount')
        manual = @()
    }
    psram = [ordered]@{
        required = @('psram')
        related = @('psram', 'psram_profile', 'psram_hardware')
        manual = @('psram_hardware')
    }
    'frame-rate' = [ordered]@{
        required = @('runtime')
        related = @('runtime')
        manual = @()
    }
}

$requestedChecks = if ($DiagnosticCheck -eq 'all') {
    @('vga', 'keyboard', 'mouse', 'audio', 'sd', 'psram', 'frame-rate')
}
else {
    @($DiagnosticCheck)
}
$checkResults = [ordered]@{}
$overallPass = $malformed.Count -eq 0
$manualPending = $false
foreach ($name in $requestedChecks) {
    $definition = $definitions[$name]
    $relatedRecords = @(Find-Records -Records @($records) `
            -Statuses @('PASS', 'FAIL', 'MANUAL', 'METRIC') -Checks @($definition.related))
    $failures = @($relatedRecords | Where-Object status -eq 'FAIL')
    $missing = [System.Collections.Generic.List[string]]::new()
    $minimumFps = $null
    foreach ($required in @($definition.required)) {
        if ($name -eq 'frame-rate') {
            $metrics = @(Find-Records -Records @($records) -Statuses @('METRIC') `
                    -Checks @('runtime'))
            $fpsValues = @($metrics | ForEach-Object {
                    if ($_.detail.Contains('fps')) {
                        $parsedFps = 0.0
                        if ([double]::TryParse([string]$_.detail['fps'],
                                [System.Globalization.NumberStyles]::Float,
                                [System.Globalization.CultureInfo]::InvariantCulture,
                                [ref]$parsedFps)) {
                            $parsedFps
                        }
                    }
                })
            if ($fpsValues.Count -eq 0) {
                $missing.Add('METRIC|runtime with numeric fps')
            }
            else {
                $minimumFps = [double](($fpsValues | Measure-Object -Minimum).Minimum)
                if ($minimumFps -lt 30.0) {
                    $failures += [pscustomobject]@{
                        line = 0
                        status = 'FAIL'
                        check = 'runtime'
                        detail = [ordered]@{ reason = 'fps-below-30'; minimumFps = $minimumFps }
                        rawDetail = "minimumFps=$minimumFps"
                    }
                }
            }
        }
        elseif (@(Find-Records -Records @($records) -Statuses @('PASS') `
                    -Checks @($required)).Count -eq 0) {
            $missing.Add("PASS|$required")
        }
    }
    $manualRecords = @()
    if (@($definition.manual).Count -gt 0) {
        $manualRecords = @(Find-Records -Records @($records) -Statuses @('MANUAL') `
                -Checks @($definition.manual))
    }
    $checkManualPending = $manualRecords.Count -gt 0
    if ($name -in @('vga', 'keyboard', 'mouse', 'audio')) {
        # A successful electrical/pipeline probe cannot prove visible, audible, or physical input.
        $checkManualPending = $true
    }
    $passed = $failures.Count -eq 0 -and $missing.Count -eq 0
    if (-not $passed) {
        $overallPass = $false
    }
    if ($checkManualPending) {
        $manualPending = $true
    }
    $checkResults[$name] = [ordered]@{
        automatedResult = if ($passed) { 'PASS' } else { 'FAIL' }
        manualVerificationPending = $checkManualPending
        minimumFps = $minimumFps
        missing = @($missing)
        failures = @($failures)
        records = @($relatedRecords)
    }
}

$result = [ordered]@{
    schemaVersion = 1
    operation = 'bounded-hardware-diagnostic-capture'
    dryRun = $false
    fixtureMode = $fixtureMode
    portOpened = -not $fixtureMode
    uploadPerformed = $false
    profile = $ConfirmBoardProfile
    port = $Port
    baud = $Baud
    diagnosticCheck = $DiagnosticCheck
    requestedDurationSeconds = $DurationSeconds
    startedAtUtc = $startedAtUtc.ToString('o')
    completedAtUtc = [DateTime]::UtcNow.ToString('o')
    rawLog = $rawLogPath
    protocolRecordCount = $records.Count
    malformedRecords = @($malformed)
    automatedResult = if ($overallPass) { 'PASS' } else { 'FAIL' }
    manualVerificationPending = $manualPending
    hardwareVerified = $false
    checks = $checkResults
    note = 'PASS applies only to captured structured firmware checks. Visual VGA, audible output, physical input, wiring, and whole-board identity remain manual where labelled.'
}
[System.IO.File]::WriteAllText(
    $resultPath,
    ($result | ConvertTo-Json -Depth 16) + [Environment]::NewLine,
    [System.Text.UTF8Encoding]::new($false))
$result | ConvertTo-Json -Depth 16
if (-not $overallPass) {
    exit 1
}
