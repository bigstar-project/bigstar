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
$hostWifiRom = [System.IO.Path]::ChangeExtension($HostRom, ".wificount2.tmp.nds")
$clientDirectRom = [System.IO.Path]::ChangeExtension($ClientRom, ".direct.tmp.nds")
$clientWifiRom = [System.IO.Path]::ChangeExtension($ClientRom, ".wificount2.tmp.nds")

& python tools\nsmb_us_rom_patch.py `
    --rom $SourceRom `
    --out $hostDirectRom `
    direct-mvl-entry `
    --player-id 0 `
    --entrance 0xff `
    --flag 1 `
    --force-ready-progress `
    --force-transfer-result 8 `
    --clear-actor-category-mask `
    --force-scene-settings 0xb4ff00 `
    --call-load-mvsl-files-after `
    --camera-player1-out-of-view-slot0 `
    --camera-focus-loop-count 2 `
    --player-stage-lock-vsmode-noop

& python tools\nsmb_us_rom_patch.py `
    --rom $hostDirectRom `
    --out $hostWifiRom `
    wifi-communicating-consoles --count 2

& python tools\nsmb_us_rom_patch.py `
    --rom $hostWifiRom `
    --out $HostRom `
    rng-constant --value 0x100

& python tools\nsmb_us_rom_patch.py `
    --rom $SourceRom `
    --out $clientDirectRom `
    direct-mvl-entry `
    --player-id 1 `
    --entrance 0xff `
    --flag 1 `
    --force-ready-progress `
    --force-transfer-result 8 `
    --clear-actor-category-mask `
    --force-scene-settings 0xb4ff00 `
    --call-load-mvsl-files-after `
    --camera-player1-out-of-view-slot0 `
    --camera-focus-loop-count 2 `
    --player-stage-lock-vsmode-noop

& python tools\nsmb_us_rom_patch.py `
    --rom $clientDirectRom `
    --out $clientWifiRom `
    wifi-communicating-consoles --count 2

& python tools\nsmb_us_rom_patch.py `
    --rom $clientWifiRom `
    --out $ClientRom `
    rng-constant --value 0x100

Remove-Item -Force $hostDirectRom, $hostWifiRom, $clientDirectRom, $clientWifiRom -ErrorAction SilentlyContinue

Write-Host "wrote local1 bootstrap host ROM: $HostRom"
Write-Host "wrote local1 bootstrap client ROM: $ClientRom"
