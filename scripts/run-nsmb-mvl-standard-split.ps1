param(
    [int]$Frames = 3600,
    [int]$Port = 8181,
    [ValidateSet("both", "host", "client")]
    [string]$RunRole = "both",
    [string]$Peer = "127.0.0.1",
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-entranceff-flag1.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-entranceff-flag1-camera-full-p1.nds",
    [string]$InputScript = "tests\nsmb_us_direct_mvl_both_different.inputs",
    [string]$HostLogDir = "logs\nsmvl-standard-split-host",
    [string]$ClientLogDir = "logs\nsmvl-standard-split-client",
    [int]$LookupTickDelay = 10,
    [int]$SendDelayFrames = 0,
    [int]$SendJitterFrames = 0,
    [int]$ScreenshotInterval = 900,
    [int]$GameStateTraceInterval = 60,
    [int]$WaitTimeoutMs = 720000,
    [int]$JobTimeoutSeconds = 780,
    [switch]$NoDirectCapture,
    [int]$PlayerStickToStarStartFrame = 0,
    [int]$PlayerStickToStarEndFrame = 0,
    [int]$PlayerStickToStarSlot = 0
)

$ErrorActionPreference = "Stop"

function Format-Arg {
    param([string]$Value)
    if ($Value.StartsWith("-")) {
        return $Value
    }
    return "'" + ($Value -replace "'", "''") + "'"
}

$common = @(
    "-Exe", $Exe,
    "-Rom", $HostRom,
    "-InputScript", $InputScript,
    "-Frames", "$Frames",
    "-WaitTimeoutMs", "$WaitTimeoutMs",
    "-GameStateTrace",
    "-GameStateTraceExtended",
    "-GameStateTraceInterval", "$GameStateTraceInterval",
    "-ScreenshotInterval", "$ScreenshotInterval",
    "-PacketBridge",
    "-PacketBridgePort", "$Port",
    "-PacketBridgeStartFrame", "1500",
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
    "-PacketBridgeMaxFrameLead", "8",
    "-PacketBridgeThrottleStartFrame", "1500",
    "-NetRandomValue", "0x12345678",
    "-NetRandomAuto"
)

if (-not $NoDirectCapture) {
    $common += "-PacketBridgeDirectCapture"
}
if ($SendDelayFrames -gt 0) {
    $common += @("-PacketBridgeSendDelayFrames", "$SendDelayFrames")
}
if ($SendJitterFrames -gt 0) {
    $common += @("-PacketBridgeSendJitterFrames", "$SendJitterFrames")
}
if ($PlayerStickToStarStartFrame -gt 0 -or $PlayerStickToStarEndFrame -gt 0) {
    $common += @(
        "-PlayerStickToStarStartFrame", "$PlayerStickToStarStartFrame",
        "-PlayerStickToStarEndFrame", "$PlayerStickToStarEndFrame",
        "-PlayerStickToStarSlot", "$PlayerStickToStarSlot"
    )
}

$hostArgs = @("-RunRole", "host", "-LogDir", $HostLogDir) + $common
$clientArgs = @(
    "-RunRole", "client",
    "-Peer", $Peer,
    "-ClientRom", $ClientRom,
    "-LogDir", $ClientLogDir
) + $common

$hostCmd = "& .\scripts\run-nsmb-mvl-lan-route-smoke.ps1 " + (($hostArgs | ForEach-Object { Format-Arg $_ }) -join " ")
$clientCmd = "& .\scripts\run-nsmb-mvl-lan-route-smoke.ps1 " + (($clientArgs | ForEach-Object { Format-Arg $_ }) -join " ")

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$jobs = @()
if ($RunRole -eq "both" -or $RunRole -eq "host") {
    $jobs += Start-Job -ScriptBlock {
        param($Root, $Command)
        Set-Location $Root
        Invoke-Expression $Command
    } -ArgumentList $repoRoot, $hostCmd
}

if ($RunRole -eq "both") {
    Start-Sleep -Seconds 3
}

if ($RunRole -eq "both" -or $RunRole -eq "client") {
    $jobs += Start-Job -ScriptBlock {
        param($Root, $Command)
        Set-Location $Root
        Invoke-Expression $Command
    } -ArgumentList $repoRoot, $clientCmd
}

Wait-Job $jobs -Timeout $JobTimeoutSeconds | Out-Null

$failed = @()
foreach ($job in $jobs) {
    if ($job.State -eq "Running") {
        Stop-Job $job
    }
    Write-Host "JOB $($job.Id) $($job.State)"
    Receive-Job $job -Keep
    if ($job.State -ne "Completed") {
        $failed += $job
    }
}

Remove-Job $jobs -Force

if ($failed.Count -gt 0) {
    throw "standard split smoke job failed or timed out"
}
