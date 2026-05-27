param(
    [string]$BaseRom = "roms\nsmb-us-direct-mvl-entry-stable-host.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-play.tmp.nds"
)

$ErrorActionPreference = "Stop"

if (!(Test-Path $BaseRom)) {
    throw "Base ROM not found: $BaseRom"
}

$stageFxRom = [System.IO.Path]::ChangeExtension($ClientRom, ".stagefx.tmp.nds")
$layoutRom = [System.IO.Path]::ChangeExtension($ClientRom, ".layout.tmp.nds")
$cameraStateRom = [System.IO.Path]::ChangeExtension($ClientRom, ".camera-state.tmp.nds")
$cameraUpdateRom = [System.IO.Path]::ChangeExtension($ClientRom, ".camera-update.tmp.nds")

& python tools\nsmb_us_rom_patch.py `
    --rom $BaseRom `
    --out $stageFxRom `
    stagefx-display-player-id --player-id 1

& python tools\nsmb_us_rom_patch.py `
    --rom $stageFxRom `
    --out $layoutRom `
    stage-layout-inventory-display-player-id --player-id 1 --mode all-read

& python tools\nsmb_us_rom_patch.py `
    --rom $layoutRom `
    --out $cameraStateRom `
    stage-camera-state-player-id --player-id 1

& python tools\nsmb_us_rom_patch.py `
    --rom $cameraStateRom `
    --out $cameraUpdateRom `
    stage-camera-player-id --player-id 1

& python tools\nsmb_us_rom_patch.py `
    --rom $cameraUpdateRom `
    --out $ClientRom `
    stage-set-zoom-camera-player-id --player-id 1

Remove-Item -Force $stageFxRom,$layoutRom,$cameraStateRom,$cameraUpdateRom -ErrorAction SilentlyContinue
Write-Host "wrote client play ROM: $ClientRom"
