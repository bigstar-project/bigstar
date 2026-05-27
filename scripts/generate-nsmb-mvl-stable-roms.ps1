param(
    [string]$SourceRom = "roms\nsmb-us.nds",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-ui.tmp.nds"
)

$ErrorActionPreference = "Stop"

if (!(Test-Path $SourceRom)) {
    throw "Source ROM not found: $SourceRom"
}

& python tools\nsmb_us_rom_patch.py `
    --rom $SourceRom `
    --out $HostRom `
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

& .\scripts\generate-nsmb-mvl-client-ui-rom.ps1 `
    -BaseRom $HostRom `
    -ClientRom $ClientRom

Write-Host "wrote stable host ROM: $HostRom"
Write-Host "wrote stable client UI ROM: $ClientRom"
