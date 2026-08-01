[CmdletBinding()]
param(
    [switch]$AsJson
)

$ErrorActionPreference = 'Stop'

function Get-CommandVersion {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [string[]]$Arguments = @('--version')
    )

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        return [ordered]@{ found = $false; path = $null; version = $null }
    }

    $versionText = $null
    try {
        $versionText = (& $command.Source @Arguments 2>&1 | Select-Object -First 1).ToString().Trim()
    }
    catch {
        $versionText = "version probe failed: $($_.Exception.Message)"
    }

    return [ordered]@{
        found = $true
        path = $command.Source
        version = $versionText
    }
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$drive = Get-PSDrive -Name ([System.IO.Path]::GetPathRoot($repositoryRoot).Substring(0, 1))
$ports = @()
try {
    $ports = @(Get-CimInstance Win32_SerialPort -ErrorAction Stop | ForEach-Object {
        [ordered]@{
            device = $_.DeviceID
            name = $_.Name
            pnpDeviceId = $_.PNPDeviceID
        }
    })
}
catch {
    $ports = @([ordered]@{ error = $_.Exception.Message })
}

$result = [ordered]@{
    repositoryRoot = $repositoryRoot
    operatingSystem = [System.Environment]::OSVersion.VersionString
    architecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    freeSpaceGiB = [math]::Round($drive.Free / 1GB, 2)
    tools = [ordered]@{
        git = Get-CommandVersion git
        cmake = Get-CommandVersion cmake
        ninja = Get-CommandVersion ninja
        cxx = Get-CommandVersion g++
        clang = Get-CommandVersion clang++
        python = Get-CommandVersion python @('--version')
        arduinoCli = Get-CommandVersion arduino-cli @('version')
        platformio = Get-CommandVersion pio @('--version')
        qtpaths = Get-CommandVersion qtpaths6 @('--qt-version')
    }
    serialPorts = $ports
}

if ($AsJson) {
    $result | ConvertTo-Json -Depth 6
    exit 0
}

Write-Host "FabGL Studio environment"
Write-Host "Repository: $repositoryRoot"
Write-Host "OS: $($result.operatingSystem) ($($result.architecture))"
Write-Host "Free space: $($result.freeSpaceGiB) GiB"
foreach ($entry in $result.tools.GetEnumerator()) {
    $state = if ($entry.Value.found) { $entry.Value.version } else { 'not found' }
    Write-Host ("{0,-12} {1}" -f $entry.Key, $state)
}
Write-Host 'Serial ports:'
if ($ports.Count -eq 0) {
    Write-Host '  none'
}
else {
    foreach ($port in $ports) {
        if ($port.error) {
            Write-Host "  probe failed: $($port.error)"
        }
        else {
            Write-Host "  $($port.device): $($port.name) [$($port.pnpDeviceId)]"
        }
    }
}
