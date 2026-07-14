param(
    [int]$Frames = 3600,
    [int]$Port = 8181,
    [ValidateSet("both", "host", "client")]
    [string]$RunRole = "both",
    [string]$Peer = "127.0.0.1",
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-entranceff-flag1.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-entranceff-flag1.nds",
    [string]$InputScript = "tests\nsmb_us_direct_mvl_both_different.inputs",
    [string]$HostLogDir = "logs\nsmvl-standard-split-host",
    [string]$ClientLogDir = "logs\nsmvl-standard-split-client",
    [int]$LookupTickDelay = 10,
    [string]$HostGameLocalPlayerID = "0",
    [string]$ClientGameLocalPlayerID = "0",
    [int]$GameLocalPlayerIDStartFrame = 0,
    [switch]$ForceGameLocalPlayerIDEarly,
    [int]$SendDelayFrames = 0,
    [int]$SendJitterFrames = 0,
    [int]$MaxFrameLead = 8,
    [int]$ScreenshotInterval = 900,
    [int]$GameStateTraceInterval = 60,
    [string]$RamDumpFrames = "",
    [int]$RamDumpInterval = 0,
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
    [switch]$PacketBridgeTrace,
    [switch]$PacketBridgePreserveLocalTouch,
    [int]$PlayerStickToStarStartFrame = 0,
    [int]$PlayerStickToStarEndFrame = 0,
    [int]$PlayerStickToStarSlot = 0,
    [switch]$ForcePlayerDeathCounters,
    [int]$ForcePlayerDeathCountersStartFrame = 900,
    [int]$ForcePlayerDeathCountersEndFrame = 1500,
    [int]$ForcePlayerDeathCounter0 = 0,
    [int]$ForcePlayerDeathCounter1 = 0,
    [switch]$ForcePlayerLives,
    [int]$ForcePlayerLife0 = 5,
    [int]$ForcePlayerLife1 = 5,
    [switch]$ForcePlayerPowerups,
    [int]$ForcePlayerPowerupsStartFrame = 0,
    [int]$ForcePlayerPowerupsEndFrame = 0,
    [int]$ForcePlayerPowerup0 = 0,
    [int]$ForcePlayerPowerup1 = 0,
    [switch]$ForcePlayerInventoryPowerups,
    [int]$ForcePlayerInventoryPowerupsStartFrame = 0,
    [int]$ForcePlayerInventoryPowerupsEndFrame = 0,
    [int]$ForcePlayerInventoryPowerup0 = 0,
    [int]$ForcePlayerInventoryPowerup1 = 0,
    [switch]$ForcePlayerStarCounters,
    [int]$ForcePlayerStarCountersStartFrame = 0,
    [int]$ForcePlayerStarCountersEndFrame = 0,
    [int]$ForcePlayerBattleStars0 = 0,
    [int]$ForcePlayerBattleStars1 = 0,
    [int]$ForcePlayerDisplayedStars0 = 0,
    [int]$ForcePlayerDisplayedStars1 = 0,
    [int]$ForcePlayerCollectedStars0 = 0,
    [int]$ForcePlayerCollectedStars1 = 0,
    [switch]$NoCameraFallbackRom,
    [switch]$ForceStageCameraSlot,
    [switch]$ForceStageCameraSlotVerticalOnly,
    [int]$ForceStageCameraSlotStartFrame = 850,
    [int]$ForceStageCameraSlotEndFrame = 1008,
    [int]$ForceStageCameraSlotSource = 0,
    [int]$ForceStageCameraSlotDest = 1,
    [switch]$ForceStageCameraObjectX,
    [int]$ForceStageCameraObjectXStartFrame = 0,
    [int]$ForceStageCameraObjectXEndFrame = 0,
    [string]$ForceStageCameraObjectXValue = "0",
    [string]$ForceStageCameraObjectZValue = "",
    [switch]$ForceStageCameraObjectXWriteDisplay,
    [switch]$ForceStageCameraObjectXWriteSlot,
    [int]$ForceStageCameraObjectXSlot = 1,
    [switch]$ForceStageFXSettings,
    [switch]$ForceStageFXSettingsHostOnly,
    [switch]$ForceStageFXSettingsClientOnly,
    [int]$ForceStageFXSettingsStartFrame = 0,
    [int]$ForceStageFXSettingsEndFrame = 0,
    [string]$ForceStageFXSettingsValue = "0x8000",
    [switch]$RenderCameraAlias,
    [int]$RenderCameraAliasSourcePlayer = 1,
    [int]$RenderCameraAliasDestPlayer = 0,
    [int]$RenderCameraAliasStartFrame = 0,
    [int]$RenderCameraAliasEndFrame = 0,
    [switch]$ForceCameraFocusLoopCount,
    [switch]$ForceCameraFocusLoopCountHostOnly,
    [switch]$ForceCameraFocusLoopCountClientOnly,
    [int]$ForceCameraFocusLoopCountValue = 2,
    [int]$ForceCameraFocusLoopCountStartFrame = 0,
    [int]$ForceCameraFocusLoopCountEndFrame = 0,
    [switch]$TracePlayerLifeCalls,
    [switch]$TracePlayerLifeChanges,
    [switch]$TracePlayerDefeated,
    [switch]$TracePlayerRender,
    [int]$TracePlayerRenderStartFrame = 0,
    [int]$TracePlayerRenderEndFrame = 0,
    [switch]$TraceStageCamera,
    [int]$TraceStageCameraStartFrame = 0,
    [int]$TraceStageCameraEndFrame = 0,
    [int]$TraceStageCameraInterval = 1,
    [switch]$CallTrace,
    [string]$CallTraceAddrs = "",
    [int]$CallTraceStartFrame = 0,
    [int]$CallTraceEndFrame = 0,
    [int]$CallTraceDumpLen = 32,
    [switch]$WriteTrace,
    [string]$WriteTraceAddrs = "",
    [int]$WriteTraceStartFrame = 0,
    [int]$WriteTraceEndFrame = 0
)

