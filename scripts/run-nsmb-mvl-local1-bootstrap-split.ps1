param(
    [int]$Frames = 3000,
    [int]$Port = 8330,
    [ValidateSet("both", "host", "client")]
    [string]$RunRole = "both",
    [string]$Peer = "127.0.0.1",
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-local1-host.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-local1-client-overlay0all-range0-vertical0.tmp.nds",
    [string]$InputScript = "tests\nsmb_us_direct_mvl_safe_short.inputs",
    [string]$HostLogDir = "logs\nsmvl-local1-bootstrap-host",
    [string]$ClientLogDir = "logs\nsmvl-local1-bootstrap-client",
    [int]$SwitchFrame = 900,
    [int]$LookupTickDelay = 60,
    [int]$MaxFrameLead = 20,
    [int]$ScreenshotInterval = 300,
    [int]$GameStateTraceInterval = 60,
    [int]$WaitTimeoutMs = 1200000,
    [int]$JobTimeoutSeconds = 1260,
    [switch]$RegenerateRoms,
    [switch]$SkipVerify,
    [switch]$RequireNoLifeLoss,
    [switch]$NoRequireInput,
    [switch]$AllowJitWithPacketBridge,
    [switch]$NoScreenshots,
    [switch]$NoGameStateTrace,
    [switch]$NoHashLog,
    [switch]$TracePlayerRender,
    [int]$TracePlayerRenderStartFrame = 0,
    [int]$TracePlayerRenderEndFrame = 0
)

$ErrorActionPreference = "Stop"

if ($RegenerateRoms -or !(Test-Path $HostRom) -or !(Test-Path $ClientRom)) {
    & .\scripts\generate-nsmb-mvl-local1-bootstrap-roms.ps1 `
        -HostRom $HostRom `
        -ClientRom $ClientRom
}

$argsForRun = @{
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
    HostGameLocalPlayerID = "0"
    ClientGameLocalPlayerID = "1"
    GameLocalPlayerIDStartFrame = $SwitchFrame
    LookupTickDelay = $LookupTickDelay
    MaxFrameLead = $MaxFrameLead
    ScreenshotInterval = $ScreenshotInterval
    GameStateTraceInterval = $GameStateTraceInterval
    WaitTimeoutMs = $WaitTimeoutMs
    JobTimeoutSeconds = $JobTimeoutSeconds
}
if ($AllowJitWithPacketBridge) { $argsForRun.AllowJitWithPacketBridge = $true }
if ($NoScreenshots) { $argsForRun.NoScreenshots = $true }
if ($NoGameStateTrace) { $argsForRun.NoGameStateTrace = $true }
if ($NoHashLog) { $argsForRun.NoHashLog = $true }
if ($TracePlayerRender) {
    $argsForRun.TracePlayerRender = $true
    $argsForRun.TracePlayerRenderStartFrame = $TracePlayerRenderStartFrame
    $argsForRun.TracePlayerRenderEndFrame = $TracePlayerRenderEndFrame
}

& .\scripts\run-nsmb-mvl-standard-split.ps1 @argsForRun

if ($RunRole -eq "both" -and -not $SkipVerify -and -not $NoGameStateTrace) {
    $argsForVerify = @{
        HostLogDir = $HostLogDir
        ClientLogDir = $ClientLogDir
        FromFrame = $SwitchFrame
        ToFrame = $Frames
        RequireStageVisibleScreenshots = (-not $NoScreenshots)
        RequirePlayerVisibleScreenshots = (-not $NoScreenshots)
    }
    if ($RequireNoLifeLoss) {
        $argsForVerify.RequireNoLifeLossUntilFrame = $Frames
    }
    if ($NoRequireInput) {
        $argsForVerify.NoRequireInput = $true
    }
    & .\scripts\verify-nsmb-mvl-stable-split.ps1 @argsForVerify
}
