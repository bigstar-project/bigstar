param(
    [int]$Frames = 3600,
    [int]$Port = 8181,
    [ValidateSet("both", "host", "client")]
    [string]$RunRole = "both",
    [string]$Peer = "127.0.0.1",
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-ui.tmp.nds",
    [string]$HostLogDir = "logs\smvl-stable-host",
    [string]$ClientLogDir = "logs\smvl-stable-client",
    [switch]$GenerateRoms,
    [switch]$NoScreenshots,
    [switch]$NoGameStateTrace,
    [switch]$NoHashLog,
    [switch]$NoFrameLimit,
    [switch]$AllowJitWithPacketBridge
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
    HostLogDir = $HostLogDir
    ClientLogDir = $ClientLogDir
    NoScreenshots = $NoScreenshots
    NoGameStateTrace = $NoGameStateTrace
    NoHashLog = $NoHashLog
    NoFrameLimit = $NoFrameLimit
    AllowJitWithPacketBridge = $AllowJitWithPacketBridge
}

& .\scripts\run-nsmb-mvl-standard-split.ps1 @standardArgs
