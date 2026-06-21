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
        Extra = @{ IgnoreSpeculativeInputFields = $true; RollbackSettleFrames = 30 }
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
        Extra = @{ IgnoreSpeculativeInputFields = $true; RollbackSettleFrames = 30 }
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
    "MELONDS_NSML_FIXED_FRAME_SLEEP"
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
        $rollbackResimulate = $true
        if ($candidateDef.PSObject.Properties.Name -contains "RollbackResimulate") {
            $rollbackResimulate = [bool]$candidateDef.RollbackResimulate
        }
        $rollbackPredictOnly = $false
        if ($candidateDef.PSObject.Properties.Name -contains "RollbackPredictOnly") {
            $rollbackPredictOnly = [bool]$candidateDef.RollbackPredictOnly
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
            MvlMatchSeed = "practical-$candidateName-$routeName-$runStamp"
        }
        foreach ($key in $routeDef.Extra.Keys) {
            $params[$key] = $routeDef.Extra[$key]
        }
        if ($rollbackSkipRenderDuringResim) {
            $params.RollbackSkipRenderDuringResim = $true
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
            LogDir = $caseRel
            Error = $errorText
        })
    }
}

$csvPath = Join-Path $runRoot "summary.csv"
$summary | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $csvPath
$summary | Format-Table Candidate, Route, Status, HostAvgMs, ClientAvgMs, HostMaxMs, ClientMaxMs, HostOver33, ClientOver33 -AutoSize
Write-Host "summary: $csvPath"
