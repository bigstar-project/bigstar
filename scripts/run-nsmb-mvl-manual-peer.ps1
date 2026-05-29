param(
    [ValidateSet("host", "client")]
    [string]$Role,
    [string]$Peer = "127.0.0.1",
    [int]$Frames = 999999,
    [int]$WaitTimeoutMs = 86400000,
    [int]$InputDelayFrames = 4,
    [int]$InputMaxFrameLead = 4,
    [switch]$InputUnreliable,
    [int]$InputBundleHistory = 8,
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-rngconst-netaid.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-rngconst-netaid.tmp.nds",
    [string]$InputScript = "tests\nsmb_us_direct_mvl_manual_bootstrap.inputs",
    [string]$LogDir = "",
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

$argsList = @(
    "-RunRole", $Role,
    "-Peer", $Peer,
    "-Frames", "$Frames",
    "-WaitTimeoutMs", "$WaitTimeoutMs",
    "-Exe", $Exe,
    "-Rom", "roms\nsmb-us.nds",
    "-InputScript", $InputScript,
    "-ScreenshotInterval", "0",
    "-NoHashLog",
    "-SkipMvlStateCheck",
    "-SkipGameplayActorCheck",
    "-InputNetplay",
    "-InputDelayFrames", "$InputDelayFrames",
    "-InputMaxFrameLead", "$InputMaxFrameLead",
    "-PacketBridgeJitHelperPatch",
    "-PacketBridgeJitHelperPatchFrame", "900",
    "-PacketBridgeStartFrame", "900",
    "-LogDir", $LogDir
)

if ($Role -eq "host") {
    $argsList += @(
        "-HostRom", $HostRom,
        "-RequireHostLocalPlayerID", "0",
        "-RequireHostNetLocalAid", "0"
    )
} else {
    $argsList += @(
        "-ClientRom", $ClientRom,
        "-RequireClientLocalPlayerID", "1",
        "-RequireClientNetLocalAid", "1"
    )
}

if (-not $NoJit) {
    $argsList += "-AllowJit"
}

if ($InputUnreliable) {
    $argsList += @("-InputUnreliable", "-InputBundleHistory", "$InputBundleHistory")
}

Write-Host "Starting NSMB MvL peer session: role=$Role peer=$Peer"
Write-Host "input delay=$InputDelayFrames max frame lead=$InputMaxFrameLead unreliable=$($InputUnreliable.IsPresent) bundleHistory=$InputBundleHistory jit=$(-not $NoJit)"
Write-Host "log=$LogDir"
Write-Host "Host controls Mario. Client controls Luigi."

Push-Location $repoRoot
try {
    & $smokeScript @argsList
} finally {
    Pop-Location
}
