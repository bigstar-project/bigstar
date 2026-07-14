param(
    [string[]]$Policies = @("neutral", "rule", "imitation"),
    [string[]]$Seeds = @("0x2f52869f"),
    [int]$Frames = 2600,
    [int]$MvlStage = 0,
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
    [switch]$DisableImitationHazardGuard,
    [switch]$SkipGameStateComparison,
    [switch]$AllowJit,
    [switch]$ForcePlayerPowerups,
    [int]$ForcePlayerPowerupsStartFrame = 0,
    [int]$ForcePlayerPowerupsEndFrame = 0,
    [int]$ForcePlayerPowerup0 = 0,
    [int]$ForcePlayerPowerup1 = 0
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$validPolicies = @("neutral", "rule", "imitation")
$Policies = @($Policies | ForEach-Object { $_ -split "," } | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" })
$Seeds = @($Seeds | ForEach-Object { $_ -split "[,\s]+" } | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" })
foreach ($policy in $Policies) {
    if ($validPolicies -notcontains $policy) {
        throw "invalid policy '$policy'; expected one of: $($validPolicies -join ', ')"
    }
}

if ($LogDir -eq "") {
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $LogDir = "logs\nsmb-mvl-ai-closed-loop-batch-$timestamp"
}

$logRoot = if ([System.IO.Path]::IsPathRooted($LogDir)) {
    $LogDir
} else {
    Join-Path $repoRoot $LogDir
}
New-Item -ItemType Directory -Force -Path $logRoot | Out-Null

$reports = New-Object System.Collections.Generic.List[string]
$runs = New-Object System.Collections.Generic.List[object]

foreach ($seed in $Seeds) {
    $seedName = ($seed -replace '[^0-9A-Za-z_-]', '_')
    foreach ($policy in $Policies) {
        $runDir = Join-Path $logRoot (Join-Path "seed-$seedName" $policy)
        $args = @(
            "-Policy", $policy,
            "-Frames", "$Frames",
            "-MvlStage", "$MvlStage",
            "-MvlMatchSeed", $seed,
            "-LogDir", $runDir,
            "-Model", $Model,
            "-AIPlayer", $AIPlayer,
            "-Threshold", $Threshold.ToString([System.Globalization.CultureInfo]::InvariantCulture),
            "-AIPlayLogInterval", "$AIPlayLogInterval",
            "-AIPlayLogMaxObjects", "$AIPlayLogMaxObjects",
            "-HostInputScript", $HostInputScript,
            "-ClientInputScript", $ClientInputScript
        )
        if ($Trace) {
            $args += @("-Trace", "-TraceInterval", "$TraceInterval")
        }
        if ($DisableImitationHazardGuard) {
            $args += "-DisableImitationHazardGuard"
        }
        if ($SkipGameStateComparison) {
            $args += "-SkipGameStateComparison"
        }
        if ($AllowJit) {
            $args += "-AllowJit"
        }
        if ($ForcePlayerPowerups) {
            $args += @(
                "-ForcePlayerPowerups",
                "-ForcePlayerPowerupsStartFrame", "$ForcePlayerPowerupsStartFrame",
                "-ForcePlayerPowerupsEndFrame", "$ForcePlayerPowerupsEndFrame",
                "-ForcePlayerPowerup0", "$ForcePlayerPowerup0",
                "-ForcePlayerPowerup1", "$ForcePlayerPowerup1"
            )
        }

        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repoRoot "scripts\run-nsmb-mvl-ai-closed-loop-eval.ps1") @args

        $reportPath = Join-Path $runDir "closed-loop-eval.json"
        if (-not (Test-Path -LiteralPath $reportPath)) {
            throw "closed loop report was not generated: $reportPath"
        }
        $reports.Add($reportPath) | Out-Null
        $runs.Add([ordered]@{
            policy = $policy
            seed = $seed
            logDir = $runDir
            report = $reportPath
        }) | Out-Null
    }
}

$compareJson = Join-Path $logRoot "closed-loop-compare.json"
$compareCsv = Join-Path $logRoot "closed-loop-compare.csv"
& python (Join-Path $repoRoot "scripts\nsmb_mvl_ai_compare_closed_loop.py") @reports --output-json $compareJson --output-csv $compareCsv

$summary = [ordered]@{
    schema = "nsmb_mvl_ai_closed_loop_batch_v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    frames = $Frames
    stage = $MvlStage
    policies = $Policies
    seeds = $Seeds
    runCount = $runs.Count
    runs = $runs
    compareJson = $compareJson
    compareCsv = $compareCsv
}
$summaryPath = Join-Path $logRoot "closed-loop-batch.json"
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
Write-Host "closedLoopBatch=$summaryPath"
Write-Host "closedLoopCompareJson=$compareJson"
Write-Host "closedLoopCompareCsv=$compareCsv"
