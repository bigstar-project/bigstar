param(
    [ValidateSet("neutral", "rule", "imitation")]
    [string]$Policy = "imitation",
    [int]$Frames = 2600,
    [int]$MvlStage = 0,
    [string]$MvlMatchSeed = "0x2f52869f",
    [string]$LogDir = "",
    [string]$Model = "logs\codex-cpp-imitation-parity-20260609\human-stage0-player1-runtime-model.json",
    [ValidateSet("MARIO", "LUIGI", "0", "1")]
    [string]$AIPlayer = "LUIGI",
    [double]$Threshold = 0.5,
    [int]$AIPlayLogInterval = 30,
    [int]$AIPlayLogMaxObjects = 96,
    [string]$HostInputScript = "tests\nsmb_us_direct_mvl_minimal_bootstrap.inputs",
    [string]$ClientInputScript = "tests\nsmb_us_direct_mvl_minimal_bootstrap.inputs",
    [switch]$Trace,
    [int]$TraceInterval = 120,
    [int]$ImitationInferInterval = 16,
    [int]$ImitationNeutralHoldFrames = 8,
    [switch]$DisableImitationHazardGuard,
    [int]$ImitationHazardGuardHorizontalRange = -1,
    [int]$ImitationHazardGuardVerticalRange = -1,
    [int]$ImitationHazardGuardCloseRange = -1,
    [switch]$SkipGameStateComparison,
    [int]$ScreenshotInterval = 0,
    [switch]$SoftwareRenderer,
    [switch]$AllowJit,
    [switch]$ForcePlayerPowerups,
    [int]$ForcePlayerPowerupsStartFrame = 0,
    [int]$ForcePlayerPowerupsEndFrame = 0,
    [int]$ForcePlayerPowerup0 = 0,
    [int]$ForcePlayerPowerup1 = 0,
    [switch]$ForcePlayerStarCounters,
    [int]$ForcePlayerStarCountersStartFrame = 900,
    [int]$ForcePlayerStarCountersEndFrame = 1500,
    [int]$ForcePlayerBattleStars0 = 0,
    [int]$ForcePlayerBattleStars1 = 0,
    [int]$ForcePlayerDisplayedStars0 = 0,
    [int]$ForcePlayerDisplayedStars1 = 0,
    [int]$ForcePlayerCollectedStars0 = 0,
    [int]$ForcePlayerCollectedStars1 = 0,
    [int]$RuleAIAuditSampleLimit = 16,
    [int]$RuleAIAuditStuckRecords = 6,
    [int]$ImitationAuditSampleLimit = 16,
    [int]$ImitationAuditNeutralRecords = 3,
    [int]$ImitationAuditStuckRecords = 6
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if ($LogDir -eq "") {
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $LogDir = "logs\nsmb-mvl-ai-closed-loop-$Policy-$timestamp"
}

$logRoot = if ([System.IO.Path]::IsPathRooted($LogDir)) {
    $LogDir
} else {
    Join-Path $repoRoot $LogDir
}
$hostLog = Join-Path $logRoot "host"
$clientLog = Join-Path $logRoot "client"
$hostAIPlayLog = Join-Path $hostLog "ai-playlog.jsonl"
$clientAIPlayLog = Join-Path $clientLog "ai-playlog.jsonl"
$summaryPath = Join-Path $logRoot "closed-loop-eval.json"
$evalPlayer = if ($AIPlayer -eq "MARIO" -or $AIPlayer -eq "0") { 0 } else { 1 }

$savedEnv = @{}
$envNames = @(
    "MELONDS_NSML_RULE_AI",
    "MELONDS_NSML_RULE_AI_PLAYER",
    "MELONDS_NSML_RULE_AI_TRACE",
    "MELONDS_NSML_RULE_AI_TRACE_INTERVAL",
    "MELONDS_NSML_RULE_AI_HOST_ONLY",
    "MELONDS_NSML_RULE_AI_CLIENT_ONLY",
    "MELONDS_NSML_IMITATION_AI",
    "MELONDS_NSML_IMITATION_AI_MODEL",
    "MELONDS_NSML_IMITATION_AI_PLAYER",
    "MELONDS_NSML_IMITATION_AI_THRESHOLD",
    "MELONDS_NSML_IMITATION_AI_TRACE",
    "MELONDS_NSML_IMITATION_AI_TRACE_INTERVAL",
    "MELONDS_NSML_IMITATION_AI_INFER_INTERVAL",
    "MELONDS_NSML_IMITATION_AI_NEUTRAL_HOLD_FRAMES",
    "MELONDS_NSML_IMITATION_AI_HOST_ONLY",
    "MELONDS_NSML_IMITATION_AI_CLIENT_ONLY",
    "MELONDS_NSML_IMITATION_AI_HAZARD_GUARD",
    "MELONDS_NSML_IMITATION_AI_DISABLE_HAZARD_GUARD",
    "MELONDS_NSML_IMITATION_AI_HAZARD_GUARD_HORIZONTAL_RANGE",
    "MELONDS_NSML_IMITATION_AI_HAZARD_GUARD_VERTICAL_RANGE",
    "MELONDS_NSML_IMITATION_AI_HAZARD_GUARD_CLOSE_RANGE",
    "MELONDS_NSML_FORCE_PLAYER_POWERUPS",
    "MELONDS_NSML_FORCE_PLAYER_POWERUPS_START_FRAME",
    "MELONDS_NSML_FORCE_PLAYER_POWERUPS_END_FRAME",
    "MELONDS_NSML_FORCE_PLAYER_POWERUP0",
    "MELONDS_NSML_FORCE_PLAYER_POWERUP1"
)
foreach ($name in $envNames) {
    $savedEnv[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
}

function Restore-EvalEnv {
    foreach ($name in $envNames) {
        $value = $savedEnv[$name]
        if ($null -eq $value) {
            [Environment]::SetEnvironmentVariable($name, $null, "Process")
        } else {
            [Environment]::SetEnvironmentVariable($name, $value, "Process")
        }
    }
}

try {
    foreach ($name in $envNames) {
        Remove-Item "Env:\$name" -ErrorAction SilentlyContinue
    }

    if ($Policy -eq "rule") {
        $env:MELONDS_NSML_RULE_AI = "1"
        $env:MELONDS_NSML_RULE_AI_PLAYER = $AIPlayer
        if ($Trace) {
            $env:MELONDS_NSML_RULE_AI_TRACE = "1"
            $env:MELONDS_NSML_RULE_AI_TRACE_INTERVAL = "$TraceInterval"
        }
    } elseif ($Policy -eq "imitation") {
        $modelPath = if ([System.IO.Path]::IsPathRooted($Model)) {
            $Model
        } else {
            (Resolve-Path (Join-Path $repoRoot $Model)).Path
        }
        $env:MELONDS_NSML_IMITATION_AI = "1"
        $env:MELONDS_NSML_IMITATION_AI_MODEL = $modelPath
        $env:MELONDS_NSML_IMITATION_AI_PLAYER = $AIPlayer
        $env:MELONDS_NSML_IMITATION_AI_THRESHOLD = $Threshold.ToString([System.Globalization.CultureInfo]::InvariantCulture)
        $env:MELONDS_NSML_IMITATION_AI_INFER_INTERVAL = "$ImitationInferInterval"
        $env:MELONDS_NSML_IMITATION_AI_NEUTRAL_HOLD_FRAMES = "$ImitationNeutralHoldFrames"
        if ($DisableImitationHazardGuard) {
            $env:MELONDS_NSML_IMITATION_AI_HAZARD_GUARD = "0"
        }
        if ($ImitationHazardGuardHorizontalRange -ge 0) {
            $env:MELONDS_NSML_IMITATION_AI_HAZARD_GUARD_HORIZONTAL_RANGE = "$ImitationHazardGuardHorizontalRange"
        }
        if ($ImitationHazardGuardVerticalRange -ge 0) {
            $env:MELONDS_NSML_IMITATION_AI_HAZARD_GUARD_VERTICAL_RANGE = "$ImitationHazardGuardVerticalRange"
        }
        if ($ImitationHazardGuardCloseRange -ge 0) {
            $env:MELONDS_NSML_IMITATION_AI_HAZARD_GUARD_CLOSE_RANGE = "$ImitationHazardGuardCloseRange"
        }
        if ($Trace) {
            $env:MELONDS_NSML_IMITATION_AI_TRACE = "1"
            $env:MELONDS_NSML_IMITATION_AI_TRACE_INTERVAL = "$TraceInterval"
        }
    }
    if ($ForcePlayerPowerups) {
        $env:MELONDS_NSML_FORCE_PLAYER_POWERUPS = "1"
        $env:MELONDS_NSML_FORCE_PLAYER_POWERUPS_START_FRAME = "$ForcePlayerPowerupsStartFrame"
        $env:MELONDS_NSML_FORCE_PLAYER_POWERUPS_END_FRAME = "$ForcePlayerPowerupsEndFrame"
        $env:MELONDS_NSML_FORCE_PLAYER_POWERUP0 = "$ForcePlayerPowerup0"
        $env:MELONDS_NSML_FORCE_PLAYER_POWERUP1 = "$ForcePlayerPowerup1"
    }

    $smokeArgs = @(
        "-Frames", "$Frames",
        "-MvlStage", "$MvlStage",
        "-MvlMatchSeed", "$MvlMatchSeed",
        "-LogDir", $logRoot,
        "-HostInputScript", $HostInputScript,
        "-ClientInputScript", $ClientInputScript,
        "-HostAIPlayLog", $hostAIPlayLog,
        "-ClientAIPlayLog", $clientAIPlayLog,
        "-AIPlayLogInterval", "$AIPlayLogInterval",
        "-AIPlayLogMaxObjects", "$AIPlayLogMaxObjects",
        "-ScreenshotInterval", "$ScreenshotInterval",
        "-AllowRemoteInputTimeoutFallback",
        "-SkipMovementProbe"
    )
    if ($AllowJit) { $smokeArgs += "-AllowJit" }
    if ($SoftwareRenderer) { $smokeArgs += "-SoftwareRenderer" }
    if ($SkipGameStateComparison) { $smokeArgs += "-SkipGameStateComparison" }
    if ($ForcePlayerPowerups) {
        $smokeArgs += @(
            "-ForcePlayerPowerups",
            "-ForcePlayerPowerupsStartFrame", "$ForcePlayerPowerupsStartFrame",
            "-ForcePlayerPowerupsEndFrame", "$ForcePlayerPowerupsEndFrame",
            "-ForcePlayerPowerup0", "$ForcePlayerPowerup0",
            "-ForcePlayerPowerup1", "$ForcePlayerPowerup1"
        )
    }
    if ($ForcePlayerStarCounters) {
        $smokeArgs += @(
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

    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repoRoot "scripts\run-nsmb-mvl-split-local-input-smoke.ps1") @smokeArgs

    & python (Join-Path $repoRoot "scripts\nsmb_mvl_ai_evaluate_closed_loop.py") `
        $hostAIPlayLog `
        $clientAIPlayLog `
        --player $evalPlayer `
        --policy $Policy `
        --output $summaryPath `
        --min-gameplay-rows 1

    if ($Policy -eq "rule") {
        $hostAuditPath = Join-Path $hostLog "ruleai-audit.json"
        $clientAuditPath = Join-Path $clientLog "ruleai-audit.json"
        $hostAuditOutput = & python (Join-Path $repoRoot "scripts\nsmb_mvl_ai_audit_ruleai.py") `
            $hostAIPlayLog `
            --player $evalPlayer `
            --output $hostAuditPath `
            --sample-limit $RuleAIAuditSampleLimit `
            --stuck-records $RuleAIAuditStuckRecords
        if ($LASTEXITCODE -ne 0) {
            $hostAuditOutput | Write-Host
            throw "RuleAI host audit failed with exit code $LASTEXITCODE"
        }
        $clientAuditOutput = & python (Join-Path $repoRoot "scripts\nsmb_mvl_ai_audit_ruleai.py") `
            $clientAIPlayLog `
            --player $evalPlayer `
            --output $clientAuditPath `
            --sample-limit $RuleAIAuditSampleLimit `
            --stuck-records $RuleAIAuditStuckRecords
        if ($LASTEXITCODE -ne 0) {
            $clientAuditOutput | Write-Host
            throw "RuleAI client audit failed with exit code $LASTEXITCODE"
        }
        Write-Host "ruleAIAuditHost=$hostAuditPath"
        Write-Host "ruleAIAuditClient=$clientAuditPath"
    } elseif ($Policy -eq "imitation") {
        $hostAuditPath = Join-Path $hostLog "imitation-audit.json"
        $clientAuditPath = Join-Path $clientLog "imitation-audit.json"
        $hostAuditOutput = & python (Join-Path $repoRoot "scripts\nsmb_mvl_ai_audit_imitation.py") `
            $hostAIPlayLog `
            --player $evalPlayer `
            --output $hostAuditPath `
            --sample-limit $ImitationAuditSampleLimit `
            --neutral-records $ImitationAuditNeutralRecords `
            --stuck-records $ImitationAuditStuckRecords
        if ($LASTEXITCODE -ne 0) {
            $hostAuditOutput | Write-Host
            throw "ImitationAI host audit failed with exit code $LASTEXITCODE"
        }
        $clientAuditOutput = & python (Join-Path $repoRoot "scripts\nsmb_mvl_ai_audit_imitation.py") `
            $clientAIPlayLog `
            --player $evalPlayer `
            --output $clientAuditPath `
            --sample-limit $ImitationAuditSampleLimit `
            --neutral-records $ImitationAuditNeutralRecords `
            --stuck-records $ImitationAuditStuckRecords
        if ($LASTEXITCODE -ne 0) {
            $clientAuditOutput | Write-Host
            throw "ImitationAI client audit failed with exit code $LASTEXITCODE"
        }
        Write-Host "imitationAuditHost=$hostAuditPath"
        Write-Host "imitationAuditClient=$clientAuditPath"
    }

    Write-Host "closedLoopEval=$summaryPath"
} finally {
    Restore-EvalEnv
}
