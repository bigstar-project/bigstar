param(
    [string[]]$Candidate = @("tinycorepreimage-delay6-lead999-rbwait3000-maxresim2-bundle8"),
    [string[]]$Route = @("stocktouch", "chaos", "death", "contact", "dualstresslong"),
    [string]$LogRoot = "logs\nsmb-mvl-practical-rollback-suite",
    [int]$WaitTimeoutMs = 720000,
    [int]$StallTimeoutMs = 5000,
    [int]$HostStartupDelayMs = 1200,
    [int]$SeedWaitTimeoutMs = 30000,
    [int]$PacketBridgePort = 18165,
    [int]$FrameHeartbeatInterval = 30,
    [int]$InputSendDelayFrames = 0,
    [int]$InputSendJitterFrames = 0,
    [switch]$InputUnreliable,
    [switch]$NoGameStateTrace,
    [switch]$StateSync,
    [switch]$FpsSpikeTrace,
    [switch]$RollbackStatsTrace,
    [switch]$RollbackStatsTraceSummaryOnly,
    [double]$MaxAverageFrameMs = 0.0,
    [double]$MaxActiveFrameMs = 90.0,
    [double]$MaxRollbackFrameMs = 0.0,
    [int]$MaxActiveFrameOver33ms = 80,
    [int]$MaxConsecutiveSlowFrames = 4,
    [double]$SlowFrameThresholdMs = 33.0
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$smokeScript = Join-Path $PSScriptRoot "run-nsmb-mvl-split-local-input-smoke.ps1"
$runStamp = Get-Date -Format "yyyyMMdd-HHmmss"
$runRootRel = Join-Path $LogRoot $runStamp
$runRoot = Join-Path $repoRoot $runRootRel
New-Item -ItemType Directory -Force $runRoot | Out-Null

function New-SplitInputFromCombined {
    param(
        [string]$CombinedPath,
        [string]$OutputDir
    )

    New-Item -ItemType Directory -Force $OutputDir | Out-Null
    $hostPath = Join-Path $OutputDir "host.inputs"
    $clientPath = Join-Path $OutputDir "client.inputs"
    $hostLines = New-Object System.Collections.Generic.List[string]
    $clientLines = New-Object System.Collections.Generic.List[string]
    $hostLines.Add("# Generated from $CombinedPath inst0 lines.")
    $clientLines.Add("# Generated from $CombinedPath inst1 lines.")
    foreach ($line in Get-Content -LiteralPath $CombinedPath -Encoding UTF8) {
        if ($line -match '^\s*inst0\s+(.+)$') {
            $hostLines.Add($Matches[1])
        } elseif ($line -match '^\s*inst1\s+(.+)$') {
            $clientLines.Add($Matches[1])
        } elseif ($line -match '^\s*#') {
            $hostLines.Add($line)
            $clientLines.Add($line)
        } elseif ($line.Trim().Length -eq 0) {
            $hostLines.Add($line)
            $clientLines.Add($line)
        } else {
            $hostLines.Add($line)
            $clientLines.Add($line)
        }
    }
    Set-Content -LiteralPath $hostPath -Encoding UTF8 -Value $hostLines
    Set-Content -LiteralPath $clientPath -Encoding UTF8 -Value $clientLines
    return [pscustomobject]@{ Host = $hostPath; Client = $clientPath }
}

function Get-LastLineMatching {
    param([string]$Path, [string]$Pattern)
    if (-not (Test-Path $Path)) { return "" }
    $matches = Select-String -LiteralPath $Path -Pattern $Pattern -ErrorAction SilentlyContinue
    if (-not $matches) { return "" }
    return $matches[-1].Line
}

function Get-TimingFields {
    param([string]$Line)
    $result = @{
        Avg = ""
        Max = ""
        Over33 = ""
    }
    if ($Line -match 'avgFrameMs=([0-9.]+)') { $result.Avg = $Matches[1] }
    if ($Line -match 'maxFrameMs=([0-9.]+)') { $result.Max = $Matches[1] }
    if ($Line -match 'over33ms=([0-9]+)') { $result.Over33 = $Matches[1] }
    return $result
}

function Get-PerfSpikeFields {
    param([string]$Path)

    $result = @{
        Total = 0
        Rollback = 0
        NonRollback = 0
        MaxMs = ""
        RollbackMaxMs = ""
        NonRollbackMaxMs = ""
    }
    if (-not (Test-Path -LiteralPath $Path)) {
        return $result
    }

    $maxMs = 0.0
    $rollbackMaxMs = 0.0
    $nonRollbackMaxMs = 0.0
    $lastRestores = 0
    $lastResims = 0
    foreach ($line in Get-Content -LiteralPath $Path -Encoding UTF8) {
        if ($line -notmatch "NSMB PerfSpike: .*frameTimeUs=([0-9]+)") {
            continue
        }

        $frameMs = [double]$Matches[1] / 1000.0
        $result.Total++
        if ($frameMs -gt $maxMs) {
            $maxMs = $frameMs
        }

        $restoreDelta = 0
        $resimDelta = 0
        if ($line -match "rollbackRestoreDelta=([0-9]+).*rollbackResimDelta=([0-9]+)") {
            $restoreDelta = [int]$Matches[1]
            $resimDelta = [int]$Matches[2]
        } elseif ($line -match "rollbackRestores=([0-9]+).*rollbackResims=([0-9]+)") {
            $restores = [int]$Matches[1]
            $resims = [int]$Matches[2]
            $restoreDelta = $restores - $lastRestores
            $resimDelta = $resims - $lastResims
            $lastRestores = $restores
            $lastResims = $resims
        }

        if ($restoreDelta -gt 0 -or $resimDelta -gt 0) {
            $result.Rollback++
            if ($frameMs -gt $rollbackMaxMs) {
                $rollbackMaxMs = $frameMs
            }
        } else {
            $result.NonRollback++
            if ($frameMs -gt $nonRollbackMaxMs) {
                $nonRollbackMaxMs = $frameMs
            }
        }
    }

    if ($result.Total -gt 0) {
        $result.MaxMs = "{0:F3}" -f $maxMs
    }
    if ($result.Rollback -gt 0) {
        $result.RollbackMaxMs = "{0:F3}" -f $rollbackMaxMs
    }
    if ($result.NonRollback -gt 0) {
        $result.NonRollbackMaxMs = "{0:F3}" -f $nonRollbackMaxMs
    }
    return $result
}

function Get-ScratchSpikeFields {
    param([string]$Path)

    $result = @{
        Count = 0
        MaxMs = ""
        ThrottleMaxMs = ""
        RemoteWaitMaxMs = ""
        WriteMaxMs = ""
    }
    if (-not (Test-Path -LiteralPath $Path)) {
        return $result
    }

    $maxMs = 0.0
    $throttleMaxMs = 0.0
    $remoteWaitMaxMs = 0.0
    $writeMaxMs = 0.0
    foreach ($line in Get-Content -LiteralPath $Path -Encoding UTF8) {
        if ($line -notmatch "NSMB PacketBridgeScratchSpike: .*totalMs=([0-9.]+)") {
            continue
        }

        $result.Count++
        $totalMs = [double]$Matches[1]
        if ($totalMs -gt $maxMs) {
            $maxMs = $totalMs
        }
        if ($line -match "throttleMs=([0-9.]+)") {
            $value = [double]$Matches[1]
            if ($value -gt $throttleMaxMs) {
                $throttleMaxMs = $value
            }
        }
        if ($line -match "lockstepRemoteWaitMs=([0-9.]+)") {
            $value = [double]$Matches[1]
            if ($value -gt $remoteWaitMaxMs) {
                $remoteWaitMaxMs = $value
            }
        }
        if ($line -match "writeMs=([0-9.]+)") {
            $value = [double]$Matches[1]
            if ($value -gt $writeMaxMs) {
                $writeMaxMs = $value
            }
        }
    }

    if ($result.Count -gt 0) {
        $result.MaxMs = "{0:F3}" -f $maxMs
        $result.ThrottleMaxMs = "{0:F3}" -f $throttleMaxMs
        $result.RemoteWaitMaxMs = "{0:F3}" -f $remoteWaitMaxMs
        $result.WriteMaxMs = "{0:F3}" -f $writeMaxMs
    }
    return $result
}

function Get-RollbackStatsFields {
    param([string]$Path)

    $result = @{
        SaveAvgUs = ""
        SaveMaxUs = ""
        RestoreAvgUs = ""
        RestoreMaxUs = ""
        ResimRunAvgUs = ""
        ResimRunMaxUs = ""
        ResimTotalAvgUs = ""
        ResimTotalMaxUs = ""
        Predicted = ""
        Predictions = ""
        Restores = ""
        Resims = ""
    }
    $line = Get-LastLineMatching -Path $Path -Pattern "NSMB Rollback: "
    if (-not $line) {
        return $result
    }

    foreach ($key in @(
        "saveAvgUs",
        "saveMaxUs",
        "restoreAvgUs",
        "restoreMaxUs",
        "resimRunAvgUs",
        "resimRunMaxUs",
        "resimTotalAvgUs",
        "resimTotalMaxUs",
        "predicted",
        "predictions",
        "restores",
        "resims"
    )) {
        if ($line -match "$key=([0-9]+)") {
            switch ($key) {
                "saveAvgUs" { $result.SaveAvgUs = $Matches[1] }
                "saveMaxUs" { $result.SaveMaxUs = $Matches[1] }
                "restoreAvgUs" { $result.RestoreAvgUs = $Matches[1] }
                "restoreMaxUs" { $result.RestoreMaxUs = $Matches[1] }
                "resimRunAvgUs" { $result.ResimRunAvgUs = $Matches[1] }
                "resimRunMaxUs" { $result.ResimRunMaxUs = $Matches[1] }
                "resimTotalAvgUs" { $result.ResimTotalAvgUs = $Matches[1] }
                "resimTotalMaxUs" { $result.ResimTotalMaxUs = $Matches[1] }
                "predicted" { $result.Predicted = $Matches[1] }
                "predictions" { $result.Predictions = $Matches[1] }
                "restores" { $result.Restores = $Matches[1] }
                "resims" { $result.Resims = $Matches[1] }
            }
        }
    }
    return $result
}

function Get-RollbackIntegrityError {
    param([string[]]$Paths)
    $patterns = @(
        "NSMB Rollback: cannot resimulate",
        "NSMB Rollback: checkpoint missing",
        "NSMB Rollback: .*chain missing",
        "NSMB Rollback: .*restore failed",
        "NSMB Test: .*rollback.*failed"
    )
    foreach ($path in $Paths) {
        if (-not (Test-Path -LiteralPath $path)) {
            continue
        }
        $hit = Select-String -LiteralPath $path -Pattern $patterns -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($hit) {
            $relative = Resolve-Path -LiteralPath $path -Relative
            return "${relative}:$($hit.LineNumber): $($hit.Line)"
        }
    }
    return ""
}

function Get-TransientMismatchFields {
    param([string]$Path)

    $result = @{
        Count = 0
        MaxFrames = 0
        Fields = ""
    }
    if (-not (Test-Path -LiteralPath $Path)) {
        return $result
    }

    $fields = New-Object System.Collections.Generic.HashSet[string]
    foreach ($line in Get-Content -LiteralPath $Path -Encoding UTF8) {
        if ($line -match "rollback transient mismatch settled frame=([0-9]+) settleFrame=([0-9]+) field=([A-Za-z0-9_]+)") {
            $result.Count++
            $delta = [int]$Matches[2] - [int]$Matches[1]
            if ($delta -gt $result.MaxFrames) {
                $result.MaxFrames = $delta
            }
            [void]$fields.Add($Matches[3])
        } elseif ($line -match "rollback transient playerGlobal mismatch settled frame=([0-9]+) lastMismatchFrame=([0-9]+)") {
            $result.Count++
            $delta = [int]$Matches[2] - [int]$Matches[1]
            if ($delta -gt $result.MaxFrames) {
                $result.MaxFrames = $delta
            }
            [void]$fields.Add("playerGlobal")
        }
    }

    $result.Fields = (($fields | Sort-Object) -join ";")
    return $result
}

$candidateDefs = @{
    "tinycorepreimage-wait0" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 0
        RollbackInputWaitUs = 0
        InputMaxFrameLead = 2
        RollbackWindow = 20
        RollbackCheckpointInterval = 1
        InputBundleHistory = 0
        Env = @{}
    }
    "tinycorepreimage-rbwait500" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 0
        RollbackInputWaitUs = 500
        InputMaxFrameLead = 2
        RollbackWindow = 20
        RollbackCheckpointInterval = 1
        InputBundleHistory = 0
        Env = @{}
    }
    "tinycorepreimage-rbwait750" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 0
        RollbackInputWaitUs = 750
        InputMaxFrameLead = 2
        RollbackWindow = 20
        RollbackCheckpointInterval = 1
        InputBundleHistory = 0
        Env = @{}
    }
    "tinycorepreimage-rbwait1000" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 0
        RollbackInputWaitUs = 1000
        InputMaxFrameLead = 2
        RollbackWindow = 20
        RollbackCheckpointInterval = 1
        InputBundleHistory = 0
        Env = @{}
    }
    "tinycorepreimage-rbwait1500" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 0
        RollbackInputWaitUs = 1500
        InputMaxFrameLead = 2
        RollbackWindow = 20
        RollbackCheckpointInterval = 1
        InputBundleHistory = 0
        Env = @{}
    }
    "tinycorepreimage-rbwait1500-window32" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 0
        RollbackInputWaitUs = 1500
        InputMaxFrameLead = 2
        RollbackWindow = 32
        RollbackCheckpointInterval = 1
        InputBundleHistory = 0
        Env = @{}
    }
    "tinycorepreimage-rbwait1500-bundle8" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 0
        RollbackInputWaitUs = 1500
        InputMaxFrameLead = 2
        RollbackWindow = 20
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "tinycorepreimage-rbwait1500-lead4" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 0
        RollbackInputWaitUs = 1500
        InputMaxFrameLead = 4
        RollbackWindow = 20
        RollbackCheckpointInterval = 1
        InputBundleHistory = 0
        Env = @{}
    }
    "tinycorepreimage-rbwait1500-lead8" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 0
        RollbackInputWaitUs = 1500
        InputMaxFrameLead = 8
        RollbackWindow = 20
        RollbackCheckpointInterval = 1
        InputBundleHistory = 0
        Env = @{}
    }
    "tinycorepreimage-rbwait1750" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 0
        RollbackInputWaitUs = 1750
        InputMaxFrameLead = 2
        RollbackWindow = 20
        RollbackCheckpointInterval = 1
        InputBundleHistory = 0
        Env = @{}
    }
    "tinycorepreimage-rbwait2000" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 0
        RollbackInputWaitUs = 2000
        InputMaxFrameLead = 2
        RollbackWindow = 20
        RollbackCheckpointInterval = 1
        InputBundleHistory = 0
        Env = @{}
    }
    "tinycorepreimage-delay4-lead999-rbwait1500-bundle8" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 4
        RollbackInputWaitUs = 1500
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "tinycorepreimage-delay4-lead999-rbwait8000-bundle8" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 4
        RollbackInputWaitUs = 8000
        RollbackMaxResimFrames = 0
        RollbackSkipRenderDuringResim = $false
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "tinycorepreimage-delay4-lead999-rbwait0-maxresim2-bundle8" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 4
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "tinycorepreimage-delay6-lead999-rbwait0-maxresim2-bundle8" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 6
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "tinycorepreimage-delay6-lead999-rbwait3000-maxresim2-bundle8" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 6
        RollbackInputWaitUs = 3000
        RollbackMaxResimFrames = 2
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "tinycorepreimage-delay10-lead999-rbwait3000-uncapped-bundle8" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 10
        RollbackInputWaitUs = 3000
        RollbackMaxResimFrames = 0
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "predictrepair-delay2-playerstate" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 2
        }
        Env = @{}
    }
    "predictrepair-delay2-player-world-lite" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 2
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 2
            WorldStateActorRescanInterval = 15
        }
        Env = @{}
    }
    "predictrepair-delay2-player-world-actorsnap" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 8
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{}
    }
    "predictrepair-delay2-player-world-actorsnap-reliableplayer" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateReliable = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 8
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{}
    }
    "predictrepair-delay2-player-world-actorsnap-predict16" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 16
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{}
    }
    "predictrepair-delay2-player-world-actorsnap-predict12" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 12
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{}
    }
    "predictrepair-delay2-player-world-actorsnap-hoststate" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "host-player-globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 8
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{}
    }
    "predictrepair-delay2-player-world-actorsnap-hostglobals" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 8
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{}
    }
    "predictrepair-delay2-player-world-actorsnap-hostglobals-sync10" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 10
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 8
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{}
    }
    "predictrepair-delay2-player-world-actorsnap-staleglobals12" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 8
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "predictrepair-delay2-player-world-actorsnap-staleglobals12-predict12" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 12
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "predictrepair-delay2-player-world-actorsnap-staleglobals12-staletransform12" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 8
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_TRANSFORM_FRAMES = "12"
        }
    }
    "predictrepair-delay2-player-world-actorsnap-staleglobals24" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 8
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "24"
        }
    }
    "predictrepair-delay2-player-world-actorsnap-staleglobals12-staletransform4" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 8
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_TRANSFORM_FRAMES = "4"
        }
    }
    "predictrepair-delay2-player-world-actorsnap-staleglobals12-counter24" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 8
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_COUNTER_FRAMES = "24"
        }
    }
    "predictrepair-delay2-player-world-actorsnap-staleglobals12-fresh2000" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 8
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_PLAYER_STATE_FRESH_WAIT_US = "2000"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "predictrepair-delay2-player-world-actorsnap-staleglobals12-transitiontransform" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 8
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET = "300"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "predictrepair-delay2-player-world-actorsnap-staleglobals12-transitiontransform-pred4" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 4
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET = "300"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "predictrepair-delay2-player-world-actorsnap-staleglobals12-transitiontransform-pred0" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 0
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET = "300"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "predictrepair-delay2-player-world-actorsnap32-lifecycle-pred0" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 0
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT_LIFECYCLE = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET = "300"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "predictrepair-delay2-player-world-actorsnap32-lifecycle-transition0-pred0" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 0
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT_LIFECYCLE = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET = "0"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "predictrepair-delay2-player-world-actorsnap32-lifecycle-transition90-pred0" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 0
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT_LIFECYCLE = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET = "90"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "predictrepair-delay2-player-world-actorsnap32-lifecycle-transition90-worldpred40-pred0" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 0
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 40
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT_LIFECYCLE = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET = "90"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "predictrepair-delay2-player-world-actorsnap32-lifecycle-prune-transition90-pred0" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 0
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT_LIFECYCLE = "1"
            MELONDS_NSML_WORLD_STATE_PRUNE_EXTRA_ACTOR_SNAPSHOT = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET = "90"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "predictrepair-delay2-player-world-actorsnap32-lifecycle-prune-transition90-hazardpred40-pred0" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 0
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT_LIFECYCLE = "1"
            MELONDS_NSML_WORLD_STATE_PRUNE_EXTRA_ACTOR_SNAPSHOT = "1"
            MELONDS_NSML_WORLD_STATE_MOVING_HAZARD_MAX_PREDICT_FRAMES = "40"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET = "90"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "predictrepair-delay2-player-world-actorsnap32-lifecycle-prune-transition90-hazardboot40-pred0" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 0
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT_LIFECYCLE = "1"
            MELONDS_NSML_WORLD_STATE_PRUNE_EXTRA_ACTOR_SNAPSHOT = "1"
            MELONDS_NSML_WORLD_STATE_MOVING_HAZARD_MAX_PREDICT_FRAMES = "40"
            MELONDS_NSML_WORLD_STATE_MOVING_HAZARD_MAX_PREDICT_END_OFFSET = "360"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET = "90"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "predictrepair-delay2-player-world-actorsnap32-lifecycle-prune-transition90-hazardboot40-hazardspawn-pred0" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 0
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT_LIFECYCLE = "1"
            MELONDS_NSML_WORLD_STATE_PRUNE_EXTRA_ACTOR_SNAPSHOT = "1"
            MELONDS_NSML_WORLD_STATE_MOVING_HAZARD_MAX_PREDICT_FRAMES = "40"
            MELONDS_NSML_WORLD_STATE_MOVING_HAZARD_MAX_PREDICT_END_OFFSET = "360"
            MELONDS_NSML_WORLD_STATE_SPAWN_MOVING_HAZARD = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET = "90"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "predictrepair-delay2-player-world-actorsnap32-lifecycle-prune-transition90-hazardboot40-hazardclear-pred0" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 0
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT_LIFECYCLE = "1"
            MELONDS_NSML_WORLD_STATE_PRUNE_EXTRA_ACTOR_SNAPSHOT = "1"
            MELONDS_NSML_WORLD_STATE_MOVING_HAZARD_MAX_PREDICT_FRAMES = "40"
            MELONDS_NSML_WORLD_STATE_MOVING_HAZARD_MAX_PREDICT_END_OFFSET = "360"
            MELONDS_NSML_WORLD_STATE_CLEAR_MOVING_HAZARD_LINK_FIELDS = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET = "90"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "predictrepair-delay2-player-world-actorsnap32-lifecycle-prune-transition90-hazardboot40-hazardspawnclear-pred0" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 0
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT_LIFECYCLE = "1"
            MELONDS_NSML_WORLD_STATE_PRUNE_EXTRA_ACTOR_SNAPSHOT = "1"
            MELONDS_NSML_WORLD_STATE_MOVING_HAZARD_MAX_PREDICT_FRAMES = "40"
            MELONDS_NSML_WORLD_STATE_MOVING_HAZARD_MAX_PREDICT_END_OFFSET = "360"
            MELONDS_NSML_WORLD_STATE_SPAWN_MOVING_HAZARD = "1"
            MELONDS_NSML_WORLD_STATE_CLEAR_MOVING_HAZARD_LINK_FIELDS = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET = "90"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "predictrepair-delay2-player-world-actorsnap32-lifecycle-prune-transition90-fresh-pred0" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 0
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT_LIFECYCLE = "1"
            MELONDS_NSML_WORLD_STATE_PRUNE_EXTRA_ACTOR_SNAPSHOT = "1"
            MELONDS_NSML_WORLD_STATE_PREFER_FRESH_SAMPLES = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET = "90"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "predictrepair-delay2-player-world-actorsnap32-lifecycle-prune-transition90-starsnapspawn-pred0" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 0
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT_LIFECYCLE = "1"
            MELONDS_NSML_WORLD_STATE_PRUNE_EXTRA_ACTOR_SNAPSHOT = "1"
            MELONDS_NSML_WORLD_STATE_SPAWN_ACTOR_SNAPSHOT_STAR_CANDIDATE = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET = "90"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "predictrepair-delay2-player-world-actorsnap32-lifecycle-prune-transition90-staractivate-pred0" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 0
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT_LIFECYCLE = "1"
            MELONDS_NSML_WORLD_STATE_PRUNE_EXTRA_ACTOR_SNAPSHOT = "1"
            MELONDS_NSML_WORLD_STATE_ACTIVATE_DORMANT_ACTOR_SNAPSHOT_STAR_CANDIDATE = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET = "90"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "predictrepair-delay2-player-world-actorsnap32-lifecycle-prune-transition90-activate-pred0" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 0
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT_LIFECYCLE = "1"
            MELONDS_NSML_WORLD_STATE_PRUNE_EXTRA_ACTOR_SNAPSHOT = "1"
            MELONDS_NSML_WORLD_STATE_ACTIVATE_DORMANT_ACTOR_SNAPSHOT_STAR_CANDIDATE = "1"
            MELONDS_NSML_WORLD_STATE_ACTIVATE_DORMANT_MOVING_HAZARD = "1"
            MELONDS_NSML_WORLD_STATE_CLEAR_MOVING_HAZARD_LINK_FIELDS = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET = "90"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "predictrepair-delay2-player-world-actorsnap32-lifecycle-prune-transition90-restorebase-pred0" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 0
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT_LIFECYCLE = "1"
            MELONDS_NSML_WORLD_STATE_PRUNE_EXTRA_ACTOR_SNAPSHOT = "1"
            MELONDS_NSML_WORLD_STATE_ACTIVATE_DORMANT_ACTOR_SNAPSHOT_STAR_CANDIDATE = "1"
            MELONDS_NSML_WORLD_STATE_ACTIVATE_DORMANT_MOVING_HAZARD = "1"
            MELONDS_NSML_WORLD_STATE_RESTORE_MOVING_HAZARD_LAST_BASE = "1"
            MELONDS_NSML_WORLD_STATE_CLEAR_MOVING_HAZARD_LINK_FIELDS = "1"
            MELONDS_NSML_WORLD_STATE_TRACE_MOVING_HAZARDS = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET = "90"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "trace-predictrepair-delay2-player-world-actorsnap32-lifecycle-prune-transition90-restorebase-pred0" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 0
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
            InputNetplayTrace = $true
            InputTraceInterval = 30
            WorldStateTraceMovingHazards = $true
        }
        Env = @{
            MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT_LIFECYCLE = "1"
            MELONDS_NSML_WORLD_STATE_PRUNE_EXTRA_ACTOR_SNAPSHOT = "1"
            MELONDS_NSML_WORLD_STATE_ACTIVATE_DORMANT_ACTOR_SNAPSHOT_STAR_CANDIDATE = "1"
            MELONDS_NSML_WORLD_STATE_ACTIVATE_DORMANT_MOVING_HAZARD = "1"
            MELONDS_NSML_WORLD_STATE_RESTORE_MOVING_HAZARD_LAST_BASE = "1"
            MELONDS_NSML_WORLD_STATE_CLEAR_MOVING_HAZARD_LINK_FIELDS = "1"
            MELONDS_NSML_WORLD_STATE_TRACE_MOVING_HAZARDS = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET = "90"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "predictrepair-delay2-player-world-actorsnap32-lifecycle-prune-transition90-restorebase-hazardpred0-pred0" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $true
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 0
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT_LIFECYCLE = "1"
            MELONDS_NSML_WORLD_STATE_PRUNE_EXTRA_ACTOR_SNAPSHOT = "1"
            MELONDS_NSML_WORLD_STATE_ACTIVATE_DORMANT_ACTOR_SNAPSHOT_STAR_CANDIDATE = "1"
            MELONDS_NSML_WORLD_STATE_ACTIVATE_DORMANT_MOVING_HAZARD = "1"
            MELONDS_NSML_WORLD_STATE_RESTORE_MOVING_HAZARD_LAST_BASE = "1"
            MELONDS_NSML_WORLD_STATE_CLEAR_MOVING_HAZARD_LINK_FIELDS = "1"
            MELONDS_NSML_WORLD_STATE_MOVING_HAZARD_MAX_PREDICT_FRAMES = "0"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET = "90"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "exact-delay2-tinycorepreimage-skiprender" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "exact-delay2-tinycorepreimage-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "exact-delay2-tinycorepreimage-maxresim3-skiprender" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 3
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "trace-exact-delay2-tinycorepreimage-maxresim3-skiprender" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 3
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            InputNetplayTrace = $true
        }
        Env = @{
            MELONDS_NSML_INPUT_TRACE_INTERVAL = "60"
        }
    }
    "exact-delay2-tinycorepreimage-maxresim3-skipmidcp-skiprender" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 3
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackSkipIntermediateResimCheckpoints = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "exact-delay2-rbwait250-tinycorepreimage-maxresim3-skiprender" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 250
        RollbackMaxResimFrames = 3
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "exact-delay2-rbwait500-tinycorepreimage-maxresim3-skiprender" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 500
        RollbackMaxResimFrames = 3
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "exact-delay2-prepump-tinycorepreimage-maxresim3-skiprender" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 3
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{
            MELONDS_NSML_ROLLBACK_PRE_PUMP_BEFORE_RESIM = "1"
        }
    }
    "exact-delay2-tinycorepreimage-maxresim4-skiprender" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 4
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "exact-delay2-tinycorepreimage-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "exact-delay2-lead2-rbwait1500-tinycorepreimage-skiprender" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1500
        RollbackMaxResimFrames = 0
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "exact-delay2-lead2-rbwait3000-tinycorepreimage-skiprender" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 3000
        RollbackMaxResimFrames = 0
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "exact-delay2-lead2-rbwait1500-maxresim2-tinycorepreimage-skiprender" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1500
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "exact-delay2-lead2-rbwait3000-maxresim2-tinycorepreimage-skiprender" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 3000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "exact-delay2-lead999-rbwait1500-tinycorepreimage-skiprender" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1500
        RollbackMaxResimFrames = 0
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "exact-delay2-lead999-rbwait3000-tinycorepreimage-skiprender" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 3000
        RollbackMaxResimFrames = 0
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "exact-delay2-lead999-rbwait1500-maxresim2-tinycorepreimage-skiprender" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1500
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "exact-delay2-lead999-rbwait3000-maxresim2-tinycorepreimage-skiprender" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 3000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "exact-delay2-tinycorepreimage-maxresim2-skipmidcp-skiprender" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackSkipIntermediateResimCheckpoints = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{}
    }
    "exact-delay2-tinycorepreimage-maxresim2-trace-skiprender" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            InputNetplayTrace = $true
        }
        Env = @{
            MELONDS_NSML_INPUT_TRACE_INTERVAL = "60"
        }
    }
    "exact-delay2-tinycorepreimage-prepump-skiprender" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{
            MELONDS_NSML_ROLLBACK_PRE_PUMP_BEFORE_RESIM = "1"
        }
    }
    "exact-delay2-tinycorepreimage-maxresim2-prepump-skiprender" = [pscustomobject]@{
        Backend = "tinycorepreimage"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{
            MELONDS_NSML_ROLLBACK_PRE_PUMP_BEFORE_RESIM = "1"
        }
    }
    "nsmbtinycore-delay2-proclist-arena-stack-noheap-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
        }
    }
    "nsmbtinycore-delay2-proclist-noheap-playerc80-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
        }
    }
    "nsmbtinycore-delay2-proclist-deltapages-noheap-playerc80-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
        }
    }
    "nsmbtinycore-delay2-proclist-deltapages-forcejit-smallc80-noheap-playerc80-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_SMALL_RANGE_MAX = "0xC80"
        }
    }
    "nsmbtinycore-delay2-proclist-deltapages-forcejit-small1000-noheap-playerc80-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_SMALL_RANGE_MAX = "0x1000"
        }
    }
    "nsmbtinycore-delay2-proclist-deltapages-forcejit-small1400-noheap-playerc80-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_SMALL_RANGE_MAX = "0x1400"
        }
    }
    "nsmbtinycore-delay2-proclist-deltapages-forcejit-small2200-noheap-playerc80-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_SMALL_RANGE_MAX = "0x2200"
        }
    }
    "nsmbtinycore-delay2-proclist-deltapages-forcejit-changedpages-noheap-playerc80-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_CHANGED_PAGES = "1"
        }
    }
    "nsmbtinycore-delay2-proclist-deltapages-forcejit-changedpages-codechunk-noheap-playerc80-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_CHANGED_PAGES = "1"
            MELONDS_NSML_ROLLBACK_JIT_CODE_CHUNK_INVALIDATION = "1"
        }
    }
    "nsmbtinycore-delay2-proclist-deltapages-forcejit-addr0200-0210-noheap-playerc80-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_ADDRESS_RANGES = "0x02000000:0x00100000"
        }
    }
    "nsmbtinycore-delay2-proclist-deltapages-forcejit-addr0210-0220-noheap-playerc80-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_ADDRESS_RANGES = "0x02100000:0x00100000"
        }
    }
    "nsmbtinycore-delay2-proclist-deltapages-forcejit-addr0220-0240-noheap-playerc80-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_ADDRESS_RANGES = "0x02200000:0x00200000"
        }
    }
    "nsmbtinycore-delay2-proclist-deltapages-forcejit-addr0200-0220-noheap-playerc80-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_ADDRESS_RANGES = "0x02000000:0x00200000"
        }
    }
    "nsmbtinycore-delay2-proclist-deltapages-forcejit-addr0200-0240-noheap-playerc80-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_ADDRESS_RANGES = "0x02000000:0x00400000"
        }
    }
    "trace-nsmbtinycore-forcejit-all-noheap-playerc80-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
            MELONDS_NSML_ROLLBACK_NSMB_JIT_INVALIDATION_TRACE = "1"
        }
    }
    "trace-nsmbtinycore-forcejit-addr0200-0240-noheap-playerc80-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_ADDRESS_RANGES = "0x02000000:0x00400000"
            MELONDS_NSML_ROLLBACK_NSMB_JIT_INVALIDATION_TRACE = "1"
        }
    }
    "nsmbtinycore-delay2-proclist-deltapages-forcejit-noheap-playerc80-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtinycore-delay2-proclist-deltapages-forcejit-noheap-playerc80-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtinycore-delay2-maxlead2-proclist-deltapages-forcejit-noheap-playerc80-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtinycore-delay2-maxlead2-proclist-heap60-deltapages-forcejit-playerc80-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL = "60"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtinycore-delay2-maxlead2-proclist-deltapages-forcejit-noheap-playerc80-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtinycore-delay2-maxlead2-proclist-deltapages-forcejit-noheap-playerc80-maxresim4-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 4
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtinycore-delay2-maxlead2-proclist-deltapages-forcejit-noheap-playerc80-maxresim2-leadbudget2ms-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtinycore-delay2-maxlead2-proclist-deltapages-forcejit-noheap-playerc80-maxresim2-leadbudget2ms-skipjit-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtinycore-delay2-maxlead2-rbwait500-proclist-deltapages-forcejit-noheap-playerc80-maxresim2-leadbudget2ms-skipjit-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 500
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtinycore-delay2-maxlead2-rbwait1000-proclist-deltapages-forcejit-noheap-playerc80-maxresim2-leadbudget2ms-skipjit-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-skipmidcp" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackSkipIntermediateResimCheckpoints = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-limit1" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-limit1-stack" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-limit1-arena" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-limit1-arena-stack" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-limit1-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-limit1-skippredlead-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_SKIP_PREDICTED_FRAME_LEAD_THROTTLE = "1"
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-limit1-appliedlead-skippredlead-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_SKIP_PREDICTED_FRAME_LEAD_THROTTLE = "1"
            MELONDS_NSML_ROLLBACK_THROTTLE_APPLIED_FRAME_LEAD = "1"
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-lead999-limit1-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-lead4-limit1-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 4
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-lead6-limit1-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 6
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr8-lead999-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr8-lead999-limit1-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr8-lead4-budget2ms-limit1-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 4
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr8-lead6-budget2ms-limit1-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 6
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr6-lead6-budget2ms-resend5-limit1-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 6
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 6
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_INPUT_FRAME_LEAD_RESEND_INTERVAL_MS = "5"
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr8-lead6-budget2ms-resend5-limit1-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 6
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_INPUT_FRAME_LEAD_RESEND_INTERVAL_MS = "5"
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr6-lead6-budget2ms-resend1-limit1-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 6
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 6
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_INPUT_FRAME_LEAD_RESEND_INTERVAL_MS = "1"
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr6-lead8-budget2ms-resend1-limit1-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 6
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 8
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_INPUT_FRAME_LEAD_RESEND_INTERVAL_MS = "1"
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr6-lead6-budget2ms-noskipjit-limit1-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 6
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 6
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr6-lead6-budget2ms-resend5-noskipjit-limit1-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 6
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 6
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_INPUT_FRAME_LEAD_RESEND_INTERVAL_MS = "5"
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr4-lead6-budget2ms-noskipjit-limit1-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 4
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 6
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr8-lead999-limit1-skipaudio-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_RESIM_SKIP_AUDIO_BUFFER = "1"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr8-lead999-limit1-future4held-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_INPUT_BUNDLE_FUTURE = "4"
            MELONDS_NSML_INPUT_BUNDLE_FUTURE_ASSUME_HELD = "1"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr8-lead999-limit1-future2held-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_INPUT_BUNDLE_FUTURE = "2"
            MELONDS_NSML_INPUT_BUNDLE_FUTURE_ASSUME_HELD = "1"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr4-lead999-limit1-future4held-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 4
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_INPUT_BUNDLE_FUTURE = "4"
            MELONDS_NSML_INPUT_BUNDLE_FUTURE_ASSUME_HELD = "1"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr3-lead999-limit1-future4held-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 3
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_INPUT_BUNDLE_FUTURE = "4"
            MELONDS_NSML_INPUT_BUNDLE_FUTURE_ASSUME_HELD = "1"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr8-lead999-limit1-future4spec-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_INPUT_BUNDLE_FUTURE = "4"
            MELONDS_NSML_INPUT_BUNDLE_FUTURE_ASSUME_HELD = "1"
            MELONDS_NSML_INPUT_BUNDLE_FUTURE_SPECULATIVE = "1"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr4-lead999-limit1-future4spec-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 4
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_INPUT_BUNDLE_FUTURE = "4"
            MELONDS_NSML_INPUT_BUNDLE_FUTURE_ASSUME_HELD = "1"
            MELONDS_NSML_INPUT_BUNDLE_FUTURE_SPECULATIVE = "1"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr8-lead999-limit1-skipmidcp-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_RESIM_SKIP_INTERMEDIATE_CHECKPOINTS = "1"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr8-lead999-limit1-postframe-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_RESIM_POST_FRAME = "1"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr8-lead999-limit1-rd2-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulateDelayFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr8-lead999-limit1-consume-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_RESIM_CONSUME_CURRENT_FRAME = "1"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr6-lead999-limit1-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 6
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr8-lead999-limit1-cp2-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr4-lead999-limit1-arena-stack-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 4
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-limit1-arena-stack-gpu3donly" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x341"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-limit1-arena-stack-gpu3d" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x37D"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-limit1-widearena-stack-gpu3d" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x37D"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_LENGTH = "0x17000"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-limit1-widearena10000-stack-gpu3d" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x37D"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_LENGTH = "0x10000"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-limit1-arena-stack-heap60-gpu3d" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x37D"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL = "60"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-limit1-arena-stack-heap60-devices-gpu3dlight" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL = "60"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-limit1-arena-stack-heap60-gpu3donly" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x341"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL = "60"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-limit1-arena-stack-heap1-gpu3d" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x37D"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-limit1-cp2-arena-stack-heap60-gpu3d" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x37D"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL = "60"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-limit1-cp2-adaptive1-arena-stack-heap60-gpu3d" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x37D"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_ADAPTIVE_CHECKPOINT_CRITICAL_INTERVAL = "1"
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL = "60"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr4-limit1-cp2-arena-stack-heap60-gpu3d" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 4
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x37D"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL = "60"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-limit1-cp4-arena-stack-heap60-gpu3d" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x37D"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 4
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL = "60"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr8-arena-stack-heap60-gpu3d" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x37D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL = "60"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-limit1-heap60" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL = "60"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr1-limit1-arena" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr1-limit1-stack" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr1-limit1-heap60" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL = "60"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-limit2-heap60" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "2"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL = "60"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr2-limit1-heap60" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL = "60"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr2-limit1-heap60-skipmidcp" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackSkipIntermediateResimCheckpoints = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL = "60"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr2-limit1-heap60-skipresimcp" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackSkipIntermediateResimCheckpoints = $true
        RollbackSkipFinalResimCheckpoint = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL = "60"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb500-mr2-limit1-heap60" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 500
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL = "60"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb0-mr2-limit1-heap60-maxlead1" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 1
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL = "60"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-limit1-skipmidcp" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackSkipIntermediateResimCheckpoints = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1500-mr2-limit1" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1500
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-consume-skipmidcp" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackSkipIntermediateResimCheckpoints = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_RESIM_CONSUME_CURRENT_FRAME = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtc-rb1000-mr2-skipresimcp" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackSkipIntermediateResimCheckpoints = $true
        RollbackSkipFinalResimCheckpoint = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtinycore-delay2-maxlead2-rbwait1000-proclist-deltapages-forcejit-noheap-playerc80-maxresim1-leadbudget2ms-skipjit-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtinycore-delay2-maxlead2-proclist-deltapages-forcejit-noheap-playerc80-maxresim2-leadbudget2ms-fastjitreset-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtinycore-delay2-maxlead2-proclist-deltapages-forcejit-noheap-playerc80-maxresim2-leadbudget1ms-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "1000"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtinycore-delay2-maxlead2-proclist-deltapages-forcejit-noheap-playerc80-maxresim2-leadbudget3ms-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "3000"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtinycore-delay2-maxlead2-proclist-deltapages-forcejit-noheap-playerc80-maxresim1-leadbudget3ms-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "3000"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtinycore-delay2-maxlead2-proclist-deltapages-forcejit-noheap-playerc80-maxresim2-inputwait500us-leadbudget3ms-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 500
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "3000"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtinycore-delay2-maxlead2-proclist-deltapages-forcejit-noheap-playerc80-maxresim2-inputwait1000us-leadbudget3ms-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "3000"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtinycore-delay2-maxlead2-proclist-deltapages-forcejit-noheap-playerc80-maxresim2-leadbudget4ms-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "4000"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtinycore-delay2-maxlead2-proclist-heap60-deltapages-forcejit-playerc80-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL = "60"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtinycore-delay2-maxlead2-proclist-heap15-deltapages-forcejit-playerc80-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL = "15"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
        }
    }
    "nsmbtinycore-delay2-proclist-deltapages-forcejit-noheap-playerc80-maxresim1-limit1-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
        }
    }
    "nsmbtinycore-delay2-proclist-deltapages-forcejit-noheap-playerc80-maxresim1-limit2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "2"
        }
    }
    "nsmbtinycore-delay2-proclist-deltapages-forcejit-codechunk-noheap-playerc80-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION = "1"
            MELONDS_NSML_ROLLBACK_JIT_CODE_CHUNK_INVALIDATION = "1"
        }
    }
    "nsmbtinycore-delay2-proclist-deltapages-interpresim-noheap-playerc80-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_DISABLE_JIT_DURING_RESIM = "1"
        }
    }
    "trace-nsmbtinycore-delay2-proclist-deltapages-noheap-playerc80-restorediff" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_RESTORE_DIFF_TRACE = "1"
        }
    }
    "nsmbtinycore-delay2-proclist-playerc80-repair-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 0
            WorldStateSync = $true
            WorldStateApply = $true
            WorldStateSpawnItem = $true
            WorldStateApplyMovingHazard = $true
            WorldStateApplyEffects = $true
            WorldStateApplyActorSnapshot = $true
            WorldStateSyncInterval = 2
            WorldStateMaxPredictFrames = 8
            WorldStateActorRescanInterval = 15
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT_LIFECYCLE = "1"
            MELONDS_NSML_WORLD_STATE_PRUNE_EXTRA_ACTOR_SNAPSHOT = "1"
            MELONDS_NSML_WORLD_STATE_ACTIVATE_DORMANT_ACTOR_SNAPSHOT_STAR_CANDIDATE = "1"
            MELONDS_NSML_WORLD_STATE_ACTIVATE_DORMANT_MOVING_HAZARD = "1"
            MELONDS_NSML_WORLD_STATE_RESTORE_MOVING_HAZARD_LAST_BASE = "1"
            MELONDS_NSML_WORLD_STATE_CLEAR_MOVING_HAZARD_LINK_FIELDS = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET = "90"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "nsmbtinycore-delay2-proclist-playerc80-playerrepair-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            StateSync = $true
            StateApply = $true
            StateApplyMode = "globals"
            StateSyncExtended = $true
            StateSyncInterval = 30
            PlayerStateSync = $true
            PlayerStateApply = $true
            PlayerStateGlobals = $true
            PlayerStateSyncInterval = 1
            PlayerStateMaxPredictFrames = 0
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM = "1"
            MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET = "90"
            MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES = "12"
        }
    }
    "nsmbtinycore-delay2-proclist-noheap-playerc80-maxresim4-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 4
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
        }
    }
    "nsmbtinycore-delay2-proclist-noheap-playerc80-maxresim8-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
        }
    }
    "nsmbtinycore-delay2-proclist-noheap-playerc80-hazard500-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_MOVING_HAZARD_OBJECT_LENGTH = "0x500"
        }
    }
    "nsmbtinycore-delay2-proclist-safe-fields-noheap-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "nsmbtinycore"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_SAFE_OBJECT_FIELD_RANGES = "1"
        }
    }
    "exact-delay2-coredelta-skiprender" = [pscustomobject]@{
        Backend = "coredelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "30"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-coreframedelta-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "coreframedelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-coreframedelta-maxresim8-skiprender" = [pscustomobject]@{
        Backend = "coreframedelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-rbwait1000-coreframedelta-maxresim8-skiprender" = [pscustomobject]@{
        Backend = "coreframedelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-rbwait3000-coreframedelta-maxresim8-skiprender" = [pscustomobject]@{
        Backend = "coreframedelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 3000
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-cart-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x251"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-wifi-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x249"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-spu-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x245"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-micspirtc-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x261"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-devices-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-fullgpu-devices-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x3E"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-page4096-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "4096"
        }
    }
    "exact-delay2-tinycoreramdelta-key2-page256-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-key3-page256-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "3"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-key2-page256-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-key2-page256-cp2-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-rbwait500-tinycoreramdelta-key2-page256-cp2-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 500
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-fastjitreset-tinycoreramdelta-key2-page256-cp2-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
        }
    }
    "exact-delay2-fastjitreset-nodelete-tinycoreramdelta-key2-page256-cp2-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
        }
    }
    "exact-delay2-rbwait1000-tinycoreramdelta-key2-page256-cp2-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-lead2-tinycoreramdelta-key2-page256-cp2-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-lead2-rbwait1000-tinycoreramdelta-key2-page256-cp2-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-key2-page256-cp2-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-key2-page256-cp1-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-future4-tinycoreramdelta-key2-page256-cp1-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_INPUT_BUNDLE_FUTURE = "4"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "trace-exact-delay2-tinycoreramdelta-key2-page256-cp1-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
            InputNetplayTrace = $true
        }
        Env = @{
            MELONDS_NSML_INPUT_TRACE_INTERVAL = "60"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-key2-page256-cp1-resimdelay1-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackResimulateDelayFrames = 1
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-key2-page256-cp1-resimdelay2-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackResimulateDelayFrames = 2
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-key2-page256-cp1-resimdelay4-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackResimulateDelayFrames = 4
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-conservativejit8-tinycoreramdelta-key2-page256-cp2-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_CONSERVATIVE_JIT = "1"
            MELONDS_NSML_ROLLBACK_CONSERVATIVE_JIT_MAX_BLOCK = "8"
        }
    }
    "exact-delay2-jitnoliteral-tinycoreramdelta-key2-page256-cp2-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
        }
    }
    "exact-delay2-jitnoliteral-nobranch-tinycoreramdelta-key2-page256-cp2-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_BRANCH_OPTIMIZATIONS = "0"
        }
    }
    "exact-delay2-jitnoliteral-nofastmem-tinycoreramdelta-key2-page256-cp2-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_FAST_MEMORY = "0"
        }
    }
    "exact-delay2-jitnoliteral-nofastmem-tinycoreramdelta-key2-page256-cp1-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_FAST_MEMORY = "0"
        }
    }
    "exact-delay2-jitmb16-noliteral-nofastmem-tinycoreramdelta-key2-page256-cp2-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_JIT_MAX_BLOCK_SIZE = "16"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_FAST_MEMORY = "0"
        }
    }
    "exact-delay2-jitmb8-noliteral-nofastmem-tinycoreramdelta-key2-page256-cp2-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_JIT_MAX_BLOCK_SIZE = "8"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_FAST_MEMORY = "0"
        }
    }
    "exact-delay2-jitmb8-noliteral-nofastmem-tinycoreramdelta-key2-page256-cp1-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_JIT_MAX_BLOCK_SIZE = "8"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_FAST_MEMORY = "0"
        }
    }
    "exact-delay2-jitmb8-noliteral-nofastmem-tinycoreramdelta-key2-page256-cp3-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 3
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_JIT_MAX_BLOCK_SIZE = "8"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_FAST_MEMORY = "0"
        }
    }
    "exact-delay2-jitmb8-noliteral-nofastmem-tinycoreramdelta-key2-page512-cp2-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "512"
            MELONDS_NSML_JIT_MAX_BLOCK_SIZE = "8"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_FAST_MEMORY = "0"
        }
    }
    "exact-delay2-jitmb8-noliteral-nobranch-nofastmem-tinycoreramdelta-key2-page256-cp2-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_JIT_MAX_BLOCK_SIZE = "8"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_BRANCH_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_FAST_MEMORY = "0"
        }
    }
    "exact-delay2-jitmb4-noliteral-nofastmem-tinycoreramdelta-key2-page256-cp2-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_JIT_MAX_BLOCK_SIZE = "4"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_FAST_MEMORY = "0"
        }
    }
    "exact-delay2-jitmb4-noliteral-nobranch-nofastmem-tinycoreramdelta-key2-page256-cp2-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_JIT_MAX_BLOCK_SIZE = "4"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_BRANCH_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_FAST_MEMORY = "0"
        }
    }
    "exact-delay2-fastjitreset-nodelete0-tinycoreramdelta-key2-page256-cp2-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
        }
    }
    "exact-delay2-fastjitreset-nodelete0-tinycoreramdelta-key2-page256-cp1-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
        }
    }
    "exact-delay2-fastjitreset-clearlookup-nodelete0-tinycoreramdelta-key2-page256-cp1-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
        }
    }
    "exact-delay2-fastjitreset-keepcodemem-clearlookup-nodelete0-tinycoreramdelta-key2-page256-cp1-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_KEEP_CODEMEM = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
        }
    }
    "exact-delay2-fastjitreset-reusecand-clearlookup-nodelete0-tinycoreramdelta-key2-page256-cp1-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_RESTORE_CANDIDATES = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
        }
    }
    "jitrc-cp1" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_RESTORE_CANDIDATES = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
        }
    }
    "jitrc-dirty-cp1" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP_DIRTY = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_RESTORE_CANDIDATES = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
        }
    }
    "jitrc-dirty-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP_DIRTY = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_RESTORE_CANDIDATES = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
        }
    }
    "jitrc-dirty-cp1-maxlead0-leadbudget3ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "3000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP_DIRTY = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_RESTORE_CANDIDATES = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
        }
    }
    "jitrc-dirty-cp1-maxlead0-leadbudget4ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "4000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP_DIRTY = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_RESTORE_CANDIDATES = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
        }
    }
    "jitrc-used-cp1" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP_USED = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_RESTORE_CANDIDATES = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
        }
    }
    "jitrc-nomem-cp1" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_RESTORE_CANDIDATES = "1"
            MELONDS_NSML_ROLLBACK_JIT_RESTORE_NO_MEMORY_BLOCKS = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
        }
    }
    "jitmap-cp1" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_MAP_RESET_ONLY = "1"
        }
    }
    "skipjit-rbwait250-cp1" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 250
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
        }
    }
    "skipjit-rbwait500-cp1" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 500
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
        }
    }
    "skipjit-rbwait500-cp1-maxlead2-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 500
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
        }
    }
    "skipjit-rbwait500-cp1-maxlead1-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 500
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 1
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
        }
    }
    "skipjit-rbwait500-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 500
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
        }
    }
    "skipjit-lookupinv-rbwait500-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 500
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_RESET_LOOKUP_INVALIDATED_REGIONS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
        }
    }
    "skipjit-rbwait500-cp1-maxlead2-leadbudget3ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 500
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "3000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
        }
    }
    "skipjit-rbwait500-cp1-maxlead2-leadbudget4ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 500
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "4000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
        }
    }
    "skipjit-rbwait500-cp1-maxlead2-leadbudget4ms-skipfinal" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 500
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "4000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_RESIM_SKIP_FINAL_CHECKPOINT = "1"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
        }
    }
    "skipjit-page512-cp1" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "512"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
        }
    }
    "trace-skipjit-page512-cp1-chaos990" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "512"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_DELTA_PAGE_TRACE = "1"
            MELONDS_NSML_ROLLBACK_DELTA_PAGE_TRACE_START_FRAME = "990"
            MELONDS_NSML_ROLLBACK_DELTA_PAGE_TRACE_END_FRAME = "1120"
            MELONDS_NSML_ROLLBACK_DELTA_PAGE_TRACE_MAX_RUNS = "80"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH = "0xC80"
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
        }
    }
    "skipjit-page512-cp1-rbwait500" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 500
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "512"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
        }
    }
    "skipjit-page512-cp1-rbwait1000" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "512"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
        }
    }
    "skipjit-page512-cp1-rd2" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackResimulateDelayFrames = 2
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "512"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
        }
    }
    "skipjit-page1024-cp1" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "1024"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
        }
    }
    "skipjit-codechunk-cp1" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_CODE_CHUNK_INVALIDATION = "1"
        }
    }
    "skipjit-skipfinalcp-cp1" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_RESIM_SKIP_FINAL_CHECKPOINT = "1"
        }
    }
    "jitlookup-cp1" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
        }
    }
    "jitlookup-cp1-maxlead2-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
        }
    }
    "jitlookup-cp1-maxlead1-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 1
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
        }
    }
    "jitlookup-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
        }
    }
    "jitlookup-consume-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_RESIM_CONSUME_CURRENT_FRAME = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
        }
    }
    "jitlookup-usedonly-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_USED_ONLY = "1"
        }
    }
    "jitlookup-usedspans-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_USED_SPANS = "1"
        }
    }
    "jitlookup-usedchunks-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_USED_CHUNKS = "1"
        }
    }
    "jitlookup-compiledchunks-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
        }
    }
    "jitlookup-compiledchunks-codechunk-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_JIT_CODE_CHUNK_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-cp1-maxlead0-leadbudget2ms-poll250" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_INPUT_FRAME_LEAD_THROTTLE_POLL_US = "250"
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-cp1-maxlead0-leadbudget2ms-poll100" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_INPUT_FRAME_LEAD_THROTTLE_POLL_US = "100"
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-cp1-resimdelay1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulateDelayFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-cp1-resimdelay2-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulateDelayFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-appliedlead-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_THROTTLE_APPLIED_FRAME_LEAD = "1"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-cp2-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-adaptivecp2-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
            MELONDS_NSML_ROLLBACK_ADAPTIVE_CHECKPOINT_CRITICAL_INTERVAL = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-postframe-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
            MELONDS_NSML_ROLLBACK_RESIM_POST_FRAME = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-prepump-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
            MELONDS_NSML_ROLLBACK_PRE_PUMP_BEFORE_RESIM = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-future4-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
            MELONDS_NSML_INPUT_BUNDLE_FUTURE = "4"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-rbwait500-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 500
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-rbwait1000-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-rbwait1000-cp2-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-rbwait1000-cp2-resimdelay1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackResimulateDelayFrames = 1
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-rbwait1000-cp2-resimdelay2-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackResimulateDelayFrames = 2
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-rbwait750-cp2-resimdelay2-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 750
        RollbackResimulateDelayFrames = 2
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-rbwait500-cp2-resimdelay2-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 500
        RollbackResimulateDelayFrames = 2
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-rbwait500-cp2-resimdelay2-maxlead1-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 500
        RollbackResimulateDelayFrames = 2
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 1
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-rbwait250-cp2-resimdelay2-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 250
        RollbackResimulateDelayFrames = 2
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-rbwait1000-cp3-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 3
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-rbwait1000-appliedlead-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_THROTTLE_APPLIED_FRAME_LEAD = "1"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-rbwait750-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 750
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-rbwait1000-poll50-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_INPUT_WAIT_POLL_US = "50"
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-rbwait1000-cp1-maxlead0-leadbudget4ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "4000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-rbwait1000-cp1-maxlead0-leadbudget6ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "6000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-rbwait1000-cp1-noleadlimit" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = -1
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "0"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-skipfinal-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
            MELONDS_NSML_ROLLBACK_RESIM_SKIP_FINAL_CHECKPOINT = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-cp1-maxlead0-leadbudget2ms-maxcorr1" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-consume-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
            MELONDS_NSML_ROLLBACK_RESIM_CONSUME_CURRENT_FRAME = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-cp1-maxlead2-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-rbwait1000-cp1-maxlead2-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-cp1-maxlead1-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 1
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-rbwait1000-cp1-maxlead1-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 1
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-cp1-maxlead0-leadbudget4ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "4000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-compiledchunks-skiprestorejit-cp1-maxlead0-leadbudget6ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "6000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS = "1"
            MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION = "1"
        }
    }
    "jitlookup-cp1-maxlead0-leadbudget4ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "4000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
        }
    }
    "jitlookup-cp1-maxlead0-leadbudget6ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "6000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
        }
    }
    "fastregion12b6-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_REGION_MASK = "0x12B6"
        }
    }
    "fastregion12b6-clearlookupdirty-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP_DIRTY = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_REGION_MASK = "0x12B6"
        }
    }
    "jitlookup-cp1-maxlead2-leadbudget2ms-skipfinal" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_RESIM_SKIP_FINAL_CHECKPOINT = "1"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
        }
    }
    "jitlookup-cp1-maxlead2-leadbudget3ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "3000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
        }
    }
    "jitlookup-cp1-maxlead2-leadbudget4ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "4000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
        }
    }
    "jitlookup-cp1-maxlead2-inputwait2ms-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 2000
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
        }
    }
    "jitlookup-cp1-maxlead2-inputwait4ms-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 4000
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
        }
    }
    "jitlookup-cp1-maxlead2-inputwait2ms-leadbudget4ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 2000
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "4000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
        }
    }
    "jitlookup-cp1-maxlead2-leadbudget6ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "6000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY = "1"
        }
    }
    "jitlookup-cp1-maxlead2-leadbudget3ms-nojit" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        AllowJit = $false
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "3000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-maxresim8-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-fullgpu-devices-maxresim8-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x3E"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-devices-maxresim8-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x27D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-devices-vram-maxresim8-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x2FD"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-devices-gpu3d-maxresim8-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x37D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-devices-gpu3d-maxresim8-limit1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x37D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
        }
    }
    "exact-delay2-tinycoreramdelta-devices-gpu3d-maxresim8-skipmidcp-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x37D"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        RollbackSkipIntermediateResimCheckpoints = $true
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-devices-gpu3d-maxresim8-maxlead2-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x37D"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-devices-gpu3d-maxresim8-maxlead2-leadbudget2ms-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x37D"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
        }
    }
    "exact-delay2-tinycoreramdelta-devices-gpu3d-maxresim8-maxlead2-skipmidcp-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x37D"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        RollbackSkipIntermediateResimCheckpoints = $true
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-cart-maxresim8-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x251"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-wifi-maxresim8-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x249"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-wifi-maxresim6-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 6
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x249"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-wifi-maxresim8-limit1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x249"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
        }
    }
    "exact-delay2-tinycoreramdelta-wifi-maxresim8-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x249"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
        }
    }
    "exact-delay2-tinycoreramdelta-wifi-maxresim8-resimdelay1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x249"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_RESIMULATE_DELAY_FRAMES = "1"
        }
    }
    "exact-delay2-tinycoreramdelta-wifi-maxresim8-resimdelay2-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x249"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_RESIMULATE_DELAY_FRAMES = "2"
        }
    }
    "exact-delay2-tinycoreramdelta-spu-maxresim8-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x245"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-micspirtc-maxresim8-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x261"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-fullgpu-devices-maxresim4-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 4
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x3E"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-fullgpu-devices-maxresim8-limit1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x3E"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME = "1"
        }
    }
    "exact-delay2-tinycoreramdelta-fullgpu-devices-maxresim8-maxlead2-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 8
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x3E"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "jitrc-dirty-cp1-maxlead2-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 2
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP_DIRTY = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_RESTORE_CANDIDATES = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
        }
    }
    "jitrc-used-cp1-maxlead0-leadbudget2ms" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 0
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US = "2000"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP_USED = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_RESTORE_CANDIDATES = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
        }
    }
    "jitrc-nofastmem-cp1" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_RESTORE_CANDIDATES = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_FAST_MEMORY = "0"
        }
    }
    "jitrc-noliteral-cp1" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_RESTORE_CANDIDATES = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
        }
    }
    "jitrc-nofastmem-lit-cp1" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_RESTORE_CANDIDATES = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_JIT_FAST_MEMORY = "0"
        }
    }
    "jitrc-mb8-nofastmem-cp1" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_RESTORE_CANDIDATES = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_JIT_MAX_BLOCK_SIZE = "8"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_FAST_MEMORY = "0"
        }
    }
    "jitrc-straight-cp1" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_RESTORE_CANDIDATES = "1"
            MELONDS_NSML_ROLLBACK_JIT_RESTORE_STRAIGHTLINE_ONLY = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
        }
    }
    "frclr-used-cp1" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP_USED = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
        }
    }
    "frclr-dirty-cp1" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP_DIRTY = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
        }
    }
    "jitrc-noclear-cp1" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_RESTORE_CANDIDATES = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
        }
    }
    "jitfr-mb8-cp1" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_JIT_MAX_BLOCK_SIZE = "8"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_FAST_MEMORY = "0"
        }
    }
    "exact-delay2-fastjitreset-nodelete0-jitmb8-noliteral-nofastmem-tinycoreramdelta-key2-page256-cp2-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_JIT_MAX_BLOCK_SIZE = "8"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_FAST_MEMORY = "0"
        }
    }
    "exact-delay2-rbwait1000-fastjitreset-nodelete0-tinycoreramdelta-key2-page256-cp2-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1000
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
        }
    }
    "exact-delay2-rbwait1500-fastjitreset-nodelete0-tinycoreramdelta-key2-page256-cp2-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 1500
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
        }
    }
    "exact-delay2-jitmb8-noliteral-nofastmem-norestorecand-tinycoreramdelta-key2-page256-cp2-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_JIT_MAX_BLOCK_SIZE = "8"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_FAST_MEMORY = "0"
            MELONDS_NSML_JIT_DISABLE_RESTORE_CANDIDATES = "1"
        }
    }
    "exact-delay2-fastjitreset-keepcodemem-nodelete0-tinycoreramdelta-key2-page256-cp2-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_KEEP_CODEMEM = "1"
        }
    }
    "exact-delay2-fastjitreset-nodelete0-interpresim-tinycoreramdelta-key2-page256-cp2-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_DISABLE_JIT_DURING_RESIM = "1"
        }
    }
    "exact-delay2-fastjitreset-partialvolatile-nodelete0-tinycoreramdelta-key2-page256-cp2-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_REGION_MASK = "0x12B6"
        }
    }
    "exact-delay2-fastjitreset-partialvolatile-nodelete0-jitmb8-noliteral-nofastmem-tinycoreramdelta-key2-page256-cp2-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_REGION_MASK = "0x12B6"
            MELONDS_NSML_JIT_MAX_BLOCK_SIZE = "8"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_FAST_MEMORY = "0"
        }
    }
    "exact-delay2-fastjitreset-partialvolatile-clearlookup-nodelete0-tinycoreramdelta-key2-page256-cp1-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_REGION_MASK = "0x12B6"
        }
    }
    "pv-main" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_REGION_MASK = "0x10"
        }
    }
    "pv-maincore" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_REGION_MASK = "0x16"
        }
    }
    "pv-maincore-future4" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_INPUT_BUNDLE_FUTURE = "4"
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_REGION_MASK = "0x16"
        }
    }
    "pv-maincore-rd2" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackResimulateDelayFrames = 2
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_REGION_MASK = "0x16"
        }
    }
    "pv-maincore-clearlookupdirty" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP_DIRTY = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_REGION_MASK = "0x16"
        }
    }
    "pv-maincore-clearlookupused" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP_USED = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_REGION_MASK = "0x16"
        }
    }
    "pv-mainshared" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_REGION_MASK = "0x30"
        }
    }
    "pv-mainshared-arm7" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_REGION_MASK = "0x230"
        }
    }
    "pv-volatile" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_REGION_MASK = "0x12B6"
        }
    }
    "pv-volatile-j8nf" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET = "1"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET = "0"
            MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_REGION_MASK = "0x12B6"
            MELONDS_NSML_JIT_MAX_BLOCK_SIZE = "8"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_FAST_MEMORY = "0"
        }
    }
    "exact-delay2-jitnoliteral-nofastmem-tinycoreramdelta-key2-page1024-cp2-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "1024"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_FAST_MEMORY = "0"
        }
    }
    "exact-delay2-jitnoliteral-nofastmem-tinycoreramdelta-key2-page1024-cp3-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 3
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "1024"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_FAST_MEMORY = "0"
        }
    }
    "exact-delay2-jitnoliteral-nofastmem-tinycoreramdelta-key2-page4096-cp2-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "4096"
            MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS = "0"
            MELONDS_NSML_JIT_FAST_MEMORY = "0"
        }
    }
    "exact-delay2-jitnofastmem-tinycoreramdelta-key2-page256-cp2-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_JIT_FAST_MEMORY = "0"
        }
    }
    "exact-delay2-conservativejit4-tinycoreramdelta-key2-page256-cp2-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_CONSERVATIVE_JIT = "1"
            MELONDS_NSML_ROLLBACK_CONSERVATIVE_JIT_MAX_BLOCK = "4"
        }
    }
    "exact-delay2-tinycoreramdelta-key2-page256-cp2-maxresim1-jitmemreset-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 2
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_ROLLBACK_JIT_MEMORY_RESET_ONLY = "1"
        }
    }
    "exact-delay2-tinycoreramdelta-key2-page256-cp3-maxresim1-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 3
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-key2-page256-maxresim1-skipjit-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 1
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RollbackSkipJitReset = $true
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "2"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-tinycoreramdelta-key1-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "tinycoreramdelta"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        RollbackTinyCoreFlags = "0x241"
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "1"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
        }
    }
    "exact-delay2-arm9ram-maxresim2-skiprender" = [pscustomobject]@{
        Backend = "arm9ram"
        InputDelayFrames = 2
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 2
        RollbackResimulate = $true
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $true
        InputMaxFrameLead = 999
        RollbackWindow = 64
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
    }
    "coredelta-baseline" = [pscustomobject]@{
        Backend = "coredelta"
        InputDelayFrames = 0
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackSkipRenderDuringResim = $false
        InputMaxFrameLead = 8
        RollbackWindow = 20
        RollbackCheckpointInterval = 8
        InputBundleHistory = 0
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "30"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_FIXED_FRAME_SLEEP = "1"
        }
    }
    "norollback-delay4-lead4-bundle8" = [pscustomobject]@{
        RollbackEnabled = $false
        Backend = ""
        InputDelayFrames = 4
        RollbackInputWaitUs = 0
        RollbackMaxResimFrames = 0
        RollbackResimulate = $false
        RollbackPredictOnly = $false
        RollbackSkipRenderDuringResim = $false
        InputMaxFrameLead = 4
        RollbackWindow = 20
        RollbackCheckpointInterval = 1
        InputBundleHistory = 8
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
        }
    }
}

