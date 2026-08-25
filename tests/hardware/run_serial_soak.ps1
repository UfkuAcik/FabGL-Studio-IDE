[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^COM[1-9][0-9]{0,2}$')]
    [string]$Port,

    [ValidateRange(1200, 3000000)]
    [int]$Baud = 115200,

    [Parameter(Mandatory = $true)]
    [ValidateSet('olimex-esp32-sbc-fabgl-revb')]
    [string]$ConfirmBoardProfile,

    [ValidateRange(10, 7200)]
    [int]$DurationSeconds = 1800,

    [switch]$RequireChurn,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$manifest = Get-Content -LiteralPath (Join-Path $repositoryRoot 'toolchains\manifest.json') `
    -Raw | ConvertFrom-Json
if ([string]$manifest.profile.id -ne $ConfirmBoardProfile) {
    throw "Board confirmation does not match the locked profile '$($manifest.profile.id)'."
}

if ($env:OS -eq 'Windows_NT') {
    $matchingPorts = @(Get-CimInstance -ClassName Win32_SerialPort | Where-Object {
        [string]$_.DeviceID -eq $Port
    })
    if ($matchingPorts.Count -ne 1) {
        throw "The explicitly approved port '$Port' is not connected as a serial device."
    }
    $pnpId = [string]$matchingPorts[0].PNPDeviceID
    if (-not $pnpId.StartsWith('USB\VID_1A86&PID_7523',
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Port '$Port' is not the expected CH340 USB serial adapter for the approved Olimex fixture."
    }
}

$resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($resolvedOutput) | Out-Null
$rawLogPath = Join-Path $resolvedOutput 'serial-soak.log'
$resultPath = Join-Path $resolvedOutput 'soak-result.json'

$runtimeSamples = [System.Collections.Generic.List[object]]::new()
$projectSamples = [System.Collections.Generic.List[object]]::new()
$soakSamples = [System.Collections.Generic.List[object]]::new()
$failRecords = [System.Collections.Generic.List[string]]::new()
$malformedRecords = [System.Collections.Generic.List[string]]::new()
$allLines = [System.Collections.Generic.List[string]]::new()
$startUtc = [DateTime]::UtcNow
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$serial = [System.IO.Ports.SerialPort]::new($Port, $Baud,
    [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.ReadTimeout = 1100
$serial.WriteTimeout = 1100
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.NewLine = "`n"

function Convert-DetailFields {
    param([Parameter(Mandatory = $true)][string]$Detail)
    $fields = @{}
    foreach ($entry in $Detail.Split(';', [System.StringSplitOptions]::RemoveEmptyEntries)) {
        $separator = $entry.IndexOf('=')
        if ($separator -gt 0) {
            $fields[$entry.Substring(0, $separator)] = $entry.Substring($separator + 1)
        }
    }
    return $fields
}

try {
    $serial.Open()
    while ($stopwatch.Elapsed.TotalSeconds -lt $DurationSeconds) {
        try {
            $line = $serial.ReadLine().TrimEnd("`r")
        }
        catch [System.TimeoutException] {
            continue
        }
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }
        $allLines.Add($line)
        if (-not $line.StartsWith('FABGLSTUDIO|', [StringComparison]::Ordinal)) {
            continue
        }
        $parts = $line.Split(
            [char[]]'|', 5, [System.StringSplitOptions]::None)
        if ($parts.Count -ne 5 -or $parts[0] -ne 'FABGLSTUDIO' -or $parts[1] -ne '1') {
            $malformedRecords.Add($line)
            continue
        }
        $status = $parts[2]
        $check = $parts[3]
        $detail = $parts[4]
        if ($status -eq 'FAIL') {
            $failRecords.Add($line)
        }
        if ($status -ne 'METRIC') {
            continue
        }
        $fields = Convert-DetailFields -Detail $detail
        if ($check -eq 'runtime' -and $fields.ContainsKey('fps') -and
            $fields.ContainsKey('heapFree') -and $fields.ContainsKey('largestBlock')) {
            $runtimeSamples.Add([pscustomobject]@{
                elapsedSeconds = [Math]::Round($stopwatch.Elapsed.TotalSeconds, 3)
                fps = [double]::Parse($fields.fps, [Globalization.CultureInfo]::InvariantCulture)
                heapFree = [long]$fields.heapFree
                heapMinimum = if ($fields.ContainsKey('heapMinimum')) {
                    [long]$fields.heapMinimum
                } else { 0L }
                largestBlock = [long]$fields.largestBlock
            })
        }
        elseif ($check -eq 'project_runtime' -and $fields.ContainsKey('updates') -and
            $fields.ContainsKey('renders')) {
            $projectSamples.Add([pscustomobject]@{
                elapsedSeconds = [Math]::Round($stopwatch.Elapsed.TotalSeconds, 3)
                updates = [long]$fields.updates
                renders = [long]$fields.renders
                mode = if ($fields.ContainsKey('mode')) { [string]$fields.mode } else { '' }
            })
        }
        elseif ($check -eq 'soak' -and $fields.ContainsKey('iterations') -and
            $fields.ContainsKey('sceneTransitions') -and $fields.ContainsKey('assetLoads') -and
            $fields.ContainsKey('assetUnloads') -and $fields.ContainsKey('audioPlays') -and
            $fields.ContainsKey('entityCreates') -and $fields.ContainsKey('entityDestroys') -and
            $fields.ContainsKey('liveEntities') -and $fields.ContainsKey('reloadFailures')) {
            $soakSamples.Add([pscustomobject]@{
                elapsedSeconds = [Math]::Round($stopwatch.Elapsed.TotalSeconds, 3)
                iterations = [long]$fields.iterations
                sceneTransitions = [long]$fields.sceneTransitions
                assetLoads = [long]$fields.assetLoads
                assetUnloads = [long]$fields.assetUnloads
                audioPlays = [long]$fields.audioPlays
                entityCreates = [long]$fields.entityCreates
                entityDestroys = [long]$fields.entityDestroys
                liveEntities = [long]$fields.liveEntities
                reloadFailures = [long]$fields.reloadFailures
            })
        }
    }
}
finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
    $serial.Dispose()
    $stopwatch.Stop()
    [System.IO.File]::WriteAllLines($rawLogPath, $allLines, [Text.UTF8Encoding]::new($false))
}

