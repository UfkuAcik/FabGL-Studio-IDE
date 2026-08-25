[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$AssetCompiler
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$compiler = (Resolve-Path -LiteralPath $AssetCompiler).Path
$examplesRoot = (Resolve-Path -LiteralPath $PSScriptRoot).Path

function Invoke-AssetCompiler {
    param([string[]]$Arguments)
    & $compiler @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "asset compiler failed with exit code ${LASTEXITCODE}: $($Arguments -join ' ')"
    }
}

function Build-Base64Image {
    param(
        [string]$Source,
        [string]$Output
    )
    $encoded = (Get-Content -Raw -LiteralPath $Source).Trim()
    $temporary = [IO.Path]::ChangeExtension($Source, '.generated.bmp')
    try {
        [IO.File]::WriteAllBytes($temporary, [Convert]::FromBase64String($encoded))
        Invoke-AssetCompiler @('image', $temporary, $Output, '--colors', '16')
    }
    finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }
}

function Build-Base64Audio {
    param(
        [string]$Source,
        [string]$Output
    )
    $encoded = (Get-Content -Raw -LiteralPath $Source).Trim()
    $temporary = [IO.Path]::ChangeExtension($Source, '.generated.wav')
    try {
        [IO.File]::WriteAllBytes($temporary, [Convert]::FromBase64String($encoded))
        Invoke-AssetCompiler @('audio', $temporary, $Output, '--rate', '22050', '--compress',
            '--stream')
    }
    finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }
}

Build-Base64Image "$examplesRoot/source_assets/platformer/Player.bmp.b64" `
    "$examplesRoot/platformer/Assets/Player.fgli"
Build-Base64Image "$examplesRoot/source_assets/top_down/Player.bmp.b64" `
    "$examplesRoot/top_down/Assets/Player.fgli"
Build-Base64Image "$examplesRoot/source_assets/animation_showcase/Animated.bmp.b64" `
    "$examplesRoot/animation_showcase/Assets/Animated.fgli"
Build-Base64Image "$examplesRoot/source_assets/asset_streaming/Marker.bmp.b64" `
    "$examplesRoot/asset_streaming/Assets/Marker.fgli"
Invoke-AssetCompiler @('image', "$examplesRoot/asset_streaming/Assets/Tiles.png", `
    "$examplesRoot/asset_streaming/Assets/Tiles.fgli", '--colors', '8')
Build-Base64Image "$examplesRoot/source_assets/pseudo3d_racer/RacerSprite.bmp.b64" `
    "$examplesRoot/pseudo3d_racer/Assets/RacerSprite.fgli"
Build-Base64Audio "$examplesRoot/source_assets/audio_showcase/Tone.wav.b64" `
    "$examplesRoot/audio_showcase/Assets/Tone.fgla"

Invoke-AssetCompiler @('tilemap', "$examplesRoot/asset_streaming/Assets/World.csv", `
    "$examplesRoot/asset_streaming/Assets/World.fgltilemap", `
    '--guid', '50000000-0000-4000-8000-000000000010', `
    '--tile-width', '8', '--tile-height', '8', `
    '--tileset', '52000000-0000-4000-8000-000000000010:0:6')
Invoke-AssetCompiler @('tileset', `
    "$examplesRoot/asset_streaming/Assets/Tiles.fgltileset", `
    '--guid', '52000000-0000-4000-8000-000000000010', `
    '--name', 'Streaming Tiles', `
    '--image', '53000000-0000-4000-8000-000000000010', `
    '--tile-width', '8', '--tile-height', '8', `
    '--count', '6', '--columns', '6', '--collision', '1')
Invoke-AssetCompiler @('mesh', "$examplesRoot/tps_technology/Assets/Environment.obj", `
    "$examplesRoot/tps_technology/Assets/Environment.fglm")

Write-Host 'Example assets generated successfully.'
