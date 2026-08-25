[CmdletBinding()]
param(
    [string]$FixturePath,
    [switch]$IncludeBluetooth
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Get-UsbIdentity {
    param([string]$PnpDeviceId)

    $identity = [ordered]@{ vid = $null; pid = $null }
    if ($PnpDeviceId -match '(?i)VID_([0-9A-F]{4})') {
        $identity.vid = $matches[1].ToUpperInvariant()
    }
    if ($PnpDeviceId -match '(?i)PID_([0-9A-F]{4})') {
        $identity.pid = $matches[1].ToUpperInvariant()
    }
    return $identity
}

function Get-CandidateClassification {
    param(
        [string]$Name,
        [string]$PnpDeviceId,
        [string]$Vid,
        [string]$ProductId
    )

    $bluetooth = $Name -match '(?i)bluetooth' -or $PnpDeviceId -match '(?i)BTHENUM|BTHMODEM'
    $known = @{
        '1A86:7523' = 'WCH CH340/CH341 USB serial bridge used by Olimex boards'
        '1A86:55D4' = 'WCH CH9102/CH343 USB serial bridge'
        '10C4:EA60' = 'Silicon Labs CP210x USB serial bridge'
        '0403:6001' = 'FTDI FT232 USB serial bridge'
        '303A:1001' = 'Espressif native USB serial/JTAG'
    }
    $key = if ($Vid -and $ProductId) { "$Vid`:$ProductId" } else { '' }
    if ($bluetooth) {
        return [ordered]@{
            candidate = $false
            confidence = 'excluded'
            reason = 'Bluetooth serial ports are not firmware upload candidates by default.'
            bluetooth = $true
        }
    }
    if ($known.ContainsKey($key)) {
        return [ordered]@{
            candidate = $true
            confidence = if ($key -eq '1A86:7523') { 'high' } else { 'possible' }
            reason = [string]$known[$key]
            bluetooth = $false
        }
    }
    return [ordered]@{
        candidate = $false
        confidence = 'unknown'
        reason = if ($Vid -and $ProductId) {
            "Unrecognized USB serial bridge VID:PID $Vid`:$ProductId; manual inspection required."
        } else {
            'No trustworthy USB VID/PID identity was available; manual inspection required.'
        }
        bluetooth = $false
    }
}

function Get-RawPorts {
    if ($FixturePath) {
        $resolved = (Resolve-Path -LiteralPath $FixturePath).Path
        $fixture = Get-Content -LiteralPath $resolved -Raw | ConvertFrom-Json
        if ($fixture -isnot [System.Array]) {
            return @($fixture)
        }
        return @($fixture)
    }

    if ($env:OS -eq 'Windows_NT') {
        $devices = @(Get-CimInstance -ClassName Win32_PnPEntity -ErrorAction Stop |
            Where-Object { $_.Name -match '\(COM[1-9][0-9]{0,2}\)' })
        return @($devices | ForEach-Object {
                [pscustomobject]@{
                    name = [string]$_.Name
                    pnpDeviceId = [string]$_.PNPDeviceID
                }
            })
    }

    $paths = @('/dev/ttyUSB*', '/dev/ttyACM*', '/dev/cu.usbserial*', '/dev/cu.usbmodem*')
    return @($paths | ForEach-Object { Get-ChildItem -Path $_ -ErrorAction SilentlyContinue } |
        ForEach-Object {
            [pscustomobject]@{ name = [string]$_.FullName; pnpDeviceId = '' }
        })
}

$ports = @()
$seen = @{}
foreach ($raw in @(Get-RawPorts)) {
    $name = [string]$raw.name
    $pnpDeviceId = [string]$raw.pnpDeviceId
    $port = if ($name -match '(?i)\((COM[1-9][0-9]{0,2})\)') {
        $matches[1].ToUpperInvariant()
    } elseif ($name -match '^/dev/') {
        $name
    } else {
        continue
    }
    if ($seen.ContainsKey($port)) {
        continue
    }
    $seen[$port] = $true
    $identity = Get-UsbIdentity -PnpDeviceId $pnpDeviceId
    $classification = Get-CandidateClassification -Name $name -PnpDeviceId $pnpDeviceId `
        -Vid $identity.vid -ProductId $identity.pid
    if ($classification.bluetooth -and -not $IncludeBluetooth) {
        continue
    }
    $ports += [pscustomobject][ordered]@{
        port = $port
        displayName = $name
        pnpDeviceId = $pnpDeviceId
        vid = $identity.vid
        pid = $identity.pid
        boardCandidate = [bool]$classification.candidate
        confidence = [string]$classification.confidence
        reason = [string]$classification.reason
        bluetooth = [bool]$classification.bluetooth
        requiresUserConfirmation = $true
    }
}

$orderedPorts = @($ports | Sort-Object -Property @{ Expression = 'boardCandidate'; Descending = $true },
    @{ Expression = 'port'; Descending = $false })
[ordered]@{
    schemaVersion = 1
    operation = 'read-only-port-detection'
    uploadPerformed = $false
    portOpened = $false
    count = $orderedPorts.Count
    ports = $orderedPorts
} | ConvertTo-Json -Depth 6
