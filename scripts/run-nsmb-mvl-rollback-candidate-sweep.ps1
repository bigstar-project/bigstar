param(
    [int]$Frames = 2600,
    [int]$WaitTimeoutMs = 240000,
    [int]$StallTimeoutMs = 5000,
    [int]$StallStartFrame = 990,
    [string[]]$Candidate = @(),
    [string]$HostInputScript = "tests\nsmb_us_direct_mvl_stress_host_move_jump_dash.inputs",
    [string]$ClientInputScript = "tests\nsmb_us_direct_mvl_stress_client_move_jump_dash.inputs",
    [string]$LogRoot = "logs\nsmb-mvl-rollback-candidate-sweep",
    [double]$SlowFrameThresholdMs = 33.0,
    [double]$MaxActiveFrameMs = 1000.0,
    [int]$MaxConsecutiveSlowFrames = 120,
    [double]$MaxRollbackFrameMs = 0.0,
    [switch]$NetworkPumpThread,
    [int]$NetworkPumpSleepUs = 250,
    [int]$RollbackPredictionProbeModulo = 0,
    [int]$RollbackPredictionProbeOffset = 0,
    [int]$RollbackPredictionProbeLimit = -1,
    [int]$RollbackPredictionProbeStartFrame = 0,
    [int]$RollbackPredictionProbeEndFrame = 0,
    [string]$RollbackPredictionProbeKeyMask = "",
    [int]$RollbackInputWaitUs = 0,
    [switch]$NoGameStateComparison,
    [switch]$SkipMovementProbe,
    [switch]$InputNetplayTrace
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$smokeScript = Join-Path $PSScriptRoot "run-nsmb-mvl-split-local-input-smoke.ps1"
$runStamp = Get-Date -Format "yyyyMMdd-HHmmss"
$runRoot = Join-Path $repoRoot (Join-Path $LogRoot $runStamp)
New-Item -ItemType Directory -Force $runRoot | Out-Null

$envKeys = @(
    "MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL",
    "MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE",
    "MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES",
    "MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES",
    "MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE",
    "MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES",
    "MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES",
    "MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL",
    "MELONDS_NSML_ROLLBACK_NSMB_SCAN_INTERVAL",
    "MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS",
    "MELONDS_NSML_ROLLBACK_MAX_RESIM_FRAMES",
    "MELONDS_NSML_ROLLBACK_INPUT_WAIT_US",
    "MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET",
    "MELONDS_NSML_ROLLBACK_RESIM_SKIP_RENDER",
    "MELONDS_NSML_NET_PUMP_THREAD",
    "MELONDS_NSML_NET_PUMP_SLEEP_US",
    "MELONDS_NSML_ROLLBACK_CORE_SKIP_MASK",
    "MELONDS_NSML_FIXED_FRAME_SLEEP",
    "MELONDS_NSML_FPS_SPIKE_THRESHOLD_MS",
    "MELONDS_NSML_FPS_SPIKE_TRACE"
)

function Set-CandidateEnvironment {
    param([hashtable]$Values)

    foreach ($key in $envKeys) {
        Remove-Item "Env:\$key" -ErrorAction SilentlyContinue
    }
    foreach ($key in $Values.Keys) {
        Set-Item "Env:\$key" "$($Values[$key])"
    }
}

function Get-CandidateStatus {
    param(
        [string]$RunLog,
        [string]$CandidateLog,
        [string]$ErrorText
    )

    $combined = "$ErrorText`n"
    if (Test-Path $RunLog) {
        $combined += (Get-Content $RunLog -Raw -ErrorAction SilentlyContinue)
    }
    if (Test-Path $CandidateLog) {
        $matches = Select-String -Path (Join-Path $CandidateLog "*\*.txt") -Pattern "stalled|timed out|prefetch abort|data abort|gameplay mismatch|active frame exceeded|active frame spike too high|over25ms exceeded|over33ms exceeded|consecutive slow frames too high|rollback frame spike too high" -ErrorAction SilentlyContinue
        foreach ($match in $matches) {
            $combined += "`n$($match.Line)"
        }
    }

    if ($combined -match "stalled") { return "stalled" }
    if ($combined -match "prefetch abort|data abort") { return "abort" }
    if ($combined -match "gameplay mismatch") { return "mismatch" }
    if ($combined -match "timed out|missing frame limit") { return "timeout" }
    if ($combined -match "active frame exceeded|active frame spike too high|over25ms exceeded|over33ms exceeded|consecutive slow frames too high|rollback frame spike too high") { return "perf-fail" }
    if ($ErrorText) { return "failed" }
    return "passed"
}

function Get-LastMatchingLine {
    param(
        [string]$CandidateLog,
        [string]$Pattern
    )

    if (-not (Test-Path $CandidateLog)) {
        return ""
    }
    $matches = Select-String -Path (Join-Path $CandidateLog "*\*.stdout.txt") -Pattern $Pattern -ErrorAction SilentlyContinue
    if (-not $matches) {
        return ""
    }
    return $matches[-1].Line
}

$candidates = @(
    [pscustomobject]@{
        Name = "coredelta-page256-k30"
        Backend = "coredelta"
        Env = @{
            MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "30"
            MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
            MELONDS_NSML_FIXED_FRAME_SLEEP = "1"
            MELONDS_NSML_FPS_SPIKE_THRESHOLD_MS = "25"
            MELONDS_NSML_FPS_SPIKE_TRACE = "1"
        }
    },
    [pscustomobject]@{
        Name = "nsmbtinycore-expanded"
        Backend = "nsmbtinycore"
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_SCAN_INTERVAL = "30"
            MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS = "0x200"
            MELONDS_NSML_FIXED_FRAME_SLEEP = "1"
            MELONDS_NSML_FPS_SPIKE_THRESHOLD_MS = "25"
            MELONDS_NSML_FPS_SPIKE_TRACE = "1"
        }
    },
    [pscustomobject]@{
        Name = "nsmbtinycore-proclist-arena-noheap"
        Backend = "nsmbtinycore"
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
            MELONDS_NSML_ROLLBACK_NSMB_SCAN_INTERVAL = "30"
            MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS = "0x200"
            MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
            MELONDS_NSML_ROLLBACK_RESIM_SKIP_RENDER = "1"
            MELONDS_NSML_FIXED_FRAME_SLEEP = "1"
            MELONDS_NSML_FPS_SPIKE_THRESHOLD_MS = "25"
            MELONDS_NSML_FPS_SPIKE_TRACE = "1"
        }
    },
    [pscustomobject]@{
        Name = "nsmbtinycore-proclist-heap900"
        Backend = "nsmbtinycore"
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL = "900"
            MELONDS_NSML_ROLLBACK_NSMB_SCAN_INTERVAL = "30"
            MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS = "0x200"
            MELONDS_NSML_FIXED_FRAME_SLEEP = "1"
            MELONDS_NSML_FPS_SPIKE_THRESHOLD_MS = "25"
            MELONDS_NSML_FPS_SPIKE_TRACE = "1"
        }
    },
    [pscustomobject]@{
        Name = "nsmbcoreranges-proclist-heap900"
        Backend = "nsmbcoreranges"
        Env = @{
            MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "1"
            MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL = "900"
            MELONDS_NSML_ROLLBACK_NSMB_SCAN_INTERVAL = "30"
            MELONDS_NSML_FIXED_FRAME_SLEEP = "1"
            MELONDS_NSML_FPS_SPIKE_THRESHOLD_MS = "25"
            MELONDS_NSML_FPS_SPIKE_TRACE = "1"
        }
    }
)

