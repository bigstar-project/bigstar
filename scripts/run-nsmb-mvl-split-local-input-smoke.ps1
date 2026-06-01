param(
    [int]$Frames = 2600,
    [int]$WaitTimeoutMs = 300000,
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-netaid.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-netaid.tmp.nds",
    [string]$HostInputScript = "tests\nsmb_us_direct_mvl_manual_host_mario_move.inputs",
    [string]$ClientInputScript = "tests\nsmb_us_direct_mvl_manual_client_luigi_move.inputs",
    [string]$MvlMatchSeed = "",
    [int]$InputDelayFrames = 16,
    [int]$InputMaxFrameLead = 2,
    [switch]$InputNetplayTrace,
    [int]$InputSendDelayFrames = 0,
    [int]$InputSendJitterFrames = 0,
    [switch]$InputUnreliable,
    [int]$InputBundleHistory = 0,
    [switch]$NetworkPumpThread,
    [int]$NetworkPumpSleepUs = 250,
    [switch]$LowDelayWan,
    [int]$InputDropModulo = 0,
    [int]$InputDropOffset = 0,
    [switch]$Rollback,
    [string]$RollbackBackend = "",
    [int]$RollbackWindow = 20,
    [int]$RollbackCheckpointInterval = 1,
    [int]$RollbackResimulateDelayFrames = 0,
    [switch]$RollbackResimulate,
    [switch]$RollbackRestoreProbe,
    [int]$RollbackPredictionProbeModulo = 0,
    [int]$RollbackPredictionProbeOffset = 0,
    [int]$RollbackPredictionProbeLimit = -1,
    [int]$RollbackPredictionProbeStartFrame = 0,
    [int]$RollbackPredictionProbeEndFrame = 0,
    [string]$RollbackPredictionProbeKeyMask = "",
    [int]$RollbackInputWaitUs = 0,
    [int]$RollbackSettleFrames = 0,
    [switch]$IgnoreSpeculativeInputFields,
    [int]$GameStateTraceInterval = 30,
    [switch]$NoGameStateTrace,
    [switch]$StateSync,
    [switch]$StateApply,
    [int]$StateSyncInterval = 60,
    [switch]$StateSyncExtended,
    [string]$StateApplyMode = "",
    [switch]$PlayerStateSync,
    [switch]$PlayerStateApply,
    [switch]$PlayerStateGlobals,
    [int]$PlayerStateSyncInterval = 1,
    [int]$PlayerStateMaxPredictFrames = 2,
    [switch]$SkipGameStateComparison,
    [switch]$SkipMovementProbe,
    [switch]$RequireActorSnapshotMovement,
    [int]$ActorSnapshotStartFrame = 990,
    [int]$ActorSnapshotMinMovedRows = 1,
    [int]$ActorSnapshotMaxDriftX = -1,
    [int]$ActorSnapshotMaxDriftY = -1,
    [switch]$TracePlayerLifeChanges,
    [switch]$TracePlayerDefeated,
    [switch]$RequireStarPickup,
    [int]$RequireStarPickupPlayer = -1,
    [switch]$RequirePlayerDeath,
    [int]$RequirePlayerDeathPlayer = -1,
    [int]$RequirePlayerDeathStartFrame = 0,
    [int]$RequirePlayerDeathEndFrame = 0,
    [switch]$RequireResultScene,
    [switch]$RequireNoResultScene,
    [switch]$CheckMovingHazardProgressDuringDeath,
    [int]$CheckMovingHazardProgressStartFrame = 0,
    [int]$CheckMovingHazardProgressEndFrame = 0,
    [int]$CheckMovingHazardProgressMinUniqueX = 3,
    [switch]$CheckVsPipeRespawnVisibility,
    [int]$CheckVsPipeRespawnVisibilityStartFrame = 0,
    [int]$CheckVsPipeRespawnVisibilityEndFrame = 0,
    [switch]$NoFrameLimit,
    [switch]$FixedFrameTime,
    [double]$TargetFps = 0.0,
    [switch]$NoDrawScreen,
    [switch]$NoAudioSync,
    [double]$MaxActiveFrameMs = 0.0,
    [int]$MaxActiveFrameOver25ms = -1,
    [int]$MaxActiveFrameOver33ms = -1,
    [double]$MaxRollbackFrameMs = 0.0,
    [int]$MinRollbackResims = -1,
    [double]$SlowFrameThresholdMs = 33.0,
    [int]$MaxConsecutiveSlowFrames = -1,
    [int]$StallTimeoutMs = 0,
    [int]$StallStartFrame = 900,
    [switch]$UseLanMP,
    [switch]$ForceStageActorFreezeFlag,
    [switch]$ForceStageActorFreezeFlagHostOnly,
    [switch]$ForceStageActorFreezeFlagClientOnly,
    [int]$ForceStageActorFreezeFlagStartFrame = 0,
    [int]$ForceStageActorFreezeFlagEndFrame = 0,
    [string]$ForceStageActorFreezeFlagValue = "0",
    [int]$HostStartupDelayMs = 1200,
    [string]$LogDir = "logs\nsmb-mvl-split-local-input-smoke",
    [switch]$AllowJit
)

