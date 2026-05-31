param(
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$Rom = "roms\nsmb-us.nds",
    [string]$InputScript = "tests\nsmb_us_direct_mvl_minimal_bootstrap.inputs",
    [string]$LogDir = "logs\nsmb-mvl-bigstar-thresholds",
    [int]$Frames = 2500,
    [switch]$StopOnFirstFailure
)

$ErrorActionPreference = "Stop"

$cases = @(
    @{ Name = "bigstar3-force2-noresult"; BigStars = 3; Stars = 2; Require = "NoResult" },
    @{ Name = "bigstar3-force3-result"; BigStars = 3; Stars = 3; Require = "Result" },
    @{ Name = "bigstar5-force3-noresult"; BigStars = 5; Stars = 3; Require = "NoResult" },
    @{ Name = "bigstar5-force5-result"; BigStars = 5; Stars = 5; Require = "Result" },
    @{ Name = "bigstar10-force9-noresult"; BigStars = 10; Stars = 9; Require = "NoResult" },
    @{ Name = "bigstar10-force10-result"; BigStars = 10; Stars = 10; Require = "Result" }
)

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$results = @()

foreach ($case in $cases) {
    $caseLogDir = Join-Path $LogDir $case.Name
    $params = @{
        RunRole = "both"
        Exe = $Exe
        Rom = $Rom
        InputScript = $InputScript
        Frames = $Frames
        ScreenshotInterval = 0
        NoHashLog = $true
        NoLanMP = $true
        InputNetplay = $true
        InputDelayFrames = 4
        InputMaxFrameLead = 4
        PacketBridgeJitHelperPatch = $true
        PacketBridgeJitHelperPatchFrame = 840
        PacketBridgeStartFrame = 840
        WaitForPeerAtNetplayStart = $true
        AllowJit = $true
        InputUnreliable = $true
        InputBundleHistory = 8
        GenerateMvlConfiguredRoms = $true
        MvlCourseMode = "random"
        MvlMatchSeed = "0x00000000"
        MvlWins = 1
        MvlBigStars = $case.BigStars
        MvlLives = "endless"
        ForcePlayerStarCounters = $true
        ForcePlayerStarCountersStartFrame = 1800
        ForcePlayerStarCountersEndFrame = 1810
        ForcePlayerBattleStars0 = $case.Stars
        ForcePlayerDisplayedStars0 = $case.Stars
        ForcePlayerCollectedStars0 = $case.Stars
        GameStateTrace = $true
        GameStateTraceExtended = $true
        LogDir = $caseLogDir
    }
    if ($case.Require -eq "Result") {
        $params.RequireResultScene = $true
    } else {
        $params.RequireNoResultScene = $true
    }

    Write-Host "MvL Big Star threshold: $($case.Name)"
    try {
        & (Join-Path $PSScriptRoot "run-nsmb-mvl-lan-route-smoke.ps1") @params
        $hostTrace = Join-Path $caseLogDir "host.game-state.csv"
        $rows = @(Import-Csv $hostTrace)
        $resultRows = @($rows | Where-Object { $_.sceneCurrentSceneID -eq "0xa" })
        $firstResult = $resultRows | Select-Object -First 1
        $results += [pscustomobject]@{
            Case = $case.Name
            BigStars = $case.BigStars
            ForcedStars = $case.Stars
            Expectation = $case.Require
            Result = "pass"
            ResultRows = $resultRows.Count
            FirstResultFrame = if ($firstResult) { $firstResult.frame } else { "" }
            LogDir = $caseLogDir
            Error = ""
        }
    } catch {
        $results += [pscustomobject]@{
            Case = $case.Name
            BigStars = $case.BigStars
            ForcedStars = $case.Stars
            Expectation = $case.Require
            Result = "fail"
            ResultRows = ""
            FirstResultFrame = ""
            LogDir = $caseLogDir
            Error = $_.Exception.Message
        }
        if ($StopOnFirstFailure) {
            break
        }
    }
}

$summaryPath = Join-Path $LogDir "bigstar-threshold-summary.csv"
$results | Export-Csv -NoTypeInformation -Encoding UTF8 $summaryPath
$failures = @($results | Where-Object { $_.Result -ne "pass" })
Write-Host "MvL Big Star threshold summary: $($results.Count) cases, $($failures.Count) failures -> $summaryPath"
if ($failures.Count -gt 0) {
    $failures | Format-Table -AutoSize
    throw "MvL Big Star threshold checks failed: $($failures.Count) / $($results.Count) cases"
}