$minimumSamples = [Math]::Max(5, [int][Math]::Floor($DurationSeconds * 0.75))
$errors = [System.Collections.Generic.List[string]]::new()
if ($failRecords.Count -ne 0) {
    $errors.Add("Firmware emitted $($failRecords.Count) FAIL record(s).")
}
if ($malformedRecords.Count -ne 0) {
    $errors.Add("Firmware emitted $($malformedRecords.Count) malformed protocol record(s).")
}
if ($runtimeSamples.Count -lt $minimumSamples) {
    $errors.Add("Only $($runtimeSamples.Count) runtime metrics were captured; expected at least $minimumSamples.")
}
if ($projectSamples.Count -lt $minimumSamples) {
    $errors.Add("Only $($projectSamples.Count) project metrics were captured; expected at least $minimumSamples.")
}
if ($RequireChurn -and $soakSamples.Count -lt $minimumSamples) {
    $errors.Add("Only $($soakSamples.Count) churn metrics were captured; expected at least $minimumSamples.")
}

$fpsMinimum = 0.0
$fpsMean = 0.0
$fpsMaximum = 0.0
$heapMinimum = 0L
$heapMaximum = 0L
$largestBlockMinimum = 0L
$heapDrift = 0L
if ($runtimeSamples.Count -gt 0) {
    $fpsValues = @($runtimeSamples | ForEach-Object { $_.fps })
    $heapValues = @($runtimeSamples | ForEach-Object { $_.heapFree })
    $blockValues = @($runtimeSamples | ForEach-Object { $_.largestBlock })
    $fpsMinimum = [double](($fpsValues | Measure-Object -Minimum).Minimum)
    $fpsMean = [double](($fpsValues | Measure-Object -Average).Average)
    $fpsMaximum = [double](($fpsValues | Measure-Object -Maximum).Maximum)
    $heapMinimum = [long](($heapValues | Measure-Object -Minimum).Minimum)
    $heapMaximum = [long](($heapValues | Measure-Object -Maximum).Maximum)
    $largestBlockMinimum = [long](($blockValues | Measure-Object -Minimum).Minimum)
    $window = [Math]::Min(30, $runtimeSamples.Count)
    $firstMean = [double](($runtimeSamples | Select-Object -First $window |
        Measure-Object -Property heapFree -Average).Average)
    $lastMean = [double](($runtimeSamples | Select-Object -Last $window |
        Measure-Object -Property heapFree -Average).Average)
    $heapDrift = [long][Math]::Round($lastMean - $firstMean)
    if ($fpsMinimum -lt 30.0) {
        $errors.Add("Runtime FPS fell below the 30 FPS soak floor: $fpsMinimum.")
    }
    if ($heapMinimum -lt 49152) {
        $errors.Add("Free heap fell below the 48 KiB project floor: $heapMinimum bytes.")
    }
    if ($largestBlockMinimum -lt 32768) {
        $errors.Add("Largest free block fell below 32 KiB: $largestBlockMinimum bytes.")
    }
    if ($heapDrift -lt -4096) {
        $errors.Add("Mean free heap fell by more than 4 KiB: $heapDrift bytes.")
    }
}

