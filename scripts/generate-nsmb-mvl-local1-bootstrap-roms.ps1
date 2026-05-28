param(
    [string]$SourceRom = "roms\nsmb-us.nds",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-local1-host.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-local1-client-overlay0all.tmp.nds"
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

& python tools\nsmb_us_rom_patch.py `
    --rom $HostRom `
    --out $ClientRom `
    overlay0-localplayer-literal-alias --mode all

Write-Host "wrote local1 bootstrap host ROM: $HostRom"
Write-Host "wrote local1 bootstrap client ROM: $ClientRom"