$ErrorActionPreference = "Stop"

if ($MaxConsecutiveSlowFrames -ge 0 -or $MaxRollbackFrameMs -gt 0.0) {
    $env:MELONDS_NSML_FPS_SPIKE_TRACE = "1"
    $currentSpikeThreshold = 0.0
    $targetSpikeThreshold = if ($MaxRollbackFrameMs -gt 0.0) {
        [Math]::Min($SlowFrameThresholdMs, $MaxRollbackFrameMs)
    } else {
        $SlowFrameThresholdMs
    }
    $hasSpikeThreshold = [double]::TryParse(
        $env:MELONDS_NSML_FPS_SPIKE_THRESHOLD_MS,
        [System.Globalization.NumberStyles]::Float,
        [System.Globalization.CultureInfo]::InvariantCulture,
        [ref]$currentSpikeThreshold)
    if (-not $hasSpikeThreshold -or $currentSpikeThreshold -le 0.0 -or $currentSpikeThreshold -gt $targetSpikeThreshold) {
        $env:MELONDS_NSML_FPS_SPIKE_THRESHOLD_MS = $targetSpikeThreshold.ToString([System.Globalization.CultureInfo]::InvariantCulture)
    }
}

if ($RollbackPredictionProbeModulo -gt 0) {
    $env:MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_MODULO = "$RollbackPredictionProbeModulo"
    $env:MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_OFFSET = "$RollbackPredictionProbeOffset"
    $env:MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_LIMIT = "$RollbackPredictionProbeLimit"
    $env:MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_START_FRAME = "$RollbackPredictionProbeStartFrame"
    $env:MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_END_FRAME = "$RollbackPredictionProbeEndFrame"
    if ($RollbackPredictionProbeKeyMask -ne "") {
        $env:MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_KEY_MASK = "$RollbackPredictionProbeKeyMask"
    }
} else {
    Remove-Item Env:\MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_MODULO -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_OFFSET -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_LIMIT -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_START_FRAME -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_END_FRAME -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_KEY_MASK -ErrorAction SilentlyContinue
}

if ($RollbackInputWaitUs -gt 0) {
    $env:MELONDS_NSML_ROLLBACK_INPUT_WAIT_US = "$RollbackInputWaitUs"
} else {
    Remove-Item Env:\MELONDS_NSML_ROLLBACK_INPUT_WAIT_US -ErrorAction SilentlyContinue
}
if ($NetworkPumpThread) {
    $env:MELONDS_NSML_NET_PUMP_THREAD = "1"
    $env:MELONDS_NSML_NET_PUMP_SLEEP_US = "$NetworkPumpSleepUs"
} else {
    Remove-Item Env:\MELONDS_NSML_NET_PUMP_THREAD -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_NET_PUMP_SLEEP_US -ErrorAction SilentlyContinue
}