$routeDefs = @{
    "stocktouch" = [pscustomobject]@{
        Frames = 3200
        HostInput = "tests\nsmb_us_direct_mvl_stress_host_move_jump_dash.inputs"
        ClientInput = "tests\nsmb_us_direct_mvl_stress_client_stock_touch.inputs"
        Extra = @{ IgnoreSpeculativeInputFields = $true; RollbackSettleFrames = 30 }
    }
    "chaos" = [pscustomobject]@{
        Frames = 4200
        HostInput = "tests\nsmb_us_direct_mvl_chaos_host.inputs"
        ClientInput = "tests\nsmb_us_direct_mvl_chaos_client.inputs"
        Extra = @{ IgnoreSpeculativeInputFields = $true; RollbackSettleFrames = 60 }
    }
    "death" = [pscustomobject]@{
        Frames = 3600
        HostInput = "tests\nsmb_us_direct_mvl_luigi_death_mario_continues_host.inputs"
        ClientInput = "tests\nsmb_us_direct_mvl_luigi_death_mario_continues_client.inputs"
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 30
            RequirePlayerDeath = $true
            RequirePlayerDeathPlayer = 1
            CheckMovingHazardProgressDuringDeath = $true
            CheckMovingHazardProgressStartFrame = 1800
            CheckMovingHazardProgressEndFrame = 3600
        }
    }
    "contact" = [pscustomobject]@{
        Frames = 3600
        CombinedInput = "tests\nsmb_us_direct_mvl_both_different.inputs"
        Extra = @{ IgnoreSpeculativeInputFields = $true; RollbackSettleFrames = 30 }
    }
    "dualstresslong" = [pscustomobject]@{
        Frames = 7200
        HostInput = "tests\nsmb_us_direct_mvl_stress_host_move_jump_dash_long.inputs"
        ClientInput = "tests\nsmb_us_direct_mvl_stress_client_move_jump_dash_long.inputs"
        Extra = @{ IgnoreSpeculativeInputFields = $true; SkipMovementProbe = $true; RollbackSettleFrames = 60 }
    }
    "dualstressmid" = [pscustomobject]@{
        Frames = 3600
        HostInput = "tests\nsmb_us_direct_mvl_stress_host_move_jump_dash_long.inputs"
        ClientInput = "tests\nsmb_us_direct_mvl_stress_client_move_jump_dash_long.inputs"
        Extra = @{ IgnoreSpeculativeInputFields = $true; SkipMovementProbe = $true; RollbackSettleFrames = 60 }
    }
    "luigistar" = [pscustomobject]@{
        Frames = 3200
        HostInput = "tests\nsmb_us_direct_mvl_luigi_star_right_host.inputs"
        ClientInput = "tests\nsmb_us_direct_mvl_luigi_star_right_client.inputs"
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            RollbackSettleFrames = 30
            RequireStarPickup = $true
            RequireStarPickupPlayer = 1
        }
    }
    "mariostarleft" = [pscustomobject]@{
        Frames = 7200
        CombinedInput = "tests\nsmb_us_direct_mvl_star_collect_left.inputs"
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            RequireStarPickup = $true
            RequireStarPickupPlayer = 0
        }
    }
    "secondgame" = [pscustomobject]@{
        Frames = 10000
        CombinedInput = "tests\nsmb_us_direct_mvl_star_collect_second_game_stress.inputs"
        Extra = @{
            IgnoreSpeculativeInputFields = $true
            SkipMovementProbe = $true
            RollbackSettleFrames = 60
            MvlLives = "3"
            RequireSecondMvlGame = $true
            RequireMvlGameCount = 2
        }
    }
}

