param(
    [ValidateSet("host", "client")]
    [string]$Role,
    [string]$Peer = "127.0.0.1",
    [int]$Frames = 999999,
    [int]$WaitTimeoutMs = 86400000,
    [int]$InputDelayFrames = 4,
    [int]$InputSendDelayFrames = 0,
    [int]$InputSendJitterFrames = 0,
    [int]$InputMaxFrameLead = 4,
    [switch]$InputUnreliable,
    [int]$InputBundleHistory = 8,
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-rngconst-netaid.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-rngconst-netaid.tmp.nds",
    [string]$InputScript = "tests\nsmb_us_direct_mvl_manual_bootstrap.inputs",
    [string]$LogDir = "",
    [int]$SwapBuffersInterval = 1,
    [switch]$UseFrameLimit,
    [switch]$NoFrameLimit,
    [switch]$NoJit
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$smokeScript = Join-Path $PSScriptRoot "run-nsmb-mvl-lan-route-smoke.ps1"

if ($LogDir -eq "") {
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $LogDir = "logs\nsmb-mvl-manual-peer-$Role-$timestamp"
}

if (-not $InputUnreliable) {
    $InputUnreliable = $true
}

$params = @{
    RunRole = $Role
    Peer = $Peer
    Frames = $Frames
    WaitTimeoutMs = $WaitTimeoutMs
    Exe = $Exe
    Rom = "roms\nsmb-us.nds"
    InputScript = $InputScript
    ScreenshotInterval = 0
    NoHashLog = $true
    SkipMvlStateCheck = $true
    SkipGameplayActorCheck = $true
    NoLanMP = $true
    InputNetplay = $true
    InputDelayFrames = $InputDelayFrames
    InputSendDelayFrames = $InputSendDelayFrames
    InputSendJitterFrames = $InputSendJitterFrames
    InputMaxFrameLead = $InputMaxFrameLead
    PacketBridgeJitHelperPatch = $true
    PacketBridgeJitHelperPatchFrame = 870
    PacketBridgeStartFrame = 870
    LogDir = $LogDir
}

if ($UseFrameLimit -and $NoFrameLimit) {
    throw "UseFrameLimit and NoFrameLimit cannot be used together"
}

if ($NoFrameLimit) {
    $params.NoFrameLimit = $true
}

if ($Role -eq "host") {
    $params.HostRom = $HostRom
} else {
    $params.ClientRom = $ClientRom
}

if (-not $NoJit) {
    $params.AllowJit = $true
}

if ($InputUnreliable) {
    $params.InputUnreliable = $true
    $params.InputBundleHistory = $InputBundleHistory
}

Write-Host "Starting NSMB MvL peer session: role=$Role peer=$Peer"
Write-Host "input delay=$InputDelayFrames sendDelay=$InputSendDelayFrames sendJitter=$InputSendJitterFrames max frame lead=$InputMaxFrameLead unreliable=$($InputUnreliable.IsPresent) bundleHistory=$InputBundleHistory jit=$(-not $NoJit)"
Write-Host "frameLimit=$(-not $NoFrameLimit.IsPresent) swapBuffersInterval=$SwapBuffersInterval"
Write-Host "log=$LogDir"
Write-Host "Host controls Mario. Client controls Luigi."

Push-Location $repoRoot
try {
    $oldSwapBuffersInterval = $env:MELONDS_NSML_SWAPBUFFERS_INTERVAL
    if ($SwapBuffersInterval -gt 1) {
        $env:MELONDS_NSML_SWAPBUFFERS_INTERVAL = "$SwapBuffersInterval"
    } else {
        Remove-Item Env:\MELONDS_NSML_SWAPBUFFERS_INTERVAL -ErrorAction SilentlyContinue
    }

    $cfgPath = Join-Path $repoRoot "build\release-windows-x86_64\melonDS.toml"
    if (Test-Path $cfgPath) {
        $cfg = Get-Content $cfgPath -Raw
        $replacements = [ordered]@{
            'LimitFPS' = 'true'
            'UseGL' = 'true'
            'VSync' = 'false'
            'Renderer' = '2'
            'ScreenSizing' = '0'
            'ShowOSD' = 'false'
        }
        foreach ($key in $replacements.Keys) {
            $value = $replacements[$key]
            if ($cfg -match "(?m)^$key\s*=") {
                $cfg = $cfg -replace "(?m)^$key\s*=.*$", "$key = $value"
            } else {
                $cfg += "`n$key = $value"
            }
        }
        Set-Content -Path $cfgPath -Value $cfg -Encoding UTF8
    }

    & $smokeScript @params
} finally {
    if ($null -ne $oldSwapBuffersInterval) {
        $env:MELONDS_NSML_SWAPBUFFERS_INTERVAL = $oldSwapBuffersInterval
    } else {
        Remove-Item Env:\MELONDS_NSML_SWAPBUFFERS_INTERVAL -ErrorAction SilentlyContinue
    }
    Pop-Location
}