if ($LowDelayWan) {
    $InputDelayFrames = 4
    $InputMaxFrameLead = 4
    $InputSendDelayFrames = 0
    $InputSendJitterFrames = 0
    $InputUnreliable = $true
    $InputBundleHistory = 8
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$smokeScript = Join-Path $PSScriptRoot "run-nsmb-mvl-lan-route-smoke.ps1"
$logRoot = Join-Path $repoRoot $LogDir
$hostLog = Join-Path $logRoot "host"
$clientLog = Join-Path $logRoot "client"
$wrapperLog = Join-Path $logRoot "wrapper"
New-Item -ItemType Directory -Force $wrapperLog | Out-Null
Remove-Item -Recurse -Force $hostLog, $clientLog -ErrorAction SilentlyContinue

$common = @(
    "-WaitTimeoutMs", "$WaitTimeoutMs",
    "-StallTimeoutMs", "$StallTimeoutMs",
    "-StallStartFrame", "$StallStartFrame",
    "-Frames", "$Frames",
    "-Exe", $Exe,
    "-ScreenshotInterval", "0",
    "-NoHashLog",
    "-SkipDisconnectScreenshotCheck",
    "-SkipBlankScreenshotCheck",
    "-SkipMvlStateCheck",
    "-SkipGameplayActorCheck",
    "-InputNetplay",
    "-InputDelayFrames", "$InputDelayFrames",
    "-InputMaxFrameLead", "$InputMaxFrameLead",
    "-InputSendDelayFrames", "$InputSendDelayFrames",
    "-InputSendJitterFrames", "$InputSendJitterFrames",
    "-PacketBridgeJitHelperPatch",
    "-PacketBridgeJitHelperPatchFrame", "870",
    "-PacketBridgeStartFrame", "870",
    "-RequireNetLocalAidStartFrame", "870"
)
if (-not $UseLanMP) {
    $common += "-NoLanMP"
}
if (-not $NoGameStateTrace) {
    $common += @(
        "-GameStateTrace",
        "-GameStateTraceExtended",
        "-GameStateTraceInterval", "$GameStateTraceInterval"
    )
}
if ($StateSync) {
    $common += @(
        "-StateSync",
        "-StateSyncInterval", "$StateSyncInterval"
    )
    if ($StateApply) {
        $common += "-StateApply"
    }
    if ($StateSyncExtended) {
        $common += "-StateSyncExtended"
    }
    if ($StateApplyMode -ne "") {
        $common += @("-StateApplyMode", "$StateApplyMode")
    }
}
if ($PlayerStateSync) {
    $common += @(
        "-PlayerStateSync",
        "-PlayerStateSyncInterval", "$PlayerStateSyncInterval",
        "-PlayerStateMaxPredictFrames", "$PlayerStateMaxPredictFrames"
    )
    if ($PlayerStateApply) {
        $common += "-PlayerStateApply"
    }
    if ($PlayerStateGlobals) {
        $common += "-PlayerStateGlobals"
    }
}
if ($AllowJit) {
    $common += "-AllowJit"
}
if ($NoFrameLimit) {
    $common += "-NoFrameLimit"
}
if ($FixedFrameTime) {
    $common += "-FixedFrameTime"
}
if ($TargetFps -gt 0.0) {
    $common += @("-TargetFps", "$TargetFps")
}
if ($NoDrawScreen) {
    $common += "-NoDrawScreen"
}
if ($NoAudioSync) {
    $common += "-NoAudioSync"
}
if ($ForceStageActorFreezeFlag) {
    $common += @(
        "-ForceStageActorFreezeFlag",
        "-ForceStageActorFreezeFlagStartFrame", "$ForceStageActorFreezeFlagStartFrame",
        "-ForceStageActorFreezeFlagEndFrame", "$ForceStageActorFreezeFlagEndFrame",
        "-ForceStageActorFreezeFlagValue", "$ForceStageActorFreezeFlagValue"
    )
    if ($ForceStageActorFreezeFlagHostOnly) {
        $common += "-ForceStageActorFreezeFlagHostOnly"
    }
    if ($ForceStageActorFreezeFlagClientOnly) {
        $common += "-ForceStageActorFreezeFlagClientOnly"
    }
}
if ($InputNetplayTrace) {
    $common += "-InputNetplayTrace"
}
if ($TracePlayerLifeChanges) {
    $common += "-TracePlayerLifeChanges"
}
if ($TracePlayerDefeated) {
    $common += "-TracePlayerDefeated"
}
if ($RequireStarPickup) {
    $common += @("-RequireStarPickup", "-RequireStarPickupPlayer", "$RequireStarPickupPlayer")
}
if ($RequirePlayerDeath) {
    $common += @(
        "-RequirePlayerDeath",
        "-RequirePlayerDeathPlayer", "$RequirePlayerDeathPlayer",
        "-RequirePlayerDeathStartFrame", "$RequirePlayerDeathStartFrame",
        "-RequirePlayerDeathEndFrame", "$RequirePlayerDeathEndFrame"
    )
}
if ($RequireResultScene) {
    $common += "-RequireResultScene"
}
if ($RequireNoResultScene) {
    $common += "-RequireNoResultScene"
}
if ($CheckMovingHazardProgressDuringDeath) {
    $common += @(
        "-CheckMovingHazardProgressDuringDeath",
        "-CheckMovingHazardProgressStartFrame", "$CheckMovingHazardProgressStartFrame",
        "-CheckMovingHazardProgressEndFrame", "$CheckMovingHazardProgressEndFrame",
        "-CheckMovingHazardProgressMinUniqueX", "$CheckMovingHazardProgressMinUniqueX"
    )
}
if ($CheckVsPipeRespawnVisibility) {
    $common += @(
        "-CheckVsPipeRespawnVisibility",
        "-CheckVsPipeRespawnVisibilityStartFrame", "$CheckVsPipeRespawnVisibilityStartFrame",
        "-CheckVsPipeRespawnVisibilityEndFrame", "$CheckVsPipeRespawnVisibilityEndFrame"
    )
}
if ($MvlMatchSeed -ne "") {
    $common += @("-MvlMatchSeed", $MvlMatchSeed)
}
if ($InputUnreliable) {
    $common += @("-InputUnreliable", "-InputBundleHistory", "$InputBundleHistory")
}
if ($InputDropModulo -gt 0) {
    $common += @("-InputDropModulo", "$InputDropModulo", "-InputDropOffset", "$InputDropOffset")
}
if ($Rollback) {
    $common += @(
        "-Rollback",
        "-RollbackWindow", "$RollbackWindow",
        "-RollbackCheckpointInterval", "$RollbackCheckpointInterval",
        "-RollbackResimulateDelayFrames", "$RollbackResimulateDelayFrames"
    )
    if ($RollbackBackend -ne "") {
        $common += @("-RollbackBackend", "$RollbackBackend")
    }
    if ($RollbackResimulate) {
        $common += "-RollbackResimulate"
    }
    if ($RollbackRestoreProbe) {
        $common += "-RollbackRestoreProbe"
    }
}

$hostArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $smokeScript
) + $common + @(
    "-RunRole", "host",
    "-HostRom", $HostRom,
    "-InputScript", $HostInputScript,
    "-LogDir", $hostLog
)
if (-not $NoGameStateTrace) {
    $hostArgs += @("-RequireHostLocalPlayerID", "0", "-RequireHostNetLocalAid", "0")
}

$clientArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $smokeScript
) + $common + @(
    "-RunRole", "client",
    "-Peer", "127.0.0.1",
    "-ClientRom", $ClientRom,
    "-InputScript", $ClientInputScript,
    "-LogDir", $clientLog
)
if (-not $NoGameStateTrace) {
    $clientArgs += @("-RequireClientLocalPlayerID", "1", "-RequireClientNetLocalAid", "1")
}

$hostOut = Join-Path $wrapperLog "host-wrapper.out.txt"
$hostErr = Join-Path $wrapperLog "host-wrapper.err.txt"
$clientOut = Join-Path $wrapperLog "client-wrapper.out.txt"
$clientErr = Join-Path $wrapperLog "client-wrapper.err.txt"

$hostProc = Start-Process -FilePath "powershell.exe" `
    -ArgumentList $hostArgs `
    -WorkingDirectory $repoRoot `
    -RedirectStandardOutput $hostOut `
    -RedirectStandardError $hostErr `
    -PassThru `
    -WindowStyle Hidden

Start-Sleep -Milliseconds $HostStartupDelayMs

$clientProc = Start-Process -FilePath "powershell.exe" `
    -ArgumentList $clientArgs `
    -WorkingDirectory $repoRoot `
    -RedirectStandardOutput $clientOut `
    -RedirectStandardError $clientErr `
    -PassThru `
    -WindowStyle Hidden

$clientProc.WaitForExit()
$hostProc.WaitForExit()