$ErrorActionPreference = "Stop"

$defaultClientRom = "roms\nsmb-us-direct-mvl-entry-entranceff-flag1-camera-full-p1.nds"
$defaultDirectRom = "roms\nsmb-us-direct-mvl-entry-entranceff-flag1.nds"
$defaultCameraFallbackRom = "roms\nsmb-us-direct-mvl-entry-entranceff-flag1-camera-fallback.tmp.nds"

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

if (-not $NoCameraFallbackRom) {
    if ($HostRom -eq $defaultDirectRom -or $ClientRom -eq $defaultDirectRom) {
        if (-not (Test-Path $defaultCameraFallbackRom) -or
            ((Get-Item $defaultCameraFallbackRom).LastWriteTime -lt (Get-Item $defaultDirectRom).LastWriteTime)) {
            Write-Host "generating camera fallback ROM: $defaultCameraFallbackRom"
            & python tools\nsmb_us_rom_patch.py --rom $defaultDirectRom --out $defaultCameraFallbackRom camera-fallback-slot-zero
        }
    }
    if ($HostRom -eq $defaultDirectRom) {
        $HostRom = $defaultCameraFallbackRom
    }
    if ($ClientRom -eq $defaultDirectRom) {
        $ClientRom = $defaultCameraFallbackRom
    }
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
    "-HostPacketBridgeForceGameLocalPlayerID", "$HostGameLocalPlayerID",
    "-ClientPacketBridgeForceGameLocalPlayerID", "$ClientGameLocalPlayerID",
    "-PacketBridgeForceGameLocalPlayerIDStartFrame", "$GameLocalPlayerIDStartFrame",
    "-PacketBridgeThrottleStartFrame", "1500",
    "-NetRandomValue", "0x12345678",
    "-NetRandomAuto"
)
if ($ForceGameLocalPlayerIDEarly) {
    $common += "-PacketBridgeForceGameLocalPlayerIDEarly"
}

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
if ($RamDumpFrames -ne "") {
    $common += @("-RamDumpFrames", $RamDumpFrames)
}
if ($RamDumpInterval -gt 0) {
    $common += @("-RamDumpInterval", "$RamDumpInterval")
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
if ($PacketBridgeTrace) {
    $common += "-PacketBridgeTrace"
}
if ($PacketBridgePreserveLocalTouch) {
    $common += "-PacketBridgePreserveLocalTouch"
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
if ($ForcePlayerDeathCounters) {
    $common += @(
        "-ForcePlayerDeathCounters",
        "-ForcePlayerDeathCountersStartFrame", "$ForcePlayerDeathCountersStartFrame",
        "-ForcePlayerDeathCountersEndFrame", "$ForcePlayerDeathCountersEndFrame",
        "-ForcePlayerDeathCounter0", "$ForcePlayerDeathCounter0",
        "-ForcePlayerDeathCounter1", "$ForcePlayerDeathCounter1"
    )
}
if ($ForcePlayerLives) {
    $common += @(
        "-ForcePlayerLives",
        "-ForcePlayerLife0", "$ForcePlayerLife0",
        "-ForcePlayerLife1", "$ForcePlayerLife1"
    )
}
if ($ForcePlayerPowerups) {
    $common += @(
        "-ForcePlayerPowerups",
        "-ForcePlayerPowerupsStartFrame", "$ForcePlayerPowerupsStartFrame",
        "-ForcePlayerPowerupsEndFrame", "$ForcePlayerPowerupsEndFrame",
        "-ForcePlayerPowerup0", "$ForcePlayerPowerup0",
        "-ForcePlayerPowerup1", "$ForcePlayerPowerup1"
    )
}
if ($ForcePlayerInventoryPowerups) {
    $common += @(
        "-ForcePlayerInventoryPowerups",
        "-ForcePlayerInventoryPowerupsStartFrame", "$ForcePlayerInventoryPowerupsStartFrame",
        "-ForcePlayerInventoryPowerupsEndFrame", "$ForcePlayerInventoryPowerupsEndFrame",
        "-ForcePlayerInventoryPowerup0", "$ForcePlayerInventoryPowerup0",
        "-ForcePlayerInventoryPowerup1", "$ForcePlayerInventoryPowerup1"
    )
}
if ($ForcePlayerStarCounters) {
    $common += @(
        "-ForcePlayerStarCounters",
        "-ForcePlayerStarCountersStartFrame", "$ForcePlayerStarCountersStartFrame",
        "-ForcePlayerStarCountersEndFrame", "$ForcePlayerStarCountersEndFrame",
        "-ForcePlayerBattleStars0", "$ForcePlayerBattleStars0",
        "-ForcePlayerBattleStars1", "$ForcePlayerBattleStars1",
        "-ForcePlayerDisplayedStars0", "$ForcePlayerDisplayedStars0",
        "-ForcePlayerDisplayedStars1", "$ForcePlayerDisplayedStars1",
        "-ForcePlayerCollectedStars0", "$ForcePlayerCollectedStars0",
        "-ForcePlayerCollectedStars1", "$ForcePlayerCollectedStars1"
    )
}
if ($ForceStageCameraSlot) {
    $common += @(
        "-ForceStageCameraSlot",
        "-ForceStageCameraSlotStartFrame", "$ForceStageCameraSlotStartFrame",
        "-ForceStageCameraSlotEndFrame", "$ForceStageCameraSlotEndFrame",
        "-ForceStageCameraSlotSource", "$ForceStageCameraSlotSource",
        "-ForceStageCameraSlotDest", "$ForceStageCameraSlotDest"
    )
    if ($ForceStageCameraSlotVerticalOnly) {
        $common += "-ForceStageCameraSlotVerticalOnly"
    }
}
if ($ForceStageCameraObjectX) {
    $common += @(
        "-ForceStageCameraObjectX",
        "-ForceStageCameraObjectXStartFrame", "$ForceStageCameraObjectXStartFrame",
        "-ForceStageCameraObjectXEndFrame", "$ForceStageCameraObjectXEndFrame",
        "-ForceStageCameraObjectXValue", "$ForceStageCameraObjectXValue",
        "-ForceStageCameraObjectZValue", "$ForceStageCameraObjectZValue",
        "-ForceStageCameraObjectXSlot", "$ForceStageCameraObjectXSlot"
    )
    if ($ForceStageCameraObjectXWriteDisplay) {
        $common += "-ForceStageCameraObjectXWriteDisplay"
    }
    if ($ForceStageCameraObjectXWriteSlot) {
        $common += "-ForceStageCameraObjectXWriteSlot"
    }
}
if ($ForceStageFXSettings) {
    $common += @(
        "-ForceStageFXSettings",
        "-ForceStageFXSettingsStartFrame", "$ForceStageFXSettingsStartFrame",
        "-ForceStageFXSettingsEndFrame", "$ForceStageFXSettingsEndFrame",
        "-ForceStageFXSettingsValue", "$ForceStageFXSettingsValue"
    )
    if ($ForceStageFXSettingsHostOnly) {
        $common += "-ForceStageFXSettingsHostOnly"
    }
    if ($ForceStageFXSettingsClientOnly) {
        $common += "-ForceStageFXSettingsClientOnly"
    }
}
if ($RenderCameraAlias) {
    $common += @(
        "-RenderCameraAlias",
        "-RenderCameraAliasSourcePlayer", "$RenderCameraAliasSourcePlayer",
        "-RenderCameraAliasDestPlayer", "$RenderCameraAliasDestPlayer",
        "-RenderCameraAliasStartFrame", "$RenderCameraAliasStartFrame",
        "-RenderCameraAliasEndFrame", "$RenderCameraAliasEndFrame"
    )
}
if ($ForceCameraFocusLoopCount) {
    $common += @(
        "-ForceCameraFocusLoopCount",
        "-ForceCameraFocusLoopCountValue", "$ForceCameraFocusLoopCountValue",
        "-ForceCameraFocusLoopCountStartFrame", "$ForceCameraFocusLoopCountStartFrame",
        "-ForceCameraFocusLoopCountEndFrame", "$ForceCameraFocusLoopCountEndFrame"
    )
    if ($ForceCameraFocusLoopCountHostOnly) {
        $common += "-ForceCameraFocusLoopCountHostOnly"
    }
    if ($ForceCameraFocusLoopCountClientOnly) {
        $common += "-ForceCameraFocusLoopCountClientOnly"
    }
}
if ($TracePlayerLifeCalls) {
    $common += "-TracePlayerLifeCalls"
}
if ($TracePlayerLifeChanges) {
    $common += "-TracePlayerLifeChanges"
}
if ($TracePlayerDefeated) {
    $common += "-TracePlayerDefeated"
}
if ($TracePlayerRender) {
    $common += @(
        "-TracePlayerRender",
        "-TracePlayerRenderStartFrame", "$TracePlayerRenderStartFrame",
        "-TracePlayerRenderEndFrame", "$TracePlayerRenderEndFrame"
    )
}
if ($TraceStageCamera) {
    $common += @(
        "-TraceStageCamera",
        "-TraceStageCameraStartFrame", "$TraceStageCameraStartFrame",
        "-TraceStageCameraEndFrame", "$TraceStageCameraEndFrame",
        "-TraceStageCameraInterval", "$TraceStageCameraInterval"
    )
}
if ($CallTrace) {
    $common += @(
        "-CallTrace",
        "-CallTraceStartFrame", "$CallTraceStartFrame",
        "-CallTraceEndFrame", "$CallTraceEndFrame",
        "-CallTraceDumpLen", "$CallTraceDumpLen"
    )
    if ($CallTraceAddrs -ne "") {
        $common += @("-CallTraceAddrs", "$CallTraceAddrs")
    }
}
if ($WriteTrace) {
    $common += @(
        "-WriteTrace",
        "-WriteTraceStartFrame", "$WriteTraceStartFrame",
        "-WriteTraceEndFrame", "$WriteTraceEndFrame"
    )
    if ($WriteTraceAddrs -ne "") {
        $common += @("-WriteTraceAddrs", "$WriteTraceAddrs")
    }
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
