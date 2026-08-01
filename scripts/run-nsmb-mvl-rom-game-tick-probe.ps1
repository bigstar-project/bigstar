param(
    [int]$Frames = 1080,
    [int]$ProbeStartFrame = 951,
    [ValidateSet("host", "client")] [string]$TargetRole = "host",
    [ValidateRange(1, 7)] [int]$ExtraTicks = 1,
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$SourceRom = "roms\nsmb-us.nds",
    [switch]$AllowJit,
    [switch]$RenderlessAB,
    [switch]$AnalyzeExisting,
    [string]$LogDir = "logs\nsmb-mvl-rom-game-tick-probe"
)

$ErrorActionPreference = "Stop"

function Get-IncludedRanges {
    param(
        [int]$Length,
        [object[]]$ExcludedRanges
    )

    $cursor = 0
    foreach ($range in @($ExcludedRanges | Sort-Object Start)) {
        $start = [Math]::Max(0, [Math]::Min($Length, [int]$range.Start))
        $end = [Math]::Max($start, [Math]::Min($Length, [int]$range.End))
        if ($start -gt $cursor) {
            [pscustomobject]@{ Start = $cursor; End = $start }
        }
        $cursor = [Math]::Max($cursor, $end)
    }
    if ($cursor -lt $Length) {
        [pscustomobject]@{ Start = $cursor; End = $Length }
    }
}

function Compare-ProbeSnapshots {
    param(
        [string]$Left,
        [string]$Right,
        [object[]]$ExcludedRanges = @()
    )

    $leftBytes = [System.IO.File]::ReadAllBytes($Left)
    $rightBytes = [System.IO.File]::ReadAllBytes($Right)
    if ($leftBytes.Length -ne $rightBytes.Length) {
        throw "snapshot length mismatch: $Left=$($leftBytes.Length) $Right=$($rightBytes.Length)"
    }

    $differentBytes = 0
    $differentPages = [System.Collections.Generic.HashSet[int]]::new()
    $firstDifference = -1
    foreach ($range in @(Get-IncludedRanges -Length $leftBytes.Length -ExcludedRanges $ExcludedRanges)) {
        for ($offset = $range.Start; $offset -lt $range.End; $offset++) {
            if ($leftBytes[$offset] -eq $rightBytes[$offset]) {
                continue
            }
            if ($firstDifference -lt 0) {
                $firstDifference = $offset
            }
            $differentBytes++
            [void]$differentPages.Add([Math]::Floor($offset / 4096))
        }
    }

    return [pscustomobject]@{
        DifferentBytes = $differentBytes
        DifferentPages = $differentPages.Count
        FirstDifference = $firstDifference
    }
}

function Compare-ProbeDeltas {
    param(
        [string]$LeftBefore,
        [string]$LeftAfter,
        [string]$RightBefore,
        [string]$RightAfter,
        [object[]]$ExcludedRanges = @()
    )

    $leftBeforeBytes = [System.IO.File]::ReadAllBytes($LeftBefore)
    $leftAfterBytes = [System.IO.File]::ReadAllBytes($LeftAfter)
    $rightBeforeBytes = [System.IO.File]::ReadAllBytes($RightBefore)
    $rightAfterBytes = [System.IO.File]::ReadAllBytes($RightAfter)
    $lengths = @($leftBeforeBytes.Length, $leftAfterBytes.Length, $rightBeforeBytes.Length, $rightAfterBytes.Length) | Select-Object -Unique
    if ($lengths.Count -ne 1) {
        throw "delta snapshot length mismatch"
    }

    $leftChanged = 0
    $rightChanged = 0
    $sharedChanged = 0
    $changeMaskMismatch = 0
    $xorMismatch = 0
    $leftPages = [System.Collections.Generic.HashSet[int]]::new()
    $rightPages = [System.Collections.Generic.HashSet[int]]::new()
    foreach ($range in @(Get-IncludedRanges -Length $leftBeforeBytes.Length -ExcludedRanges $ExcludedRanges)) {
        for ($offset = $range.Start; $offset -lt $range.End; $offset++) {
            $leftDidChange = $leftBeforeBytes[$offset] -ne $leftAfterBytes[$offset]
            $rightDidChange = $rightBeforeBytes[$offset] -ne $rightAfterBytes[$offset]
            if ($leftDidChange) {
                $leftChanged++
                [void]$leftPages.Add([Math]::Floor($offset / 4096))
            }
            if ($rightDidChange) {
                $rightChanged++
                [void]$rightPages.Add([Math]::Floor($offset / 4096))
            }
            if ($leftDidChange -and $rightDidChange) {
                $sharedChanged++
            }
            if ($leftDidChange -ne $rightDidChange) {
                $changeMaskMismatch++
            }
            if (($leftBeforeBytes[$offset] -bxor $leftAfterBytes[$offset]) -ne
                ($rightBeforeBytes[$offset] -bxor $rightAfterBytes[$offset])) {
                $xorMismatch++
            }
        }
    }

    return [pscustomobject]@{
        LeftChangedBytes = $leftChanged
        LeftChangedPages = $leftPages.Count
        RightChangedBytes = $rightChanged
        RightChangedPages = $rightPages.Count
        SharedChangedBytes = $sharedChanged
        ChangeMaskMismatchBytes = $changeMaskMismatch
        XorMismatchBytes = $xorMismatch
    }
}