$hostText = if (Test-Path $hostOut) { Get-Content $hostOut -Raw } else { "" }
$clientText = if (Test-Path $clientOut) { Get-Content $clientOut -Raw } else { "" }
$hostText = [string]$hostText
$clientText = [string]$clientText
$hostMelonOut = Join-Path $hostLog "host.stdout.txt"
$clientMelonOut = Join-Path $clientLog "client.stdout.txt"
$hostMelonText = if (Test-Path $hostMelonOut) { [string](Get-Content $hostMelonOut -Raw) } else { "" }
$clientMelonText = if (Test-Path $clientMelonOut) { [string](Get-Content $clientMelonOut -Raw) } else { "" }
$hostExitFailed = $null -ne $hostProc.ExitCode -and $hostProc.ExitCode -ne 0
$clientExitFailed = $null -ne $clientProc.ExitCode -and $clientProc.ExitCode -ne 0
if ($hostExitFailed -or
    $clientExitFailed -or
    $hostText -notmatch "NSMB Mario vs Luigi LAN route smoke passed" -or
    $clientText -notmatch "NSMB Mario vs Luigi LAN route smoke passed") {
    $details = @()
    foreach ($path in @($hostOut, $hostErr, $clientOut, $clientErr)) {
        if (Test-Path $path) { $details += Get-Content $path -Raw }
    }
    throw "split local-input child smoke failed: hostExit=$($hostProc.ExitCode) clientExit=$($clientProc.ExitCode) $($details -join "`n")"
}

function Assert-ActiveFrameTiming {
    param(
        [string]$Role,
        [string]$Text,
        [double]$RollbackFrameLimitMs
    )

    $line = ($Text -split "`r?`n") |
        Where-Object { $_ -match "NSMB Test: active frame timing" } |
        Select-Object -Last 1
    if ($null -eq $line) {
        throw "$Role missing active frame timing line"
    }

    if ($line -notmatch "maxFrameMs=([0-9.]+).*over25ms=([0-9]+).*over33ms=([0-9]+)") {
        throw "$Role malformed active frame timing line: $line"
    }

    $maxFrameMs = [double]$Matches[1]
    $over25ms = [int]$Matches[2]
    $over33ms = [int]$Matches[3]
    if ($MaxActiveFrameMs -gt 0.0 -and $maxFrameMs -gt $MaxActiveFrameMs) {
        throw "$Role active frame spike too high: maxFrameMs=$maxFrameMs limit=$MaxActiveFrameMs"
    }
    if ($MaxActiveFrameOver25ms -ge 0 -and $over25ms -gt $MaxActiveFrameOver25ms) {
        throw "$Role active frame over25ms too high: over25ms=$over25ms limit=$MaxActiveFrameOver25ms"
    }
    if ($MaxActiveFrameOver33ms -ge 0 -and $over33ms -gt $MaxActiveFrameOver33ms) {
        throw "$Role active frame over33ms too high: over33ms=$over33ms limit=$MaxActiveFrameOver33ms"
    }

    if ($MaxConsecutiveSlowFrames -ge 0) {
        $maxRun = 0
        $run = 0
        $lastFrame = -1
        foreach ($perfLine in ($Text -split "`r?`n")) {
            if ($perfLine -notmatch "NSMB PerfSpike: .*frame=([0-9]+) frameTimeUs=([0-9]+)") {
                continue
            }

            $frame = [int]$Matches[1]
            $frameMs = [double]$Matches[2] / 1000.0
            if ($frameMs -lt $SlowFrameThresholdMs) {
                continue
            }

            if ($lastFrame -ge 0 -and $frame -eq ($lastFrame + 1)) {
                $run++
            } else {
                $run = 1
            }
            $lastFrame = $frame
            if ($run -gt $maxRun) {
                $maxRun = $run
            }
        }

        if ($maxRun -gt $MaxConsecutiveSlowFrames) {
            throw "$Role consecutive slow frames too high: thresholdMs=$SlowFrameThresholdMs maxRun=$maxRun limit=$MaxConsecutiveSlowFrames"
        }
    }

    if ($RollbackFrameLimitMs -gt 0.0) {
        $maxRollbackFrameMs = 0.0
        $rollbackSpikeCount = 0
        $lastRestores = 0
        $lastResims = 0
        foreach ($perfLine in ($Text -split "`r?`n")) {
            if ($perfLine -notmatch "NSMB PerfSpike: .*frame=([0-9]+) frameTimeUs=([0-9]+)") {
                continue
            }

            $frameMs = [double]$Matches[2] / 1000.0
            $restoreDelta = 0
            $resimDelta = 0
            if ($perfLine -match "rollbackRestoreDelta=([0-9]+).*rollbackResimDelta=([0-9]+)") {
                $restoreDelta = [int]$Matches[1]
                $resimDelta = [int]$Matches[2]
            } elseif ($perfLine -match "rollbackRestores=([0-9]+).*rollbackResims=([0-9]+)") {
                $restores = [int]$Matches[1]
                $resims = [int]$Matches[2]
                $restoreDelta = $restores - $lastRestores
                $resimDelta = $resims - $lastResims
                $lastRestores = $restores
                $lastResims = $resims
            }

            if ($restoreDelta -le 0 -and $resimDelta -le 0) {
                continue
            }

            $rollbackSpikeCount++
            if ($frameMs -gt $maxRollbackFrameMs) {
                $maxRollbackFrameMs = $frameMs
            }
        }

        if ($maxRollbackFrameMs -gt $RollbackFrameLimitMs) {
            throw "$Role rollback frame spike too high: maxRollbackFrameMs=$maxRollbackFrameMs limit=$RollbackFrameLimitMs rollbackSpikeCount=$rollbackSpikeCount"
        }
    }
}

function Assert-RollbackResimCount {
    param(
        [string]$Role,
        [string]$Text
    )

    if ($MinRollbackResims -lt 0) {
        return
    }

    $maxResims = 0
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match "NSMB Rollback: frame=.*resims=([0-9]+)") {
            $value = [int]$Matches[1]
            if ($value -gt $maxResims) {
                $maxResims = $value
            }
        } elseif ($line -match "rollbackResims=([0-9]+)") {
            $value = [int]$Matches[1]
            if ($value -gt $maxResims) {
                $maxResims = $value
            }
        }
    }

    if ($maxResims -lt $MinRollbackResims) {
        throw "$Role rollback resim count too low: resims=$maxResims min=$MinRollbackResims"
    }
}

if ($MaxActiveFrameMs -gt 0.0 -or $MaxActiveFrameOver25ms -ge 0 -or $MaxActiveFrameOver33ms -ge 0 -or $MaxConsecutiveSlowFrames -ge 0 -or $MaxRollbackFrameMs -gt 0.0) {
    Assert-ActiveFrameTiming -Role "host" -Text $hostMelonText -RollbackFrameLimitMs $MaxRollbackFrameMs
    Assert-ActiveFrameTiming -Role "client" -Text $clientMelonText -RollbackFrameLimitMs $MaxRollbackFrameMs
}
if ($MinRollbackResims -ge 0) {
    Assert-RollbackResimCount -Role "host" -Text $hostMelonText
    Assert-RollbackResimCount -Role "client" -Text $clientMelonText
}

function Convert-TraceHexToInt64 {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return 0
    }
    if ($Value.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
        return [Convert]::ToInt64($Value.Substring(2), 16)
    }
    return [Convert]::ToInt64($Value, 10)
}

function Convert-TraceHexToSigned32 {
    param([string]$Value)

    $raw = Convert-TraceHexToInt64 $Value
    if ($raw -ge [int64]2147483648) {
        return $raw - [int64]4294967296
    }
    return $raw
}

$hostCsv = Join-Path $hostLog "host.game-state.csv"
$clientCsv = Join-Path $clientLog "client.game-state.csv"
$hostRows = $null
$clientRows = $null
if (-not $NoGameStateTrace -and ($RequireActorSnapshotMovement -or -not $SkipGameStateComparison)) {
    if (-not (Test-Path $hostCsv)) {
        throw "missing host game-state trace: $hostCsv"
    }
    if (-not (Test-Path $clientCsv)) {
        throw "missing client game-state trace: $clientCsv"
    }
    $hostRows = @(Import-Csv $hostCsv)
    $clientRows = @(Import-Csv $clientCsv)
}

function Assert-ActorSnapshotMovement {
    param(
        [object[]]$HostRows,
        [object[]]$ClientRows
    )

    function Test-RemoteMovement {
        param(
            [string]$Label,
            [object[]]$Rows,
            [string]$FoundField,
            [string]$XField,
            [string]$InputField
        )

        $candidateRows = @($Rows | Where-Object {
            [int]$_.frame -ge $ActorSnapshotStartFrame -and $_.$FoundField -eq "0x1"
        })
        if ($candidateRows.Count -lt 2) {
            throw "$Label actor snapshot movement check failed: rows=$($candidateRows.Count) startFrame=$ActorSnapshotStartFrame"
        }

        $first = $candidateRows | Select-Object -First 1
        $last = $candidateRows | Select-Object -Last 1
        $firstX = Convert-TraceHexToInt64 $first.$XField
        $inputRows = @($candidateRows | Where-Object { (Convert-TraceHexToInt64 $_.$InputField) -ne 0 })
        $movedRows = @($candidateRows | Where-Object { (Convert-TraceHexToInt64 $_.$XField) -ne $firstX })
        if ($inputRows.Count -eq 0 -or $movedRows.Count -lt $ActorSnapshotMinMovedRows) {
            throw "$Label actor snapshot movement check failed: rows=$($candidateRows.Count) inputRows=$($inputRows.Count) movedRows=$($movedRows.Count) minMoved=$ActorSnapshotMinMovedRows firstX=$($first.$XField) lastX=$($last.$XField) lastInput=$($last.$InputField)"
        }
        Write-Host "$Label actor snapshot movement check passed: rows=$($candidateRows.Count) inputRows=$($inputRows.Count) movedRows=$($movedRows.Count) firstX=$($first.$XField) lastX=$($last.$XField)"
    }

    Test-RemoteMovement `
        -Label "client remote player0" `
        -Rows $ClientRows `
        -FoundField "playerActor0Found" `
        -XField "playerActor0X" `
        -InputField "inputPlayer0Held"
    Test-RemoteMovement `
        -Label "host remote player1" `
        -Rows $HostRows `
        -FoundField "playerActor1Found" `
        -XField "playerActor1X" `
        -InputField "inputPlayer1Held"

    if ($ActorSnapshotMaxDriftX -lt 0 -and $ActorSnapshotMaxDriftY -lt 0) {
        return
    }

    $hostByFrame = @{}
    foreach ($row in $HostRows) {
        $hostByFrame[[int]$row.frame] = $row
    }
    $clientByFrame = @{}
    foreach ($row in $ClientRows) {
        $clientByFrame[[int]$row.frame] = $row
    }

    $maxDriftX = 0
    $maxDriftY = 0
    $checked = 0
    foreach ($frame in $hostByFrame.Keys) {
        if ($frame -lt $ActorSnapshotStartFrame -or -not $clientByFrame.ContainsKey($frame)) {
            continue
        }

        $hostRow = $hostByFrame[$frame]
        $clientRow = $clientByFrame[$frame]
        foreach ($pair in @(
            @{ Label = "player0"; Local = $hostRow; Remote = $clientRow; X = "playerActor0X"; Y = "playerActor0Y"; Found = "playerActor0Found" },
            @{ Label = "player1"; Local = $clientRow; Remote = $hostRow; X = "playerActor1X"; Y = "playerActor1Y"; Found = "playerActor1Found" }
        )) {
            if ($pair.Local.($pair.Found) -ne "0x1" -or $pair.Remote.($pair.Found) -ne "0x1") {
                continue
            }

            $dx = [Math]::Abs((Convert-TraceHexToSigned32 $pair.Local.($pair.X)) - (Convert-TraceHexToSigned32 $pair.Remote.($pair.X)))
            $dy = [Math]::Abs((Convert-TraceHexToSigned32 $pair.Local.($pair.Y)) - (Convert-TraceHexToSigned32 $pair.Remote.($pair.Y)))
            if ($dx -gt $maxDriftX) { $maxDriftX = $dx }
            if ($dy -gt $maxDriftY) { $maxDriftY = $dy }
            $checked++
            if ($ActorSnapshotMaxDriftX -ge 0 -and $dx -gt $ActorSnapshotMaxDriftX) {
                throw "$($pair.Label) actor snapshot X drift too high: frame=$frame dx=$dx limit=$ActorSnapshotMaxDriftX local=$($pair.Local.($pair.X)) remote=$($pair.Remote.($pair.X))"
            }
            if ($ActorSnapshotMaxDriftY -ge 0 -and $dy -gt $ActorSnapshotMaxDriftY) {
                throw "$($pair.Label) actor snapshot Y drift too high: frame=$frame dy=$dy limit=$ActorSnapshotMaxDriftY local=$($pair.Local.($pair.Y)) remote=$($pair.Remote.($pair.Y))"
            }
        }
    }

    if ($checked -eq 0) {
        throw "actor snapshot drift check failed: no comparable actor rows after frame $ActorSnapshotStartFrame"
    }
    Write-Host "actor snapshot movement/drift check passed: checked=$checked maxDriftX=$maxDriftX maxDriftY=$maxDriftY"
}

if ($RequireActorSnapshotMovement) {
    Assert-ActorSnapshotMovement -HostRows $hostRows -ClientRows $clientRows
}

if ($NoGameStateTrace -or $SkipGameStateComparison) {
    Get-Content $hostOut
    Get-Content $clientOut
    Write-Host "NSMB Mario vs Luigi split local-input smoke passed without game-state comparison: frames=$Frames log=$logRoot"
    return
}

$clientByFrame = @{}
foreach ($row in $clientRows) {
    $clientByFrame[[int]$row.frame] = $row
}

$stableFields = @(
    "stageID", "stageGroup", "vsMode", "sceneCurrentSceneID",
    "vsStarActorFound", "vsStarActorX", "vsStarActorY",
    "playerActor0Found", "playerActor0X", "playerActor0Y", "playerActor0Z",
    "playerActor1Found", "playerActor1X", "playerActor1Y", "playerActor1Z",
    "movingHazardFound", "movingHazardX", "movingHazardY",
    "objectActiveCount", "objectDeadCount",
    "player0Lives", "player1Lives", "player0BattleStars", "player1BattleStars",
    "player0Dead", "player1Dead", "player0InventoryPowerup", "player1InventoryPowerup",
    "playerGlobalHash", "wifiCandidateHash",
    "playerActor0UpdateLocked", "playerActor1UpdateLocked",
    "playerActor0VisibleFlag", "playerActor1VisibleFlag"
)
$inputFields = @("inputPlayer0Held", "inputPlayer1Held", "inputPlayer0Pressed", "inputPlayer1Pressed")
$fields = if ($IgnoreSpeculativeInputFields) {
    $stableFields
} else {
    @($stableFields + $inputFields)
}

function RowAtFrame {
    param([object[]]$Rows, [int]$Frame)
    return $Rows | Where-Object { [int]$_.frame -eq $Frame } | Select-Object -First 1
}

function RowsMatchFields {
    param([object]$HostRow, [object]$ClientRow, [string[]]$Fields)
    foreach ($field in $Fields) {
        if ($HostRow.$field -ne $ClientRow.$field) {
            return $false
        }
    }
    return $true
}

foreach ($hostRow in $hostRows) {
    $frame = [int]$hostRow.frame
    if ($frame -lt 900) { continue }
    if (-not $clientByFrame.ContainsKey($frame)) {
        throw "missing client frame $frame"
    }
    $clientRow = $clientByFrame[$frame]
    foreach ($field in $fields) {
        if ($hostRow.$field -ne $clientRow.$field) {
            if ($RollbackSettleFrames -gt 0) {
                for ($settleFrame = $frame + 1; $settleFrame -le $frame + $RollbackSettleFrames; $settleFrame++) {
                    $hostSettle = RowAtFrame -Rows $hostRows -Frame $settleFrame
                    $clientSettle = if ($clientByFrame.ContainsKey($settleFrame)) { $clientByFrame[$settleFrame] } else { $null }
                    if ($null -ne $hostSettle -and $null -ne $clientSettle -and
                        (RowsMatchFields -HostRow $hostSettle -ClientRow $clientSettle -Fields $fields)) {
                        Write-Host "rollback transient mismatch settled frame=$frame settleFrame=$settleFrame field=$field"
                        break
                    }
                }
                if ($settleFrame -le $frame + $RollbackSettleFrames) {
                    break
                }
            }
            throw "gameplay mismatch frame=$frame field=$field host=$($hostRow.$field) client=$($clientRow.$field)"
        }
    }
}

if (-not $SkipMovementProbe) {
    $before = RowAtFrame -Rows $hostRows -Frame 1770
    $after = RowAtFrame -Rows $hostRows -Frame 2220
    if ($null -eq $before -or $null -eq $after) {
        throw "missing movement probe rows"
    }
    if ($before.playerActor0X -eq $after.playerActor0X) {
        throw "Mario did not move in host local-input probe"
    }
    if ($before.playerActor1X -eq $after.playerActor1X) {
        throw "Luigi did not move in client local-input probe"
    }
}

Get-Content $hostOut
Get-Content $clientOut
Write-Host "NSMB Mario vs Luigi split local-input smoke passed: frames=$Frames log=$logRoot"
