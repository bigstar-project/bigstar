param(
    [int]$Frames = 3600,
    [int]$Port = 8181,
    [ValidateSet("both", "host", "client")]
    [string]$RunRole = "both",
    [string]$Peer = "127.0.0.1",
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-ui.tmp.nds",
    [string]$InputScript = "tests\nsmb_us_direct_mvl_both_different.inputs",
    [string]$HostLogDir = "logs\smvl-stable-host",
    [string]$ClientLogDir = "logs\smvl-stable-client",
    [int]$LookupTickDelay = 10,
    [int]$SendDelayFrames = 0,
    [int]$SendJitterFrames = 0,
    [int]$MaxFrameLead = 8,
    [int]$ScreenshotInterval = 900,
    [int]$GameStateTraceInterval = 60,
    [int]$WaitTimeoutMs = 720000,
    [int]$JobTimeoutSeconds = 780,
    [switch]$GenerateRoms,
    [switch]$NoScreenshots,
    [switch]$NoGameStateTrace,
    [switch]$NoHashLog,
    [switch]$NoFrameLimit,
    [switch]$FixedFrameTime,
    [double]$TargetFps = 0.0,
    [switch]$AllowJitWithPacketBridge,
    [switch]$PacketBridgeTrace,
    [switch]$PacketBridgePreserveLocalTouch
)

$ErrorActionPreference = "Stop"

if ($GenerateRoms -or !(Test-Path $HostRom) -or !(Test-Path $ClientRom)) {
    & .\scripts\generate-nsmb-mvl-stable-roms.ps1 -HostRom $HostRom -ClientRom $ClientRom
}

$standardArgs = @{
    Frames = $Frames
    Port = $Port
    RunRole = $RunRole
    Peer = $Peer
    Exe = $Exe
    HostRom = $HostRom
    ClientRom = $ClientRom
    InputScript = $InputScript
    HostLogDir = $HostLogDir
    ClientLogDir = $ClientLogDir
    LookupTickDelay = $LookupTickDelay
    SendDelayFrames = $SendDelayFrames
    SendJitterFrames = $SendJitterFrames
    MaxFrameLead = $MaxFrameLead
    ScreenshotInterval = $ScreenshotInterval
    GameStateTraceInterval = $GameStateTraceInterval
    WaitTimeoutMs = $WaitTimeoutMs
    JobTimeoutSeconds = $JobTimeoutSeconds
    NoScreenshots = $NoScreenshots
    NoGameStateTrace = $NoGameStateTrace
    NoHashLog = $NoHashLog
    NoFrameLimit = $NoFrameLimit
    FixedFrameTime = $FixedFrameTime
    TargetFps = $TargetFps
    AllowJitWithPacketBridge = $AllowJitWithPacketBridge
    PacketBridgeTrace = $PacketBridgeTrace
    PacketBridgePreserveLocalTouch = $PacketBridgePreserveLocalTouch
}

& .\scripts\run-nsmb-mvl-standard-split.ps1 @standardArgs