function Get-SnapshotPath {
    param(
        [string]$Root,
        [pscustomobject]$Row
    )

    return Join-Path $Root "rom-game-tick-probe-$($Row.role)-frame$($Row.frame)-$($Row.phase).bin"
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedLogDir = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $LogDir))
if (!$AnalyzeExisting) {
    if (Test-Path -LiteralPath $resolvedLogDir) {
        Remove-Item -LiteralPath $resolvedLogDir -Recurse -Force
    }
    $romDir = Join-Path $resolvedLogDir "roms"
    New-Item -ItemType Directory -Force -Path $romDir | Out-Null
    $hostRom = Join-Path $romDir "host.nds"
    $clientRom = Join-Path $romDir "client.nds"

    & cargo run --release --manifest-path (Join-Path $repoRoot "tools\bigstar-rom\Cargo.toml") -- `
        generate-stable `
        --source-rom ([System.IO.Path]::GetFullPath((Join-Path $repoRoot $SourceRom))) `
        --host-rom $hostRom `
        --client-rom $clientRom `
        --stage 0 `
        --wins 2 `
        --big-stars 5 `
        --lives endless `
        --course-mode random `
        --scene-settings 0x00b4ff00 `
        --game-tick-probe
    if ($LASTEXITCODE -ne 0) {
        throw "game-tick probe ROM generation failed with exit code $LASTEXITCODE"
    }

    $oldEnabled = $env:MELONDS_NSML_ROM_GAME_TICK_PROBE
    $oldStart = $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_START_FRAME
    $oldRole = $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_TARGET_ROLE
    $oldDir = $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_DIR
    $oldRestore = $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_RESTORE_AFTER_EXTRA
    $oldTicks = $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_EXTRA_TICKS
    $oldRenderlessAB = $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_RENDERLESS_AB
    try {
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE = "1"
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_START_FRAME = "$ProbeStartFrame"
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_TARGET_ROLE = $TargetRole
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_DIR = $resolvedLogDir
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_RESTORE_AFTER_EXTRA = "1"
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_EXTRA_TICKS = "$ExtraTicks"
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_RENDERLESS_AB = if ($RenderlessAB) { "1" } else { $null }

        $smokeArgs = @{
            Frames = $Frames
            Exe = $Exe
            HostRom = $hostRom
            ClientRom = $clientRom
            SkipRomEnsure = $true
            InputDelayFrames = 4
            InputMaxFrameLead = 2
            GameStateTraceInterval = 30
            GameStateTraceStartFrame = 900
            SkipGameStateComparison = $true
            NoFrameLimit = $true
            FixedFrameTime = $true
            NoDrawScreen = $true
            NoAudioSync = $true
            LogDir = Join-Path $resolvedLogDir "split"
        }
        if ($AllowJit) {
            $smokeArgs.AllowJit = $true
        }
        & (Join-Path $repoRoot "scripts\run-nsmb-mvl-split-local-input-smoke.ps1") @smokeArgs
    } finally {
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE = $oldEnabled
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_START_FRAME = $oldStart
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_TARGET_ROLE = $oldRole
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_DIR = $oldDir
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_RESTORE_AFTER_EXTRA = $oldRestore
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_EXTRA_TICKS = $oldTicks
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_RENDERLESS_AB = $oldRenderlessAB
    }
}

$controlRole = if ($TargetRole -eq "host") { "client" } else { "host" }
$targetRows = Import-Csv -LiteralPath (Join-Path $resolvedLogDir "rom-game-tick-probe-$TargetRole.csv")
$controlRows = Import-Csv -LiteralPath (Join-Path $resolvedLogDir "rom-game-tick-probe-$controlRole.csv")
$excludedRanges = @(
    [pscustomobject]@{ Start = 0x000019C0; End = 0x00001B00 },
    [pscustomobject]@{ Start = 0x00288000; End = 0x0028A000 }
)

if ($RenderlessAB) {
    $targetBeforeRows = @($targetRows | Where-Object { $_.phase -eq "before-renderless-ab-tick" })
    $targetAfterRows = @($targetRows | Where-Object { $_.phase -eq "after-renderless-tick" })
    $targetRecoveryRows = @($targetRows | Where-Object { $_.phase -eq "after-recovery-normal-tick" })
    $controlBeforeRows = @($controlRows | Where-Object { $_.phase -eq "before-renderless-ab-tick" })
    $controlAfterRows = @($controlRows | Where-Object { $_.phase -eq "after-normal-control-tick" })
    $controlRecoveryRows = @($controlRows | Where-Object { $_.phase -eq "after-second-normal-control-tick" })
    foreach ($entry in @(
        [pscustomobject]@{ Name = "target before"; Rows = $targetBeforeRows },
        [pscustomobject]@{ Name = "target after"; Rows = $targetAfterRows },
        [pscustomobject]@{ Name = "target recovery"; Rows = $targetRecoveryRows },
        [pscustomobject]@{ Name = "control before"; Rows = $controlBeforeRows },
        [pscustomobject]@{ Name = "control after"; Rows = $controlAfterRows },
        [pscustomobject]@{ Name = "control recovery"; Rows = $controlRecoveryRows }
    )) {
        if ($entry.Rows.Count -ne 1) { throw "$($entry.Name) phase missing or duplicated: count=$($entry.Rows.Count)" }
    }
    $targetBeforePath = Get-SnapshotPath -Root $resolvedLogDir -Row $targetBeforeRows[0]
    $targetAfterPath = Get-SnapshotPath -Root $resolvedLogDir -Row $targetAfterRows[0]
    $targetRecoveryPath = Get-SnapshotPath -Root $resolvedLogDir -Row $targetRecoveryRows[0]
    $controlBeforePath = Get-SnapshotPath -Root $resolvedLogDir -Row $controlBeforeRows[0]
    $controlAfterPath = Get-SnapshotPath -Root $resolvedLogDir -Row $controlAfterRows[0]
    $controlRecoveryPath = Get-SnapshotPath -Root $resolvedLogDir -Row $controlRecoveryRows[0]
    $rawBeforeCross = Compare-ProbeSnapshots -Left $targetBeforePath -Right $controlBeforePath
    $rawAfterCross = Compare-ProbeSnapshots -Left $targetAfterPath -Right $controlAfterPath
    $curatedBeforeCross = Compare-ProbeSnapshots -Left $targetBeforePath -Right $controlBeforePath -ExcludedRanges $excludedRanges
    $curatedAfterCross = Compare-ProbeSnapshots -Left $targetAfterPath -Right $controlAfterPath -ExcludedRanges $excludedRanges
    $curatedRecoveryCross = Compare-ProbeSnapshots -Left $targetRecoveryPath -Right $controlRecoveryPath -ExcludedRanges $excludedRanges
    $rawDelta = Compare-ProbeDeltas -LeftBefore $targetBeforePath -LeftAfter $targetAfterPath -RightBefore $controlBeforePath -RightAfter $controlAfterPath
    $curatedDelta = Compare-ProbeDeltas -LeftBefore $targetBeforePath -LeftAfter $targetAfterPath -RightBefore $controlBeforePath -RightAfter $controlAfterPath -ExcludedRanges $excludedRanges
    $summary = [pscustomobject]@{
        Mode = "renderless-ab"
        ExtraTicksRequested = $ExtraTicks
        TargetExtraTicksSeen = [uint32]$targetAfterRows[0].extra_ticks_seen
        ControlTicksSeen = [uint32]$controlAfterRows[0].extra_ticks_seen
        TargetRole = $TargetRole
        ControlRole = $controlRole
        TargetBeforeFrame = [int]$targetBeforeRows[0].frame
        TargetAfterFrame = [int]$targetAfterRows[0].frame
        ControlBeforeFrame = [int]$controlBeforeRows[0].frame
        ControlAfterFrame = [int]$controlAfterRows[0].frame
        TargetGameFrameBefore = [uint32]$targetBeforeRows[0].game_frame_counter
        TargetGameFrameAfter = [uint32]$targetAfterRows[0].game_frame_counter
        ControlGameFrameBefore = [uint32]$controlBeforeRows[0].game_frame_counter
        ControlGameFrameAfter = [uint32]$controlAfterRows[0].game_frame_counter
        TargetGameFrameAdvance = [uint32]$targetAfterRows[0].game_frame_counter - [uint32]$targetBeforeRows[0].game_frame_counter
        ControlGameFrameAdvance = [uint32]$controlAfterRows[0].game_frame_counter - [uint32]$controlBeforeRows[0].game_frame_counter
        RawCrossPeerBeforeBytes = $rawBeforeCross.DifferentBytes
        RawCrossPeerAfterBytes = $rawAfterCross.DifferentBytes
        CuratedCrossPeerBeforeBytes = $curatedBeforeCross.DifferentBytes
        CuratedCrossPeerBeforePages = $curatedBeforeCross.DifferentPages
        CuratedCrossPeerAfterBytes = $curatedAfterCross.DifferentBytes
        CuratedCrossPeerAfterPages = $curatedAfterCross.DifferentPages
        CuratedCrossPeerAfterRecoveryBytes = $curatedRecoveryCross.DifferentBytes
        CuratedCrossPeerAfterRecoveryPages = $curatedRecoveryCross.DifferentPages
        CuratedRenderlessChangedBytes = $curatedDelta.LeftChangedBytes
        CuratedRenderlessChangedPages = $curatedDelta.LeftChangedPages
        CuratedNormalChangedBytes = $curatedDelta.RightChangedBytes
        CuratedNormalChangedPages = $curatedDelta.RightChangedPages
        CuratedSharedChangedBytes = $curatedDelta.SharedChangedBytes
        CuratedChangeMaskMismatchBytes = $curatedDelta.ChangeMaskMismatchBytes
        CuratedXorMismatchBytes = $curatedDelta.XorMismatchBytes
        GameFrameCountersMatch = [uint32]$targetAfterRows[0].game_frame_counter -eq [uint32]$controlAfterRows[0].game_frame_counter
        RequestedGameFrameAdvanceObserved = `
            ([uint32]$targetAfterRows[0].game_frame_counter - [uint32]$targetBeforeRows[0].game_frame_counter -eq $ExtraTicks) -and `
            ([uint32]$controlAfterRows[0].game_frame_counter - [uint32]$controlBeforeRows[0].game_frame_counter -eq $ExtraTicks)
    }
    $summary | Export-Csv -LiteralPath (Join-Path $resolvedLogDir "summary.csv") -NoTypeInformation -Encoding UTF8
    $summary | Format-List
    Write-Host "NSMB MvL ROM renderless A/B probe completed: log=$resolvedLogDir"
    return
}

$targetPhases = @("target-after-normal-tick", "after-extra-tick", "next-before-normal-tick", "next-after-normal-tick")
$controlPhases = @("target-after-normal-tick", "next-before-normal-tick", "next-after-normal-tick")
$target = @{}
$control = @{}
foreach ($phase in $targetPhases) {
    $rows = @($targetRows | Where-Object { $_.phase -eq $phase })
    if ($rows.Count -ne 1) { throw "target phase missing or duplicated: $phase count=$($rows.Count)" }
    $target[$phase] = $rows[0]
}
foreach ($phase in $controlPhases) {
    $rows = @($controlRows | Where-Object { $_.phase -eq $phase })
    if ($rows.Count -ne 1) { throw "control phase missing or duplicated: $phase count=$($rows.Count)" }
    $control[$phase] = $rows[0]
}

$targetPaths = @{}
$controlPaths = @{}
foreach ($phase in $targetPhases) { $targetPaths[$phase] = Get-SnapshotPath -Root $resolvedLogDir -Row $target[$phase] }
foreach ($phase in $controlPhases) { $controlPaths[$phase] = Get-SnapshotPath -Root $resolvedLogDir -Row $control[$phase] }

$rawReplay = Compare-ProbeSnapshots -Left $targetPaths["after-extra-tick"] -Right $targetPaths["next-after-normal-tick"]
$curatedReplay = Compare-ProbeSnapshots -Left $targetPaths["after-extra-tick"] -Right $targetPaths["next-after-normal-tick"] -ExcludedRanges $excludedRanges
$baselineCrossPeer = Compare-ProbeSnapshots -Left $targetPaths["target-after-normal-tick"] -Right $controlPaths["target-after-normal-tick"] -ExcludedRanges $excludedRanges
$nextCrossPeer = Compare-ProbeSnapshots -Left $targetPaths["next-after-normal-tick"] -Right $controlPaths["next-after-normal-tick"] -ExcludedRanges $excludedRanges
$rawDelta = Compare-ProbeDeltas `
    -LeftBefore $targetPaths["target-after-normal-tick"] `
    -LeftAfter $targetPaths["after-extra-tick"] `
    -RightBefore $targetPaths["next-before-normal-tick"] `
    -RightAfter $targetPaths["next-after-normal-tick"]
$curatedDelta = Compare-ProbeDeltas `
    -LeftBefore $targetPaths["target-after-normal-tick"] `
    -LeftAfter $targetPaths["after-extra-tick"] `
    -RightBefore $targetPaths["next-before-normal-tick"] `
    -RightAfter $targetPaths["next-after-normal-tick"] `
    -ExcludedRanges $excludedRanges

$summary = [pscustomobject]@{
    TargetRole = $TargetRole
    ControlRole = $controlRole
    TargetFrame = [int]$target["target-after-normal-tick"].frame
    ExtraFrame = [int]$target["after-extra-tick"].frame
    NextNormalFrame = [int]$target["next-after-normal-tick"].frame
    GameFrameBefore = [uint32]$target["target-after-normal-tick"].game_frame_counter
    GameFrameAfterExtra = [uint32]$target["after-extra-tick"].game_frame_counter
    GameFrameAfterNormalReplay = [uint32]$target["next-after-normal-tick"].game_frame_counter
    ExtraTicksSeen = [uint32]$target["after-extra-tick"].extra_ticks_seen
    RawReplayDifferentBytes = $rawReplay.DifferentBytes
    RawReplayDifferentPages = $rawReplay.DifferentPages
    CuratedReplayDifferentBytes = $curatedReplay.DifferentBytes
    CuratedReplayDifferentPages = $curatedReplay.DifferentPages
    BaselineCrossPeerDifferentBytes = $baselineCrossPeer.DifferentBytes
    BaselineCrossPeerDifferentPages = $baselineCrossPeer.DifferentPages
    NextCrossPeerDifferentBytes = $nextCrossPeer.DifferentBytes
    NextCrossPeerDifferentPages = $nextCrossPeer.DifferentPages
    RawExtraChangedBytes = $rawDelta.LeftChangedBytes
    RawNormalChangedBytes = $rawDelta.RightChangedBytes
    RawChangeMaskMismatchBytes = $rawDelta.ChangeMaskMismatchBytes
    CuratedExtraChangedBytes = $curatedDelta.LeftChangedBytes
    CuratedExtraChangedPages = $curatedDelta.LeftChangedPages
    CuratedNormalChangedBytes = $curatedDelta.RightChangedBytes
    CuratedNormalChangedPages = $curatedDelta.RightChangedPages
    CuratedSharedChangedBytes = $curatedDelta.SharedChangedBytes
    CuratedChangeMaskMismatchBytes = $curatedDelta.ChangeMaskMismatchBytes
    CuratedXorMismatchBytes = $curatedDelta.XorMismatchBytes
    ExtraAndNormalGameFrameCounterMatch = [uint32]$target["after-extra-tick"].game_frame_counter -eq [uint32]$target["next-after-normal-tick"].game_frame_counter
}
$summary | Export-Csv -LiteralPath (Join-Path $resolvedLogDir "summary.csv") -NoTypeInformation -Encoding UTF8
$summary | Format-List
Write-Host "NSMB MvL ROM game-tick probe completed: log=$resolvedLogDir"
