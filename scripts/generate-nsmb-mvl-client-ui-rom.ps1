param(
    [string]$BaseRom = "roms\nsmb-us-direct-mvl-entry-entranceff-flag1-p1outviewslot0-loop2.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-entranceff-flag1-p1outviewslot0-loop2-client-ui.tmp.nds"
)

$ErrorActionPreference = "Stop"

if (!(Test-Path $BaseRom)) {
    throw "Base ROM not found: $BaseRom"
}

$stageFxRom = [System.IO.Path]::ChangeExtension($ClientRom, ".stagefx.tmp.nds")

& python tools\nsmb_us_rom_patch.py `
    --rom $BaseRom `
    --out $stageFxRom `
    stagefx-display-player-id --player-id 1

& python tools\nsmb_us_rom_patch.py `
    --rom $stageFxRom `
    --out $ClientRom `
    stage-layout-inventory-display-player-id --player-id 1 --mode hud

Remove-Item -Force $stageFxRom -ErrorAction SilentlyContinue
Write-Host "wrote stable client UI ROM: $ClientRom"
