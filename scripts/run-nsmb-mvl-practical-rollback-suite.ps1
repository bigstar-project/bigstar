param(
    [string[]]$Candidate = @("tinycorepreimage-delay6-lead999-rbwait3000-maxresim2-bundle8"),
    [string[]]$Route = @("stocktouch", "chaos", "death", "contact", "dualstresslong"),
    [string]$LogRoot = "logs\nsmb-mvl-practical-rollback-suite",
    [int]$WaitTimeoutMs = 720000,
    [int]$StallTimeoutMs = 5000,
    [int]$FrameHeartbeatInterval = 30,
    [int]$InputSendDelayFrames = 0,
    [int]$InputSendJitterFrames = 0,
    [switch]$InputUnreliable,
    [switch]$NoGameStateTrace,
    [switch]$StateSync,
    [double]$MaxActiveFrameMs = 90.0,
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
    "MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE",
    "MELONDS_NSML_ROLLBACK_PRE_PUMP_BEFORE_RESIM",
    "MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES",
    "MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE",
    "MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES",
    "MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES",
    "MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL",
    "MELONDS_NSML_ROLLBACK_NSMB_PLAYER_OBJECT_LENGTH",
    "MELONDS_NSML_ROLLBACK_NSMB_MOVING_HAZARD_OBJECT_LENGTH",
    "MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT_LIFECYCLE",
    "MELONDS_NSML_WORLD_STATE_PRUNE_EXTRA_ACTOR_SNAPSHOT",
    "MELONDS_NSML_WORLD_STATE_MOVING_HAZARD_MAX_PREDICT_FRAMES",
    "MELONDS_NSML_WORLD_STATE_MOVING_HAZARD_MAX_PREDICT_END_OFFSET",
    "MELONDS_NSML_WORLD_STATE_SPAWN_MOVING_HAZARD",
    "MELONDS_NSML_WORLD_STATE_CLEAR_MOVING_HAZARD_LINK_FIELDS",
    "MELONDS_NSML_FIXED_FRAME_SLEEP",
    "MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM",
    "MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET",
    "MELONDS_NSML_PLAYER_STATE_FRESH_WAIT_US",
    "MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES",
    "MELONDS_NSML_PLAYER_STATE_MAX_STALE_COUNTER_FRAMES",
    "MELONDS_NSML_PLAYER_STATE_MAX_STALE_TRANSFORM_FRAMES",
    "MELONDS_NSML_INPUT_TRACE_INTERVAL"
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

        $params = @{
            Frames = $routeDef.Frames
            WaitTimeoutMs = $WaitTimeoutMs
            StallTimeoutMs = $StallTimeoutMs
            LogDir = $caseRel
            HostInputScript = $hostInput
            ClientInputScript = $clientInput
            Rollback = $true
            RollbackBackend = $candidateDef.Backend
            RollbackTinyCoreFlags = $rollbackTinyCoreFlags
            RollbackWindow = $candidateDef.RollbackWindow
            RollbackCheckpointInterval = $candidateDef.RollbackCheckpointInterval
            RollbackResimulate = $rollbackResimulate
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
            AllowJit = $true
            MaxActiveFrameMs = $MaxActiveFrameMs
            MaxActiveFrameOver33ms = $MaxActiveFrameOver33ms
            MaxConsecutiveSlowFrames = $MaxConsecutiveSlowFrames
            SlowFrameThresholdMs = $SlowFrameThresholdMs
            GameStateCompareStartFrame = 990
            MvlMatchSeed = "practical-$candidateName-$routeName-$runStamp"
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
        $transientFields = Get-TransientMismatchFields -Path $runLog
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
        elseif ($errorText -match "active frame|over33ms|consecutive slow") { $status = "perf-fail" }
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
$summary | Format-Table Candidate, Route, Status, HostAvgMs, ClientAvgMs, HostMaxMs, ClientMaxMs, HostOver33, ClientOver33, TransientMismatchCount, MaxTransientFrames -AutoSize
Write-Host "summary: $csvPath"