if ($Candidate.Count -gt 0) {
    $wanted = @{}
    foreach ($name in $Candidate) {
        $wanted[$name] = $true
    }
    $candidates = @($candidates | Where-Object { $wanted.ContainsKey($_.Name) })
}

$summary = @()
foreach ($item in $candidates) {
    $candidateLogRel = Join-Path (Join-Path $LogRoot $runStamp) $item.Name
    $candidateLog = Join-Path $repoRoot $candidateLogRel
    $runLog = Join-Path $runRoot "$($item.Name).run.txt"
    Write-Host "running rollback candidate: $($item.Name)"

    Set-CandidateEnvironment -Values $item.Env

    $candidateParams = @{
        Frames = $Frames
        WaitTimeoutMs = $WaitTimeoutMs
        StallTimeoutMs = $StallTimeoutMs
        StallStartFrame = $StallStartFrame
        HostInputScript = $HostInputScript
        ClientInputScript = $ClientInputScript
        Rollback = $true
        RollbackBackend = $item.Backend
        RollbackWindow = 64
        RollbackCheckpointInterval = 8
        RollbackResimulate = $true
        InputDelayFrames = 0
        InputMaxFrameLead = 8
        AllowJit = $true
        RollbackSettleFrames = 8
        MaxActiveFrameMs = $MaxActiveFrameMs
        SlowFrameThresholdMs = $SlowFrameThresholdMs
        MaxConsecutiveSlowFrames = $MaxConsecutiveSlowFrames
        LogDir = $candidateLogRel
    }
    if ($item.Backend -eq "nsmbtinycore") {
        $candidateParams.RollbackCheckpointInterval = 1
        $candidateParams.InputMaxFrameLead = 1
    }
    if ($MaxRollbackFrameMs -gt 0.0) {
        $candidateParams.MaxRollbackFrameMs = $MaxRollbackFrameMs
    }
    if ($RollbackInputWaitUs -gt 0) {
        $candidateParams.RollbackInputWaitUs = $RollbackInputWaitUs
    }
    if ($NetworkPumpThread) {
        $candidateParams.NetworkPumpThread = $true
        $candidateParams.NetworkPumpSleepUs = $NetworkPumpSleepUs
    }
    if ($RollbackPredictionProbeModulo -gt 0) {
        $candidateParams.RollbackPredictionProbeModulo = $RollbackPredictionProbeModulo
        $candidateParams.RollbackPredictionProbeOffset = $RollbackPredictionProbeOffset
        $candidateParams.RollbackPredictionProbeLimit = $RollbackPredictionProbeLimit
        $candidateParams.RollbackPredictionProbeStartFrame = $RollbackPredictionProbeStartFrame
        $candidateParams.RollbackPredictionProbeEndFrame = $RollbackPredictionProbeEndFrame
        if ($RollbackPredictionProbeKeyMask -ne "") {
            $candidateParams.RollbackPredictionProbeKeyMask = $RollbackPredictionProbeKeyMask
        }
    }
    if ($InputNetplayTrace) {
        $candidateParams.InputNetplayTrace = $true
    }
    if ($NoGameStateComparison) {
        $candidateParams.NoGameStateTrace = $true
        $candidateParams.SkipGameStateComparison = $true
    }
    if ($SkipMovementProbe) {
        $candidateParams.SkipMovementProbe = $true
    }

    $errorText = ""
    try {
        & $smokeScript @candidateParams *> $runLog
    } catch {
        $errorText = $_.Exception.Message
        Add-Content -Path $runLog -Value $errorText
    }

    $status = Get-CandidateStatus -RunLog $runLog -CandidateLog $candidateLog -ErrorText $errorText
    $rollbackLine = Get-LastMatchingLine -CandidateLog $candidateLog -Pattern "NSMB Rollback: frame="
    $timingLine = Get-LastMatchingLine -CandidateLog $candidateLog -Pattern "NSMB Test: active frame timing"

    $summary += [pscustomobject]@{
        Name = $item.Name
        Backend = $item.Backend
        Status = $status
        LogDir = $candidateLogRel
        Rollback = $rollbackLine
        ActiveTiming = $timingLine
        Error = $errorText
    }
}

$csvPath = Join-Path $runRoot "summary.csv"
$summary | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $csvPath
$summary | Format-Table Name, Backend, Status, LogDir -AutoSize
Write-Host "summary: $csvPath"
