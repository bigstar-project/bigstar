param(
    [int]$Frames = 3000,
    [ValidateSet("both", "host", "client")]
    [string]$RunRole = "both",
    [string]$Peer = "127.0.0.1",
    [int]$Port = 8237,
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$SourceRom = "roms\nsmb-us.nds",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host-hybrid-render.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-hybrid-render.tmp.nds",
    [string]$InputScript = "tests\nsmb_us_direct_mvl_avoid_goomba.inputs",
    [string]$LogDir = "logs\nsmvl-hybrid-split",
    [int]$PacketBridgeStartFrame = 1500,
    [int]$PacketBridgeForceTickBase = 1536,
    [int]$LookupTickDelay = 60,
    [int]$MaxFrameLead = 20,
    [int]$ThrottleTimeoutMs = 250,
    [int]$ScreenshotInterval = 300,
    [int]$GameStateTraceInterval = 60,
    [int]$WaitTimeoutMs = 1200000,
    [switch]$RegenerateRoms,
    [switch]$NoRenderVisiblePatch,
    [switch]$PatchStageEntitySkipRender,
    [switch]$HashLog,
    [switch]$SkipVerify
)

$ErrorActionPreference = "Stop"

function Format-Arg {
    param([string]$Value)
    if ($Value.StartsWith("-")) {
        return $Value
    }
    return "'" + ($Value -replace "'", "''") + "'"
}

if ($RegenerateRoms -or !(Test-Path $HostRom) -or !(Test-Path $ClientRom)) {
    $generateArgs = @(
        "-SourceRom", $SourceRom,
        "-HostRom", $HostRom,
        "-ClientRom", $ClientRom
    )
    if ($PatchStageEntitySkipRender) {
        $generateArgs += "-PatchStageEntitySkipRender"
    }
    if ($NoRenderVisiblePatch) {
        $generateArgs += "-NoRenderVisiblePatch"
    }
    $generateCmd = "& .\scripts\generate-nsmb-mvl-hybrid-roms.ps1 " + (($generateArgs | ForEach-Object { Format-Arg $_ }) -join " ")
    Invoke-Expression $generateCmd
}

$runArgs = @(
    "-RunRole", $RunRole,
    "-Frames", "$Frames",
    "-WaitTimeoutMs", "$WaitTimeoutMs",
    "-Exe", $Exe,
    "-Rom", $HostRom,
    "-ClientRom", $ClientRom,
    "-InputScript", $InputScript,
    "-LogDir", $LogDir,
    "-PacketBridge",
    "-PacketBridgeDirectCapture",
    "-PacketBridgeForceTick",
    "-PacketBridgeForceTickStartFrame", "$PacketBridgeStartFrame",
    "-PacketBridgeForceTickBase", "$PacketBridgeForceTickBase",
    "-PacketBridgeMaxFrameLead", "$MaxFrameLead",
    "-PacketBridgeThrottleStartFrame", "$PacketBridgeStartFrame",
    "-PacketBridgeThrottleTimeoutMs", "$ThrottleTimeoutMs",
    "-PacketBridgePort", "$Port",
    "-PacketBridgeStartFrame", "$PacketBridgeStartFrame",
    "-PacketBridgeLookupTickDelay", "$LookupTickDelay",
    "-PacketBridgeLiveFallbackWindow", "4",
    "-PacketBridgeLiveFallbackLatestBefore",
    "-PacketBridgeReplayReturnLookupTick",
    "-PacketBridgeReplayOps", "keys,byte,tick,action",
    "-PacketBridgeNeutralizeLocalInput",
    "-HostPacketBridgeLocalPlayer", "0",
    "-ClientPacketBridgeLocalPlayer", "1",
    "-HostPacketBridgeForceGameLocalPlayerID", "0",
    "-ClientPacketBridgeForceGameLocalPlayerID", "0",
    "-PacketBridgeForceGameLocalPlayerIDStartFrame", "0",
    "-PacketBridgeForceGameLocalPlayerIDEarly",
    "-NetRandomValue", "0x12345678",
    "-NetRandomAuto",
    "-GameStateTrace",
    "-GameStateTraceExtended",
    "-GameStateTraceInterval", "$GameStateTraceInterval",
    "-ScreenshotInterval", "$ScreenshotInterval"
)

if ($RunRole -eq "client") {
    $runArgs += @("-Peer", $Peer)
}
if (-not $HashLog) {
    $runArgs += "-NoHashLog"
}

$runCmd = "& .\scripts\run-nsmb-mvl-lan-route-smoke.ps1 " + (($runArgs | ForEach-Object { Format-Arg $_ }) -join " ")
Invoke-Expression $runCmd

if (-not $SkipVerify -and $RunRole -eq "both") {
    & .\scripts\verify-nsmb-mvl-lan-result.ps1 `
        -LogDir $LogDir `
        -FromFrame $PacketBridgeStartFrame `
        -ToFrame $Frames `
        -RequirePlayer0Input `
        -RequirePlayer1Input `
        -RequireStageVisibleScreenshots
}
