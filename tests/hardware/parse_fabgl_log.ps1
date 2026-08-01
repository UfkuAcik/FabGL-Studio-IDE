[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$LogPath,

    [switch]$Json
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Add-Problem {
    param([Parameter(Mandatory = $true)][string]$Message)
    $script:problems.Add($Message)
}

function Convert-Detail {
    param(
        [Parameter(Mandatory = $true)][string]$Value,
        [Parameter(Mandatory = $true)][int]$LineNumber
    )

    $result = @{}
    if ([string]::IsNullOrEmpty($Value)) {
        return $result
    }
    foreach ($field in $Value.Split(';')) {
        $separator = $field.IndexOf('=')
        if ($separator -lt 0) {
            if ([string]::IsNullOrWhiteSpace($field) -or $field.IndexOf('|') -ge 0) {
                Add-Problem "line $LineNumber has malformed detail flag '$field'"
            }
            continue
        }
        $key = $field.Substring(0, $separator)
        $fieldValue = $field.Substring($separator + 1)
        if ($key -notmatch '^[A-Za-z][A-Za-z0-9_-]*$') {
            Add-Problem "line $LineNumber has malformed detail field '$field'"
            continue
        }
        if ($result.ContainsKey($key)) {
            Add-Problem "line $LineNumber repeats detail key '$key'"
            continue
        }
        $result[$key] = $fieldValue
    }
    return $result
}

if ($LogPath.StartsWith('\\', [System.StringComparison]::Ordinal)) {
    throw 'Hardware log must be a local filesystem path; UNC and device paths are rejected.'
}
$resolvedLog = [System.IO.Path]::GetFullPath($LogPath)
if (-not (Test-Path -LiteralPath $resolvedLog -PathType Leaf)) {
    throw "Hardware log not found: $resolvedLog"
}
$logInfo = Get-Item -LiteralPath $resolvedLog
if (($logInfo.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "Hardware log cannot be a symbolic link or reparse point: $resolvedLog"
}
if ($logInfo.Length -gt 16MB) {
    throw "Hardware log exceeds the 16 MiB offline-parser limit: $resolvedLog"
}

$problems = [System.Collections.Generic.List[string]]::new()
$records = [System.Collections.Generic.List[psobject]]::new()
$lineNumber = 0
foreach ($line in Get-Content -LiteralPath $resolvedLog) {
    ++$lineNumber
    if (-not $line.StartsWith('FABGLSTUDIO|', [System.StringComparison]::Ordinal)) {
        continue
    }
    $parts = $line -split '\|', 5
    if ($parts.Count -ne 5) {
        Add-Problem "line $lineNumber is a truncated protocol record"
        continue
    }
    if ($parts[0] -ne 'FABGLSTUDIO' -or $parts[1] -ne '1') {
        Add-Problem "line $lineNumber uses an unsupported protocol identifier or version"
        continue
    }
    if ($parts[2] -notin @('BOOT', 'PASS', 'FAIL', 'MANUAL', 'READY', 'METRIC')) {
        Add-Problem "line $lineNumber has unknown status '$($parts[2])'"
        continue
    }
    if ($parts[3] -notmatch '^[a-z][a-z0-9_]*$') {
        Add-Problem "line $lineNumber has malformed check name '$($parts[3])'"
        continue
    }
    $records.Add([pscustomobject]@{
            Line = $lineNumber
            Status = $parts[2]
            Check = $parts[3]
            Detail = Convert-Detail -Value $parts[4] -LineNumber $lineNumber
        })
}

if ($records.Count -eq 0) {
    Add-Problem 'no FABGLSTUDIO protocol records were found'
}
$bootRecords = @($records | Where-Object { $_.Status -eq 'BOOT' -and $_.Check -eq 'firmware' })
if ($bootRecords.Count -ne 1) {
    Add-Problem "expected exactly one BOOT|firmware record; found $($bootRecords.Count)"
}
elseif (-not $bootRecords[0].Detail.ContainsKey('profile') -or
    $bootRecords[0].Detail['profile'] -ne 'olimex-esp32-sbc-fabgl-revb') {
    Add-Problem 'BOOT record does not identify the locked Olimex Rev B profile'
}

$failureRecords = @($records | Where-Object Status -eq 'FAIL')
foreach ($record in $failureRecords) {
    Add-Problem "firmware reported FAIL for '$($record.Check)' on line $($record.Line)"
}

$requiredPassChecks = @(
    'serial', 'vga_init', 'renderer_2d', 'keyboard_detect', 'mouse_detect',
    'audio_pipeline', 'sd_mount', 'memory'
)
foreach ($check in $requiredPassChecks) {
    if (@($records | Where-Object { $_.Status -eq 'PASS' -and $_.Check -eq $check }).Count -eq 0) {
        Add-Problem "required PASS record is missing: $check"
    }
}
$psramPass = @($records | Where-Object {
        $_.Status -eq 'PASS' -and $_.Check -in @('psram_profile', 'psram')
    })
if ($psramPass.Count -eq 0) {
    Add-Problem 'required PASS record is missing: psram_profile or psram'
}

$sdPass = @($records | Where-Object { $_.Status -eq 'PASS' -and $_.Check -eq 'sd_mount' } |
        Select-Object -Last 1)
if ($sdPass.Count -eq 1) {
    $expectedSd = @{ bus = 'HSPI'; miso = '35'; mosi = '12'; clock = '14'; cs = '13' }
    foreach ($key in $expectedSd.Keys) {
        if (-not $sdPass[0].Detail.ContainsKey($key) -or
            $sdPass[0].Detail[$key] -ne $expectedSd[$key]) {
            Add-Problem "sd_mount PASS does not confirm $key=$($expectedSd[$key])"
        }
    }
}

$readyRecords = @($records | Where-Object { $_.Status -eq 'READY' -and $_.Check -eq 'diagnostics' })
if ($readyRecords.Count -ne 1) {
    Add-Problem "expected exactly one READY|diagnostics record; found $($readyRecords.Count)"
}
elseif (-not $readyRecords[0].Detail.ContainsKey('upload-command') -or
    $readyRecords[0].Detail['upload-command'] -ne 'absent' -or
    -not $readyRecords[0].Detail.ContainsKey('sd-write') -or
    $readyRecords[0].Detail['sd-write'] -ne 'false') {
    Add-Problem 'READY record does not confirm upload-command=absent and sd-write=false'
}
if (@($records | Where-Object { $_.Status -eq 'METRIC' -and $_.Check -eq 'runtime' }).Count -eq 0) {
    Add-Problem 'at least one METRIC|runtime record is required'
}

$manualChecks = @($records | Where-Object Status -eq 'MANUAL' |
        Select-Object -ExpandProperty Check -Unique | Sort-Object)
$summary = [ordered]@{
    schemaVersion = 1
    log = $resolvedLog
    automatedResult = if ($problems.Count -eq 0) { 'PASS' } else { 'FAIL' }
    recordCount = $records.Count
    failureRecordCount = $failureRecords.Count
    problems = @($problems)
    manualChecks = $manualChecks
    hardwareVerified = $false
    note = 'This offline parser cannot satisfy visual, audible, electrical, soak, or board-identity checks.'
}

if ($Json) {
    $summary | ConvertTo-Json -Depth 6
}
else {
    Write-Host "Automated log result: $($summary.automatedResult)"
    Write-Host "Protocol records: $($summary.recordCount)"
    if ($manualChecks.Count -gt 0) {
        Write-Host "Manual checks still required: $($manualChecks -join ', ')"
    }
    foreach ($problem in $problems) {
        Write-Host "FAIL: $problem"
    }
    Write-Host 'Hardware verified: false'
}
if ($problems.Count -ne 0) {
    exit 1
}
exit 0
