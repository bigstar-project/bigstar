param(
    [string[]]$Policies = @("rule"),
    [string[]]$Seeds = @("0x2f52869f"),
    [string[]]$StarCases = @("even:0:0", "p0-losing:0:5", "p1-losing:5:0"),
    [int]$Frames = 2600,
    [int]$MvlStage = 0,
    [string]$LogDir = "",
    [ValidateSet("MARIO", "LUIGI", "0", "1")]
    [string]$AIPlayer = "LUIGI",
    [int]$AIPlayLogInterval = 1,
    [int]$AIPlayLogMaxObjects = 128,
    [switch]$AllowJit,
    [switch]$SoftwareRenderer,
    [int]$ScreenshotInterval = 0,
    [int]$ForceStarStartFrame = 900,
    [int]$ForceStarEndFrame = 1500,
    [int]$SvgPerCluster = 2,
    [ValidateSet(0, 1)]
    [int]$SvgPlayer = 1,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$Policies = @($Policies | ForEach-Object { $_ -split "," } | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" })
$Seeds = @($Seeds | ForEach-Object { $_ -split "[,\s]+" } | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" })
$StarCases = @($StarCases | ForEach-Object { $_ -split "," } | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" })

if ($LogDir -eq "") {
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $LogDir = "logs\nsmb-mvl-item-audit-sweep-$timestamp"
}

$logRoot = if ([System.IO.Path]::IsPathRooted($LogDir)) {
    $LogDir
} else {
    Join-Path $repoRoot $LogDir
}
New-Item -ItemType Directory -Force -Path $logRoot | Out-Null

function Convert-SafeName([string]$Value) {
    return ($Value -replace '[^0-9A-Za-z_-]', '_')
}

function Parse-StarCase([string]$Value) {
    $parts = $Value -split ":"
    if ($parts.Count -ne 3) {
        throw "invalid StarCase '$Value'; expected name:p0Stars:p1Stars"
    }
    return [ordered]@{
        name = $parts[0]
        p0 = [int]$parts[1]
        p1 = [int]$parts[2]
    }
}

$runs = New-Object System.Collections.Generic.List[object]
foreach ($seed in $Seeds) {
    foreach ($policy in $Policies) {
        foreach ($caseText in $StarCases) {
            $case = Parse-StarCase $caseText
            $seedName = Convert-SafeName $seed
            $caseName = Convert-SafeName $case.name
            $policyName = Convert-SafeName $policy
            $runDir = Join-Path $logRoot (Join-Path "seed-$seedName" (Join-Path $policyName $caseName))
            $evalArgs = @(
                "-Policy", $policy,
                "-Frames", "$Frames",
                "-MvlStage", "$MvlStage",
                "-MvlMatchSeed", $seed,
                "-LogDir", $runDir,
                "-AIPlayer", $AIPlayer,
                "-AIPlayLogInterval", "$AIPlayLogInterval",
                "-AIPlayLogMaxObjects", "$AIPlayLogMaxObjects",
                "-ForcePlayerStarCounters",
                "-ForcePlayerStarCountersStartFrame", "$ForceStarStartFrame",
                "-ForcePlayerStarCountersEndFrame", "$ForceStarEndFrame",
                "-ForcePlayerBattleStars0", "$($case.p0)",
                "-ForcePlayerBattleStars1", "$($case.p1)",
                "-ForcePlayerDisplayedStars0", "$($case.p0)",
                "-ForcePlayerDisplayedStars1", "$($case.p1)",
                "-ForcePlayerCollectedStars0", "$($case.p0)",
                "-ForcePlayerCollectedStars1", "$($case.p1)"
            )
            if ($AllowJit) { $evalArgs += "-AllowJit" }
            if ($SoftwareRenderer) { $evalArgs += "-SoftwareRenderer" }
            if ($ScreenshotInterval -gt 0) {
                $evalArgs += @("-ScreenshotInterval", "$ScreenshotInterval")
            }

            $hostPlaylog = Join-Path $runDir "host\ai-playlog.jsonl"
            $clientPlaylog = Join-Path $runDir "client\ai-playlog.jsonl"
            $auditJson = Join-Path $runDir "item-audit.json"
            $auditCsv = Join-Path $runDir "item-audit-clusters.csv"
            $svgDir = Join-Path $runDir "item-audit-svgs"
            $auditArgs = @(
                $hostPlaylog,
                $clientPlaylog,
                "--output", $auditJson,
                "--csv", $auditCsv,
                "--svg-dir", $svgDir,
                "--svg-per-cluster", "$SvgPerCluster",
                "--svg-player", "$SvgPlayer"
            )

            $runs.Add([ordered]@{
                policy = $policy
                seed = $seed
                starCase = $case.name
                p0Stars = $case.p0
                p1Stars = $case.p1
                logDir = $runDir
                evalArgs = $evalArgs
                itemAudit = $auditJson
                itemAuditCsv = $auditCsv
                itemAuditSvgDir = $svgDir
            }) | Out-Null

            if ($DryRun) {
                continue
            }

            & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repoRoot "scripts\run-nsmb-mvl-ai-closed-loop-eval.ps1") @evalArgs
            if (-not (Test-Path -LiteralPath $hostPlaylog) -or -not (Test-Path -LiteralPath $clientPlaylog)) {
                throw "AI play logs missing for item audit: $runDir"
            }
            & python (Join-Path $repoRoot "scripts\nsmb_mvl_ai_audit_items.py") @auditArgs
        }
    }
}

$summary = [ordered]@{
    schema = "nsmb_mvl_item_audit_sweep_v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    frames = $Frames
    stage = $MvlStage
    policies = $Policies
    seeds = $Seeds
    starCases = $StarCases
    forceStarStartFrame = $ForceStarStartFrame
    forceStarEndFrame = $ForceStarEndFrame
    dryRun = [bool]$DryRun
    runCount = $runs.Count
    runs = $runs
}
$summaryPath = Join-Path $logRoot "item-audit-sweep.json"
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
Write-Host "itemAuditSweep=$summaryPath"
