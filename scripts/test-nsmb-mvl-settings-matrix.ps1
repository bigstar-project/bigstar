param(
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$Rom = "roms\nsmb-us.nds",
    [string]$InputScript = "tests\nsmb_us_direct_mvl_minimal_bootstrap.inputs",
    [string]$LogDir = "logs\nsmb-mvl-settings-matrix",
    [int]$Frames = 1200,
    [ValidateSet("random", "fixed")]
    [string[]]$CourseModes = @("random"),
    [int]$StartSeed = 0,
    [switch]$StopOnFirstFailure
)

$ErrorActionPreference = "Stop"

function Convert-ToMvlSceneSettings {
    param(
        [int]$Stage
    )

    if ($Stage -lt 0 -or $Stage -gt 4) {
        throw "MvlStage must be between 0 and 4: $Stage"
    }
    $settings = ((0xb4 + $Stage) -shl 16) -bor 0xff00
    return "0x$('{0:x6}' -f $settings)"
}

$winsValues = @(1, 2, 3)
$bigStarValues = @(3, 5, 10)
$lifeValues = @("3", "5", "endless")
$smokeScript = Join-Path $PSScriptRoot "run-nsmb-mvl-lan-route-smoke.ps1"
$results = @()
$caseIndex = 0

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

foreach ($courseMode in $CourseModes) {
    foreach ($wins in $winsValues) {
        foreach ($bigStars in $bigStarValues) {
            foreach ($lives in $lifeValues) {
                $seed = $StartSeed + $caseIndex
                $stage = $seed % 5
                $seedText = "0x$('{0:x8}' -f $seed)"
                $settings = Convert-ToMvlSceneSettings -Stage $stage
                $caseName = "course-$courseMode-w$wins-s$bigStars-l$lives-seed$seed"
                $caseLogDir = Join-Path $LogDir $caseName
                Write-Host "MvL settings matrix: $caseName settings=$settings stage=$stage"

                try {
                    & $smokeScript `
                        -RunRole both `
                        -Exe $Exe `
                        -Rom $Rom `
                        -InputScript $InputScript `
                        -Frames $Frames `
                        -ScreenshotInterval 0 `
                        -NoHashLog `
                        -NoLanMP `
                        -InputNetplay `
                        -InputDelayFrames 4 `
                        -InputMaxFrameLead 4 `
                        -PacketBridgeJitHelperPatch `
                        -PacketBridgeJitHelperPatchFrame 840 `
                        -PacketBridgeStartFrame 840 `
                        -WaitForPeerAtNetplayStart `
                        -AllowJit `
                        -InputUnreliable `
                        -InputBundleHistory 8 `
                        -GenerateMvlConfiguredRoms `
                        -MvlCourseMode $courseMode `
                        -MvlMatchSeed $seedText `
                        -MvlWins $wins `
                        -MvlBigStars $bigStars `
                        -MvlLives $lives `
                        -RequireMvlStage $stage `
                        -RequireMvlSceneSettings $settings `
                        -RequireMvlLives $lives `
                        -RequireMvlInitialSpawnState `
                        -GameStateTrace `
                        -GameStateTraceExtended `
                        -LogDir $caseLogDir
                    $results += [pscustomobject]@{
                        CourseMode = $courseMode
                        Wins = $wins
                        BigStars = $bigStars
                        Lives = $lives
                        MatchSeed = $seedText
                        Stage = $stage
                        SceneSettings = $settings
                        Result = "pass"
                        LogDir = $caseLogDir
                        Error = ""
                    }
                } catch {
                    $results += [pscustomobject]@{
                        CourseMode = $courseMode
                        Wins = $wins
                        BigStars = $bigStars
                        Lives = $lives
                        MatchSeed = $seedText
                        Stage = $stage
                        SceneSettings = $settings
                        Result = "fail"
                        LogDir = $caseLogDir
                        Error = $_.Exception.Message
                    }
                    Write-Host "MvL settings matrix failed: $caseName"
                    if ($StopOnFirstFailure) {
                        break
                    }
                }

                $caseIndex++
            }
            if ($StopOnFirstFailure -and ($results | Where-Object { $_.Result -eq "fail" })) { break }
        }
        if ($StopOnFirstFailure -and ($results | Where-Object { $_.Result -eq "fail" })) { break }
    }
    if ($StopOnFirstFailure -and ($results | Where-Object { $_.Result -eq "fail" })) { break }
}

$summaryPath = Join-Path $LogDir "settings-matrix-summary.csv"
$results | Export-Csv -NoTypeInformation -Encoding UTF8 $summaryPath
$failures = @($results | Where-Object { $_.Result -ne "pass" })
Write-Host "MvL settings matrix summary: $($results.Count) cases, $($failures.Count) failures -> $summaryPath"
if ($failures.Count -gt 0) {
    $failures | Format-Table -AutoSize
    throw "MvL settings matrix failed: $($failures.Count) / $($results.Count) cases"
}