$envKeys = @(
    "MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL",
    "MELONDS_NSML_ROLLBACK_STATS_TRACE",
    "MELONDS_NSML_ROLLBACK_STATS_TRACE_SUMMARY_ONLY",
    "MELONDS_NSML_ROLLBACK_STATS_TRACE_INTERVAL",
    "MELONDS_NSML_FPS_SPIKE_TRACE_MAX_LINES",
    "MELONDS_NSML_ROLLBACK_ADAPTIVE_CHECKPOINT_CRITICAL_INTERVAL",
    "MELONDS_NSML_ROLLBACK_MAX_CORRECTIONS_PER_FRAME",
    "MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE",
    "MELONDS_NSML_ROLLBACK_PRE_PUMP_BEFORE_RESIM",
    "MELONDS_NSML_ROLLBACK_SKIP_PREDICTED_FRAME_LEAD_THROTTLE",
    "MELONDS_NSML_ROLLBACK_THROTTLE_APPLIED_FRAME_LEAD",
    "MELONDS_NSML_ROLLBACK_FRAME_LEAD_THROTTLE_BUDGET_US",
    "MELONDS_NSML_INPUT_FRAME_LEAD_THROTTLE_POLL_US",
    "MELONDS_NSML_INPUT_FRAME_LEAD_RESEND_INTERVAL_MS",
    "MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES",
    "MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_LENGTH",
    "MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE",
    "MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES",
    "MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES",
    "MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES",
    "MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL",
    "MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH",
    "MELONDS_NSML_ROLLBACK_NSMB_MOVING_HAZARD_OBJECT_LENGTH",
    "MELONDS_NSML_ROLLBACK_NSMB_SAFE_OBJECT_FIELD_RANGES",
    "MELONDS_NSML_ROLLBACK_NSMB_RESTORE_DIFF_TRACE",
    "MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_RANGE_INVALIDATION",
    "MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_SMALL_RANGE_MAX",
    "MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_CHANGED_PAGES",
    "MELONDS_NSML_ROLLBACK_NSMB_FORCE_JIT_ADDRESS_RANGES",
    "MELONDS_NSML_ROLLBACK_NSMB_JIT_INVALIDATION_TRACE",
    "MELONDS_NSML_ROLLBACK_DELTA_PAGE_TRACE",
    "MELONDS_NSML_ROLLBACK_DELTA_PAGE_TRACE_START_FRAME",
    "MELONDS_NSML_ROLLBACK_DELTA_PAGE_TRACE_END_FRAME",
    "MELONDS_NSML_ROLLBACK_DELTA_PAGE_TRACE_MAX_RUNS",
    "MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT_LIFECYCLE",
    "MELONDS_NSML_WORLD_STATE_PRUNE_EXTRA_ACTOR_SNAPSHOT",
    "MELONDS_NSML_WORLD_STATE_SPAWN_ACTOR_SNAPSHOT_STAR_CANDIDATE",
    "MELONDS_NSML_WORLD_STATE_ACTIVATE_DORMANT_ACTOR_SNAPSHOT_STAR_CANDIDATE",
    "MELONDS_NSML_WORLD_STATE_MOVING_HAZARD_MAX_PREDICT_FRAMES",
    "MELONDS_NSML_WORLD_STATE_MOVING_HAZARD_MAX_PREDICT_END_OFFSET",
    "MELONDS_NSML_WORLD_STATE_SPAWN_MOVING_HAZARD",
    "MELONDS_NSML_WORLD_STATE_ACTIVATE_DORMANT_MOVING_HAZARD",
    "MELONDS_NSML_WORLD_STATE_RESTORE_MOVING_HAZARD_LAST_BASE",
    "MELONDS_NSML_WORLD_STATE_CLEAR_MOVING_HAZARD_LINK_FIELDS",
    "MELONDS_NSML_FIXED_FRAME_SLEEP",
    "MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM",
    "MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET",
    "MELONDS_NSML_PLAYER_STATE_FRESH_WAIT_US",
    "MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES",
    "MELONDS_NSML_PLAYER_STATE_MAX_STALE_COUNTER_FRAMES",
    "MELONDS_NSML_PLAYER_STATE_MAX_STALE_TRANSFORM_FRAMES",
    "MELONDS_NSML_INPUT_TRACE_INTERVAL",
    "MELONDS_NSML_INPUT_WAIT_POLL_US",
    "MELONDS_NSML_ROLLBACK_CONSERVATIVE_JIT",
    "MELONDS_NSML_ROLLBACK_CONSERVATIVE_JIT_MAX_BLOCK",
    "MELONDS_NSML_JIT_MAX_BLOCK_SIZE",
    "MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS",
    "MELONDS_NSML_JIT_BRANCH_OPTIMIZATIONS",
    "MELONDS_NSML_JIT_FAST_MEMORY",
    "MELONDS_NSML_ROLLBACK_JIT_FAST_RESET",
    "MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET",
    "MELONDS_NSML_ROLLBACK_JIT_DEFERRED_DELETE_BUDGET",
    "MELONDS_NSML_JIT_DISABLE_RESTORE_CANDIDATES",
    "MELONDS_NSML_ROLLBACK_JIT_MAP_RESET_ONLY",
    "MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_ONLY",
    "MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_USED_ONLY",
    "MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_USED_SPANS",
    "MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_USED_CHUNKS",
    "MELONDS_NSML_ROLLBACK_JIT_LOOKUP_RESET_COMPILED_CHUNKS",
    "MELONDS_NSML_ROLLBACK_SKIP_RESTORED_JIT_INVALIDATION",
    "MELONDS_NSML_ROLLBACK_JIT_RESET_LOOKUP_INVALIDATED_REGIONS",
    "MELONDS_NSML_ROLLBACK_JIT_CODE_CHUNK_INVALIDATION",
    "MELONDS_NSML_ROLLBACK_ADAPTIVE_CHECKPOINT_CRITICAL_INTERVAL",
    "MELONDS_NSML_ROLLBACK_RESIM_SKIP_AUDIO_BUFFER",
    "MELONDS_NSML_ROLLBACK_RESIM_SKIP_FINAL_CHECKPOINT",
    "MELONDS_NSML_ROLLBACK_RESIM_CONSUME_CURRENT_FRAME",
    "MELONDS_NSML_ROLLBACK_RESIM_POST_FRAME",
    "MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_KEEP_CODEMEM",
    "MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP",
    "MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP_DIRTY",
    "MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_CLEAR_LOOKUP_USED",
    "MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_RESTORE_CANDIDATES",
    "MELONDS_NSML_ROLLBACK_JIT_RESTORE_NO_MEMORY_BLOCKS",
    "MELONDS_NSML_ROLLBACK_JIT_RESTORE_STRAIGHTLINE_ONLY",
    "MELONDS_NSML_ROLLBACK_DISABLE_JIT_DURING_RESIM",
    "MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_REGION_MASK",
    "MELONDS_NSML_INPUT_BUNDLE_FUTURE",
    "MELONDS_NSML_INPUT_BUNDLE_FUTURE_ASSUME_HELD",
    "MELONDS_NSML_INPUT_BUNDLE_FUTURE_SPECULATIVE"
)

