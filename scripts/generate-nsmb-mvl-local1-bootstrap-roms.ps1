param(
    [string]$SourceRom = "roms\nsmb-us.nds",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-local1-host.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-local1-client-overlay0all-range0-vertical0.tmp.nds"
)

$ErrorActionPreference = "Stop"

if (!(Test-Path $SourceRom)) {
    throw "Source ROM not found: $SourceRom"
}

$hostDirectRom = [System.IO.Path]::ChangeExtension($HostRom, ".direct.tmp.nds")

& python tools\nsmb_us_rom_patch.py `
    --rom $SourceRom `
    --out $hostDirectRom `
    direct-mvl-entry `
    --entrance 0xff `
    --flag 1 `
    --force-ready-progress `
    --force-transfer-result 8 `
    --clear-actor-category-mask `
    --force-scene-settings 0xb4ff00 `
    --call-load-mvsl-files-after `
    --camera-player1-out-of-view-slot0 `
    --camera-focus-loop-count 2 `
    --player-signal-locked-noop

& python tools\nsmb_us_rom_patch.py `
    --rom $hostDirectRom `
    --out $HostRom `
    player-render-model-visible

$clientOverlayRom = [System.IO.Path]::ChangeExtension($ClientRom, ".overlay0all.tmp.nds")
$clientRangeRom = [System.IO.Path]::ChangeExtension($ClientRom, ".range0.tmp.nds")

& python tools\nsmb_us_rom_patch.py `
    --rom $HostRom `
    --out $clientOverlayRom `
    overlay0-localplayer-literal-alias --mode all-no-inventory

& python tools\nsmb_us_rom_patch.py `
    --rom $clientOverlayRom `
    --out $clientRangeRom `
    player-render-range-view-player-id --player-id 0

& python tools\nsmb_us_rom_patch.py `
    --rom $clientRangeRom `
    --out $ClientRom `
    stage-camera-state-vertical-slot-zero

$clientInventoryRom = [System.IO.Path]::ChangeExtension($ClientRom, ".inventory-use1.tmp.nds")
Move-Item -Force $ClientRom $clientInventoryRom
& python tools\nsmb_us_rom_patch.py `
    --rom $clientInventoryRom `
    --out $ClientRom `
    stage-layout-inventory-use-player-id --player-id 1

Remove-Item -Force $hostDirectRom, $clientOverlayRom, $clientRangeRom, $clientInventoryRom -ErrorAction SilentlyContinue

Write-Host "wrote local1 bootstrap host ROM: $HostRom"
Write-Host "wrote local1 bootstrap client ROM: $ClientRom"
