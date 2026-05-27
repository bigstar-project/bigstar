param(
    [string]$SourceRom = "roms\nsmb-us.nds",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host-hybrid-render.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-hybrid-render.tmp.nds",
    [switch]$NoRenderVisiblePatch,
    [switch]$PatchStageEntitySkipRender
)

$ErrorActionPreference = "Stop"

if (!(Test-Path $SourceRom)) {
    throw "Source ROM not found: $SourceRom"
}

$directHostRom = if ($NoRenderVisiblePatch) {
    $HostRom
} else {
    [System.IO.Path]::ChangeExtension($HostRom, ".direct.tmp.nds")
}

& python tools\nsmb_us_rom_patch.py `
    --rom $SourceRom `
    --out $directHostRom `
    direct-mvl-entry `
    --entrance 0xff `
    --flag 1 `
    --force-ready-progress `
    --force-transfer-result 8 `
    --clear-actor-category-mask `
    --force-scene-settings 0xb4ff00 `
    --call-load-mvsl-files-after `
    --camera-player1-out-of-view-slot0 `
    --camera-focus-loop-count 2

if (-not $NoRenderVisiblePatch) {
    & python tools\nsmb_us_rom_patch.py `
        --rom $directHostRom `
        --out $HostRom `
        player-render-model-visible
}

$cameraRom = [System.IO.Path]::ChangeExtension($ClientRom, ".camera.tmp.nds")
$stageFxRom = [System.IO.Path]::ChangeExtension($ClientRom, ".stagefx.tmp.nds")
$inventoryRom = [System.IO.Path]::ChangeExtension($ClientRom, ".inventory.tmp.nds")

& python tools\nsmb_us_rom_patch.py `
    --rom $HostRom `
    --out $cameraRom `
    stage-camera-player-id --player-id 1

& python tools\nsmb_us_rom_patch.py `
    --rom $cameraRom `
    --out $stageFxRom `
    stagefx-display-player-id --player-id 1

& python tools\nsmb_us_rom_patch.py `
    --rom $stageFxRom `
    --out $inventoryRom `
    stage-layout-inventory-display-player-id --player-id 1 --mode hud

if ($PatchStageEntitySkipRender) {
    & python tools\nsmb_us_rom_patch.py `
        --rom $inventoryRom `
        --out $ClientRom `
        stage-entity-skip-render-player-id --player-id 1
} else {
    Copy-Item -Force $inventoryRom $ClientRom
}

Remove-Item -Force $cameraRom, $stageFxRom, $inventoryRom -ErrorAction SilentlyContinue
if (-not $NoRenderVisiblePatch) {
    Remove-Item -Force $directHostRom -ErrorAction SilentlyContinue
}

Write-Host "wrote hybrid host ROM: $HostRom"
Write-Host "wrote hybrid client ROM: $ClientRom"
