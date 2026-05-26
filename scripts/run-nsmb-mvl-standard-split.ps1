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
    [int]$MaxFrameLead = 8,
    [int]$ScreenshotInterval = 900,
    [int]$GameStateTraceInterval = 60,
    [int]$WaitTimeoutMs = 720000,
    [int]$JobTimeoutSeconds = 780,
    [switch]$NoDirectCapture,
    [switch]$NoGameStateTrace,
    [switch]$NoScreenshots,
    [switch]$NoHashLog,
    [switch]$NoFrameLimit,
    [switch]$FixedFrameTime,
    [double]$TargetFps = 0.0,
    [switch]$AllowJitWithPacketBridge,
    [int]$PlayerStickToStarStartFrame = 0,
    [int]$PlayerStickToStarEndFrame = 0,
    [int]$PlayerStickToStarSlot = 0
)

$ErrorActionPreference = "Stop"

$defaultClientRom = "roms\nsmb-us-direct-mvl-entry-entranceff-flag1-camera-full-p1.nds"

function Format-Arg {
    param([string]$Value)
    if ($Value.StartsWith("-")) {
        return $Value
    }
    return "'" + ($Value -replace "'", "''") + "'"
}

if (-not (Test-Path $ClientRom) -and $ClientRom -eq $defaultClientRom) {
    $tempClientRom = "roms\nsmb-us-direct-mvl-entry-entranceff-flag1-camera-state-p1.tmp.nds"
    Write-Host "default client camera ROM is missing; generating $ClientRom"
    & python tools\nsmb_us_rom_patch.py --rom $HostRom --out $tempClientRom stage-camera-state-player-id --player-id 1
    & python tools\nsmb_us_rom_patch.py --rom $tempClientRom --out $ClientRom stage-camera-player-id --player-id 1
    Remove-Item -Force $tempClientRom -ErrorAction SilentlyContinue
}

$common = @(
    "-Exe", $Exe,
    "-Rom", $HostRom,
    "-InputScript", $InputScript,
    "-Frames", "$Frames",
    "-WaitTimeoutMs", "$WaitTimeoutMs",
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
    "-PacketBridgeThrottleStartFrame", "1500",
    "-NetRandomValue", "0x12345678",
    "-NetRandomAuto"
)

if ($MaxFrameLead -ge 0) {
    $common += @("-PacketBridgeMaxFrameLead", "$MaxFrameLead")
}

if (-not $NoGameStateTrace) {
    $common += @(
        "-GameStateTrace",
        "-GameStateTraceExtended",
        "-GameStateTraceInterval", "$GameStateTraceInterval"
    )
}

if ($NoScreenshots) {
    $common += @("-ScreenshotInterval", "0")
} else {
    $common += @("-ScreenshotInterval", "$ScreenshotInterval")
}

if (-not $NoDirectCapture) {
    $common += "-PacketBridgeDirectCapture"
}
if ($NoHashLog) {
    $common += "-NoHashLog"
}
if ($NoFrameLimit) {
    $common += "-NoFrameLimit"
}
if ($FixedFrameTime) {
    $common += "-FixedFrameTime"
}
if ($TargetFps -gt 0.0) {
    $common += @("-TargetFps", $TargetFps.ToString([System.Globalization.CultureInfo]::InvariantCulture))
}
if ($AllowJitWithPacketBridge) {
    $common += "-PacketBridgeAllowJit"
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
$startedAt = Get-Date
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

$finishedAt = Get-Date
$elapsed = ($finishedAt - $startedAt).TotalSeconds
if ($elapsed -gt 0) {
    $effectiveFps = [double]$Frames / $elapsed
    Write-Host ("NSMB MvL standard split timing: frames={0} elapsedSec={1:n2} effectiveFps={2:n2} runRole={3}" -f $Frames, $elapsed, $effectiveFps, $RunRole)
}