$firstUpdates = 0L
$lastUpdates = 0L
$firstRenders = 0L
$lastRenders = 0L
if ($projectSamples.Count -gt 0) {
    $firstUpdates = [long]$projectSamples[0].updates
    $lastUpdates = [long]$projectSamples[$projectSamples.Count - 1].updates
    $firstRenders = [long]$projectSamples[0].renders
    $lastRenders = [long]$projectSamples[$projectSamples.Count - 1].renders
    if ($lastUpdates -le $firstUpdates -or $lastRenders -le $firstRenders) {
        $errors.Add('Project update/render counters did not advance during the soak.')
    }
    for ($index = 1; $index -lt $projectSamples.Count; ++$index) {
        if ($projectSamples[$index].updates -lt $projectSamples[$index - 1].updates -or
            $projectSamples[$index].renders -lt $projectSamples[$index - 1].renders) {
            $errors.Add("Project counters moved backwards at sample $index.")
            break
        }
    }
}

$finalSoak = $null
if ($soakSamples.Count -gt 0) {
    $counterNames = @('iterations', 'sceneTransitions', 'assetLoads', 'assetUnloads',
        'audioPlays', 'entityCreates', 'entityDestroys', 'reloadFailures')
    for ($index = 0; $index -lt $soakSamples.Count; ++$index) {
        $sample = $soakSamples[$index]
        if ($sample.iterations -ne $sample.sceneTransitions -or
            $sample.iterations -ne $sample.audioPlays -or
            $sample.assetLoads -lt $sample.assetUnloads -or
            $sample.assetLoads - $sample.assetUnloads -gt 1 -or
            $sample.entityCreates -lt $sample.entityDestroys -or
            $sample.entityCreates - $sample.entityDestroys -ne $sample.liveEntities -or
            $sample.liveEntities -lt 0 -or $sample.liveEntities -gt 16 -or
            $sample.reloadFailures -ne 0) {
            $errors.Add("Churn invariant failed at sample $index.")
            break
        }
        if ($index -gt 0) {
            foreach ($name in $counterNames) {
                if ([long]$sample.$name -lt [long]$soakSamples[$index - 1].$name) {
                    $errors.Add("Churn counter '$name' moved backwards at sample $index.")
                    break
                }
            }
        }
    }
    $finalSoak = $soakSamples[$soakSamples.Count - 1]
    if ($RequireChurn -and
        ($finalSoak.sceneTransitions -le 0 -or $finalSoak.assetLoads -le 0 -or
         $finalSoak.assetUnloads -le 0 -or $finalSoak.audioPlays -le 0 -or
         $finalSoak.entityCreates -le 0 -or $finalSoak.entityDestroys -le 0)) {
        $errors.Add('The required scene/asset/audio/entity churn paths did not all execute.')
    }
}

$endUtc = [DateTime]::UtcNow
$result = [ordered]@{
    schemaVersion = 1
    profile = $ConfirmBoardProfile
    port = $Port
    baud = $Baud
    startedUtc = $startUtc.ToString('o')
    endedUtc = $endUtc.ToString('o')
    requestedDurationSeconds = $DurationSeconds
    observedDurationSeconds = [Math]::Round($stopwatch.Elapsed.TotalSeconds, 3)
    automatedSoakPassed = ($errors.Count -eq 0)
    hardwareVerified = $false
    manualVisualAudioInputPending = $true
    rawLog = [System.IO.Path]::GetFileName($rawLogPath)
    protocolLines = $allLines.Count
    failRecords = $failRecords.Count
    malformedRecords = $malformedRecords.Count
    runtimeSamples = $runtimeSamples.Count
    projectSamples = $projectSamples.Count
    churnRequired = [bool]$RequireChurn
    soakSamples = $soakSamples.Count
    fps = [ordered]@{ minimum = $fpsMinimum; mean = $fpsMean; maximum = $fpsMaximum }
    memory = [ordered]@{
        freeHeapMinimum = $heapMinimum
        freeHeapMaximum = $heapMaximum
        largestBlockMinimum = $largestBlockMinimum
        meanHeapDriftBytes = $heapDrift
    }
    projectCounters = [ordered]@{
        firstUpdates = $firstUpdates
        lastUpdates = $lastUpdates
        firstRenders = $firstRenders
        lastRenders = $lastRenders
    }
    soakCounters = if ($null -eq $finalSoak) { $null } else {
        [ordered]@{
            iterations = $finalSoak.iterations
            sceneTransitions = $finalSoak.sceneTransitions
            assetLoads = $finalSoak.assetLoads
            assetUnloads = $finalSoak.assetUnloads
            audioPlays = $finalSoak.audioPlays
            entityCreates = $finalSoak.entityCreates
            entityDestroys = $finalSoak.entityDestroys
            liveEntities = $finalSoak.liveEntities
            reloadFailures = $finalSoak.reloadFailures
        }
    }
    errors = @($errors)
    note = 'Automated serial/runtime soak only. VGA image, audible output, physical input, SD write, PSRAM, cold power cycles, and board-label inspection remain manual.'
}
[System.IO.File]::WriteAllText(
    $resultPath,
    ($result | ConvertTo-Json -Depth 8),
    [Text.UTF8Encoding]::new($false))
$result | ConvertTo-Json -Depth 8
if ($errors.Count -ne 0) {
    exit 1
}