$summary = New-Object System.Collections.Generic.List[object]

foreach ($candidateName in $Candidate) {
    if (-not $candidateDefs.ContainsKey($candidateName)) {
        throw "Unknown candidate: $candidateName"
    }
    $candidateDef = $candidateDefs[$candidateName]
    foreach ($routeName in $Route) {
        if (-not $routeDefs.ContainsKey($routeName)) {
            throw "Unknown route: $routeName"
        }
        $routeDef = $routeDefs[$routeName]
        $caseRel = Join-Path $runRootRel (Join-Path $candidateName $routeName)
        $caseDir = Join-Path $repoRoot $caseRel
        $runLog = Join-Path $caseDir "suite-run.txt"
        New-Item -ItemType Directory -Force $caseDir | Out-Null

        foreach ($key in $envKeys) {
            Remove-Item "Env:\$key" -ErrorAction SilentlyContinue
        }
        foreach ($key in $candidateDef.Env.Keys) {
            Set-Item "Env:\$key" "$($candidateDef.Env[$key])"
        }

        $hostInput = $routeDef.HostInput
        $clientInput = $routeDef.ClientInput
        if ($routeDef.PSObject.Properties.Name -contains "CombinedInput") {
            $split = New-SplitInputFromCombined `
                -CombinedPath (Join-Path $repoRoot $routeDef.CombinedInput) `
                -OutputDir (Join-Path $caseDir "inputs")
            $hostInput = $split.Host
            $clientInput = $split.Client
        }

        $rollbackMaxResimFrames = 0
        if ($candidateDef.PSObject.Properties.Name -contains "RollbackMaxResimFrames") {
            $rollbackMaxResimFrames = [int]$candidateDef.RollbackMaxResimFrames
        }
        $rollbackSkipRenderDuringResim = $false
        if ($candidateDef.PSObject.Properties.Name -contains "RollbackSkipRenderDuringResim") {
            $rollbackSkipRenderDuringResim = [bool]$candidateDef.RollbackSkipRenderDuringResim
        }
        $rollbackSkipIntermediateResimCheckpoints = $false
        if ($candidateDef.PSObject.Properties.Name -contains "RollbackSkipIntermediateResimCheckpoints") {
            $rollbackSkipIntermediateResimCheckpoints = [bool]$candidateDef.RollbackSkipIntermediateResimCheckpoints
        }
        $rollbackSkipFinalResimCheckpoint = $false
        if ($candidateDef.PSObject.Properties.Name -contains "RollbackSkipFinalResimCheckpoint") {
            $rollbackSkipFinalResimCheckpoint = [bool]$candidateDef.RollbackSkipFinalResimCheckpoint
        }
        $rollbackResimulate = $true
        if ($candidateDef.PSObject.Properties.Name -contains "RollbackResimulate") {
            $rollbackResimulate = [bool]$candidateDef.RollbackResimulate
        }
        $rollbackPredictOnly = $false
        if ($candidateDef.PSObject.Properties.Name -contains "RollbackPredictOnly") {
            $rollbackPredictOnly = [bool]$candidateDef.RollbackPredictOnly
        }
        $rollbackTinyCoreFlags = ""
        if ($candidateDef.PSObject.Properties.Name -contains "RollbackTinyCoreFlags") {
            $rollbackTinyCoreFlags = [string]$candidateDef.RollbackTinyCoreFlags
        }
        $rollbackResimulateDelayFrames = 0
        if ($candidateDef.PSObject.Properties.Name -contains "RollbackResimulateDelayFrames") {
            $rollbackResimulateDelayFrames = [int]$candidateDef.RollbackResimulateDelayFrames
        }
        $allowJit = $true
        if ($candidateDef.PSObject.Properties.Name -contains "AllowJit") {
            $allowJit = [bool]$candidateDef.AllowJit
        }
        $rollbackEnabled = $true
        if ($candidateDef.PSObject.Properties.Name -contains "RollbackEnabled") {
            $rollbackEnabled = [bool]$candidateDef.RollbackEnabled
        }

        $params = @{
            Frames = $routeDef.Frames
            WaitTimeoutMs = $WaitTimeoutMs
            StallTimeoutMs = $StallTimeoutMs
            HostStartupDelayMs = $HostStartupDelayMs
            SeedWaitTimeoutMs = $SeedWaitTimeoutMs
            PacketBridgePort = $PacketBridgePort
            LogDir = $caseRel
            HostInputScript = $hostInput
            ClientInputScript = $clientInput
            RollbackBackend = $candidateDef.Backend
            RollbackTinyCoreFlags = $rollbackTinyCoreFlags
            RollbackWindow = $candidateDef.RollbackWindow
            RollbackCheckpointInterval = $candidateDef.RollbackCheckpointInterval
            RollbackResimulate = $rollbackResimulate
            RollbackResimulateDelayFrames = $rollbackResimulateDelayFrames
            RollbackInputWaitUs = $candidateDef.RollbackInputWaitUs
            RollbackMaxResimFrames = $rollbackMaxResimFrames
            InputDelayFrames = $candidateDef.InputDelayFrames
            InputMaxFrameLead = $candidateDef.InputMaxFrameLead
            InputSendDelayFrames = $InputSendDelayFrames
            InputSendJitterFrames = $InputSendJitterFrames
            InputBundleHistory = $candidateDef.InputBundleHistory
            NetworkPumpThread = $true
            NetworkPumpSleepUs = 50
            FrameHeartbeatInterval = $FrameHeartbeatInterval
            MaxActiveFrameMs = $MaxActiveFrameMs
            MaxRollbackFrameMs = $MaxRollbackFrameMs
            MaxActiveFrameOver33ms = $MaxActiveFrameOver33ms
            MaxConsecutiveSlowFrames = $MaxConsecutiveSlowFrames
            SlowFrameThresholdMs = $SlowFrameThresholdMs
            GameStateCompareStartFrame = 990
            MvlMatchSeed = "practical-$candidateName-$routeName-$runStamp"
        }
        if ($rollbackEnabled) {
            $params.Rollback = $true
        }
        if ($allowJit) {
            $params.AllowJit = $true
        }
        foreach ($key in $routeDef.Extra.Keys) {
            $params[$key] = $routeDef.Extra[$key]
        }
        if ($rollbackSkipRenderDuringResim) {
            $params.RollbackSkipRenderDuringResim = $true
        }
        if ($rollbackSkipIntermediateResimCheckpoints) {
            $params.RollbackSkipIntermediateResimCheckpoints = $true
        }
        if ($rollbackSkipFinalResimCheckpoint) {
            $params.RollbackSkipFinalResimCheckpoint = $true
        }
        if ($rollbackPredictOnly) {
            $params.RollbackPredictOnly = $true
        }
        if ($candidateDef.PSObject.Properties.Name -contains "Extra") {
            foreach ($key in $candidateDef.Extra.Keys) {
                $params[$key] = $candidateDef.Extra[$key]
            }
        }
        if ($InputUnreliable) {
            $params.InputUnreliable = $true
        }
        if ($NoGameStateTrace) {
            $params.NoGameStateTrace = $true
            if ($params.ContainsKey("CheckMovingHazardProgressDuringDeath")) {
                $params.CheckMovingHazardProgressDuringDeath = $false
            }
        }
        if ($StateSync) {
            $params.StateSync = $true
            $params.StateSyncExtended = $true
            $params.StateSyncInterval = 60
        }
        if ($FpsSpikeTrace) {
            $params.FpsSpikeTrace = $true
        }
        if ($RollbackStatsTrace) {
            $params.RollbackStatsTrace = $true
            if ($RollbackStatsTraceSummaryOnly -or $FpsSpikeTrace) {
                $params.RollbackStatsTraceSummaryOnly = $true
            }
        }

        Write-Host "running practical case: candidate=$candidateName route=$routeName"
        $errorText = ""
        try {
            & $smokeScript @params *> $runLog
        } catch {
            $errorText = $_.Exception.Message
            Add-Content -LiteralPath $runLog -Encoding UTF8 -Value $errorText
        }

        $hostStdout = Join-Path $caseDir "host\host.stdout.txt"
        $clientStdout = Join-Path $caseDir "client\client.stdout.txt"
        $hostTiming = Get-LastLineMatching -Path $hostStdout -Pattern "NSMB Test: active frame timing"
        $clientTiming = Get-LastLineMatching -Path $clientStdout -Pattern "NSMB Test: active frame timing"
        $hostFields = Get-TimingFields -Line $hostTiming
        $clientFields = Get-TimingFields -Line $clientTiming
        $hostPerfSpikes = Get-PerfSpikeFields -Path $hostStdout
        $clientPerfSpikes = Get-PerfSpikeFields -Path $clientStdout
        $hostScratchSpikes = Get-ScratchSpikeFields -Path $hostStdout
        $clientScratchSpikes = Get-ScratchSpikeFields -Path $clientStdout
        $hostRollbackStats = Get-RollbackStatsFields -Path $hostStdout
        $clientRollbackStats = Get-RollbackStatsFields -Path $clientStdout
        $transientFields = Get-TransientMismatchFields -Path $runLog
        if ($MaxAverageFrameMs -gt 0.0) {
            $avgFailures = New-Object System.Collections.Generic.List[string]
            if ($hostFields.Avg -ne "" -and ([double]$hostFields.Avg) -gt $MaxAverageFrameMs) {
                $avgFailures.Add("host avg frame too high: avgMs=$($hostFields.Avg) limit=$MaxAverageFrameMs")
            }
            if ($clientFields.Avg -ne "" -and ([double]$clientFields.Avg) -gt $MaxAverageFrameMs) {
                $avgFailures.Add("client avg frame too high: avgMs=$($clientFields.Avg) limit=$MaxAverageFrameMs")
            }
            if ($avgFailures.Count -gt 0) {
                $avgError = $avgFailures -join "; "
                if ($errorText) {
                    $errorText = "$errorText; $avgError"
                } else {
                    $errorText = $avgError
                }
            }
        }
        $rollbackIntegrityError = Get-RollbackIntegrityError -Paths @($hostStdout, $clientStdout)
        if ($rollbackIntegrityError) {
            if ($errorText) {
                $errorText = "$errorText; rollback integrity failure: $rollbackIntegrityError"
            } else {
                $errorText = "rollback integrity failure: $rollbackIntegrityError"
            }
        }
        $status = if ($errorText) { "fail" } else { "pass" }
        if ($errorText -match "rollback integrity failure") { $status = "rollback-fail" }
        elseif ($errorText -match "gameplay mismatch") { $status = "mismatch" }
        elseif ($errorText -match "active frame|avg frame|rollback frame|over33ms|consecutive slow") { $status = "perf-fail" }
        elseif ($errorText -match "stalled|timeout|timed out|missing frame limit") { $status = "stall" }

        $summary.Add([pscustomobject]@{
            Candidate = $candidateName
            Route = $routeName
            Status = $status
            HostAvgMs = $hostFields.Avg
            ClientAvgMs = $clientFields.Avg
            HostMaxMs = $hostFields.Max
            ClientMaxMs = $clientFields.Max
            HostOver33 = $hostFields.Over33
            ClientOver33 = $clientFields.Over33
            HostPerfSpikes = $hostPerfSpikes.Total
            ClientPerfSpikes = $clientPerfSpikes.Total
            HostRollbackSpikes = $hostPerfSpikes.Rollback
            ClientRollbackSpikes = $clientPerfSpikes.Rollback
            HostNonRollbackSpikes = $hostPerfSpikes.NonRollback
            ClientNonRollbackSpikes = $clientPerfSpikes.NonRollback
            HostRollbackMaxMs = $hostPerfSpikes.RollbackMaxMs
            ClientRollbackMaxMs = $clientPerfSpikes.RollbackMaxMs
            HostNonRollbackMaxMs = $hostPerfSpikes.NonRollbackMaxMs
            ClientNonRollbackMaxMs = $clientPerfSpikes.NonRollbackMaxMs
            HostScratchSpikes = $hostScratchSpikes.Count
            ClientScratchSpikes = $clientScratchSpikes.Count
            HostScratchMaxMs = $hostScratchSpikes.MaxMs
            ClientScratchMaxMs = $clientScratchSpikes.MaxMs
            HostThrottleMaxMs = $hostScratchSpikes.ThrottleMaxMs
            ClientThrottleMaxMs = $clientScratchSpikes.ThrottleMaxMs
            HostRemoteWaitMaxMs = $hostScratchSpikes.RemoteWaitMaxMs
            ClientRemoteWaitMaxMs = $clientScratchSpikes.RemoteWaitMaxMs
            HostScratchWriteMaxMs = $hostScratchSpikes.WriteMaxMs
            ClientScratchWriteMaxMs = $clientScratchSpikes.WriteMaxMs
            HostSaveAvgUs = $hostRollbackStats.SaveAvgUs
            ClientSaveAvgUs = $clientRollbackStats.SaveAvgUs
            HostSaveMaxUs = $hostRollbackStats.SaveMaxUs
            ClientSaveMaxUs = $clientRollbackStats.SaveMaxUs
            HostRestoreAvgUs = $hostRollbackStats.RestoreAvgUs
            ClientRestoreAvgUs = $clientRollbackStats.RestoreAvgUs
            HostRestoreMaxUs = $hostRollbackStats.RestoreMaxUs
            ClientRestoreMaxUs = $clientRollbackStats.RestoreMaxUs
            HostResimRunAvgUs = $hostRollbackStats.ResimRunAvgUs
            ClientResimRunAvgUs = $clientRollbackStats.ResimRunAvgUs
            HostResimRunMaxUs = $hostRollbackStats.ResimRunMaxUs
            ClientResimRunMaxUs = $clientRollbackStats.ResimRunMaxUs
            HostResimTotalAvgUs = $hostRollbackStats.ResimTotalAvgUs
            ClientResimTotalAvgUs = $clientRollbackStats.ResimTotalAvgUs
            HostResimTotalMaxUs = $hostRollbackStats.ResimTotalMaxUs
            ClientResimTotalMaxUs = $clientRollbackStats.ResimTotalMaxUs
            HostPredicted = $hostRollbackStats.Predicted
            ClientPredicted = $clientRollbackStats.Predicted
            HostPredictions = $hostRollbackStats.Predictions
            ClientPredictions = $clientRollbackStats.Predictions
            HostRestores = $hostRollbackStats.Restores
            ClientRestores = $clientRollbackStats.Restores
            HostResims = $hostRollbackStats.Resims
            ClientResims = $clientRollbackStats.Resims
            TransientMismatchCount = $transientFields.Count
            MaxTransientFrames = $transientFields.MaxFrames
            TransientMismatchFields = $transientFields.Fields
            LogDir = $caseRel
            Error = $errorText
        })
    }
}

$csvPath = Join-Path $runRoot "summary.csv"
$summary | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $csvPath
$summary | Format-Table Candidate, Route, Status, HostAvgMs, ClientAvgMs, HostMaxMs, ClientMaxMs, HostOver33, ClientOver33, HostRollbackSpikes, ClientRollbackSpikes, HostNonRollbackSpikes, ClientNonRollbackSpikes, TransientMismatchCount, MaxTransientFrames -AutoSize
Write-Host "summary: $csvPath"
