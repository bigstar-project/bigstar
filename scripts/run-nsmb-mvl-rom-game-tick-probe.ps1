param(
    [int]$Frames = 1080,
    [int]$ProbeStartFrame = 951,
    [ValidateSet("host", "client", "both")] [string]$TargetRole = "host",
    [string]$MvlMatchSeed = "0x12345678",
    [ValidateRange(0, 65535)] [int]$HistoryBaseTick = 0x51,
    [ValidateRange(1, 4)] [int]$HistoryStartOffset = 2,
    [ValidateRange(1, 7)] [int]$ExtraTicks = 1,
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$SourceRom = "roms\nsmb-us.nds",
    [switch]$AllowJit,
    [switch]$RenderlessAB,
    [switch]$HistoricalInputAB,
    [switch]$GuestOwnedHistoryAB,
    [switch]$FrameBoundary,
    [switch]$StageTrace,
    [ValidateRange(0, 1000)] [double]$MaxTargetHistoryRunMs = 0,
    [ValidateRange(0, 1000)] [double]$MaxTargetHistoryFrameMs = 0,
    [ValidateRange(0, 1000000)] [int]$ScreenshotInterval = 0,
    [switch]$AnalyzeExisting,
    [string]$LogDir = "logs\nsmb-mvl-rom-game-tick-probe"
)

$ErrorActionPreference = "Stop"

if ($HistoricalInputAB -and -not $RenderlessAB) {
    throw "HistoricalInputAB requires RenderlessAB"
}
if ($GuestOwnedHistoryAB -and -not $HistoricalInputAB) {
    throw "GuestOwnedHistoryAB requires HistoricalInputAB"
}
if ($FrameBoundary -and -not $GuestOwnedHistoryAB) {
    throw "FrameBoundary requires GuestOwnedHistoryAB"
}
if ($StageTrace -and -not $FrameBoundary) {
    throw "StageTrace requires FrameBoundary"
}

function Get-StageTimingSummary {
    param(
        [string]$Root,
        [string]$Role,
        [int]$TickCount
    )

    $path = Join-Path $Root "rom-game-tick-stages-$Role.csv"
    if (!(Test-Path -LiteralPath $path)) {
        return $null
    }
    $rows = @(Import-Csv -LiteralPath $path | Sort-Object { [uint64]$_.sequence })
    $start = @($rows | Where-Object {
        $_.stage -eq "tick_begin" -and [uint32]$_.history_enabled -eq 1 -and
        [uint32]$_.history_index -eq 0
    } | Select-Object -First 1)
    if ($start.Count -ne 1) {
        throw "$Role stage trace has no unique transaction start"
    }
    $startRow = $start[0]
    $end = @($rows | Where-Object {
        [uint64]$_.sequence -gt [uint64]$startRow.sequence -and $_.stage -eq "tick_end" -and
        [uint32]$_.history_index -ge $TickCount
    } | Select-Object -First 1)
    if ($end.Count -ne 1) {
        throw "$Role stage trace has no transaction end"
    }
    $endRow = $end[0]
    $window = @($rows | Where-Object {
        [uint64]$_.sequence -ge [uint64]$startRow.sequence -and
        [uint64]$_.sequence -le [uint64]$endRow.sequence
    })

    $segmentTotals = @{
        InputWallUs = [uint64]0
        InputCycles = [uint64]0
        UpdateWallUs = [uint64]0
        UpdateCycles = [uint64]0
        RenderWallUs = [uint64]0
        RenderCycles = [uint64]0
        TailWallUs = [uint64]0
        TailCycles = [uint64]0
        DeleteWallUs = [uint64]0
        DeleteCycles = [uint64]0
        CreateWallUs = [uint64]0
        CreateCycles = [uint64]0
        GameplayWallUs = [uint64]0
        GameplayCycles = [uint64]0
    }
    $pairs = @(
        [pscustomobject]@{ Start = "input_begin"; End = "input_end"; Wall = "InputWallUs"; Cycles = "InputCycles" },
        [pscustomobject]@{ Start = "input_end"; End = "render_begin"; Wall = "UpdateWallUs"; Cycles = "UpdateCycles" },
        [pscustomobject]@{ Start = "render_begin"; End = "render_end"; Wall = "RenderWallUs"; Cycles = "RenderCycles" },
        [pscustomobject]@{ Start = "render_end"; End = "tick_end"; Wall = "TailWallUs"; Cycles = "TailCycles" },
        [pscustomobject]@{ Start = "delete_begin"; End = "delete_end"; Wall = "DeleteWallUs"; Cycles = "DeleteCycles" },
        [pscustomobject]@{ Start = "create_begin"; End = "create_end"; Wall = "CreateWallUs"; Cycles = "CreateCycles" },
        [pscustomobject]@{ Start = "gameplay_begin"; End = "gameplay_end"; Wall = "GameplayWallUs"; Cycles = "GameplayCycles" }
    )
    foreach ($pair in $pairs) {
        foreach ($segmentStart in @($window | Where-Object { $_.stage -eq $pair.Start })) {
            $segmentEnd = @($window | Where-Object {
                [uint64]$_.sequence -gt [uint64]$segmentStart.sequence -and $_.stage -eq $pair.End
            } | Select-Object -First 1)
            if ($segmentEnd.Count -ne 1) {
                continue
            }
            $segmentTotals[$pair.Wall] += [uint64]$segmentEnd[0].wall_us - [uint64]$segmentStart.wall_us
            $segmentTotals[$pair.Cycles] += [uint64]$segmentEnd[0].sys_timestamp - [uint64]$segmentStart.sys_timestamp
        }
    }

    $finalRenderBegin = @($window | Where-Object {
        $_.stage -eq "render_begin" -and [uint32]$_.history_index -ge $TickCount
    } | Select-Object -First 1)
    $finalRenderWallUs = [uint64]0
    $finalRenderCycles = [uint64]0
    if ($finalRenderBegin.Count -eq 1) {
        $finalRenderEnd = @($window | Where-Object {
            [uint64]$_.sequence -gt [uint64]$finalRenderBegin[0].sequence -and $_.stage -eq "render_end"
        } | Select-Object -First 1)
        if ($finalRenderEnd.Count -eq 1) {
            $finalRenderWallUs = [uint64]$finalRenderEnd[0].wall_us - [uint64]$finalRenderBegin[0].wall_us
            $finalRenderCycles = [uint64]$finalRenderEnd[0].sys_timestamp - [uint64]$finalRenderBegin[0].sys_timestamp
        }
    }

    $firstFrameEnd = @($rows | Where-Object {
        [uint64]$_.sequence -gt [uint64]$startRow.sequence -and $_.stage -eq "frame_end"
    } | Select-Object -First 1)
    return [pscustomobject]@{
        Role = $Role
        ExtraTicks = $TickCount
        TransactionWallUs = [uint64]$endRow.wall_us - [uint64]$startRow.wall_us
        TransactionCycles = [uint64]$endRow.sys_timestamp - [uint64]$startRow.sys_timestamp
        DisplayFramesTouched = [uint32]$endRow.display_frame - [uint32]$startRow.display_frame + 1
        FirstFrameRemainingWallUs = if ($firstFrameEnd.Count -eq 1) { [uint64]$firstFrameEnd[0].wall_us - [uint64]$startRow.wall_us } else { 0 }
        FirstFrameRemainingCycles = if ($firstFrameEnd.Count -eq 1) { [uint64]$firstFrameEnd[0].sys_timestamp - [uint64]$startRow.sys_timestamp } else { 0 }
        InputWallUs = $segmentTotals.InputWallUs
        InputCycles = $segmentTotals.InputCycles
        UpdateWallUs = $segmentTotals.UpdateWallUs
        UpdateCycles = $segmentTotals.UpdateCycles
        RenderWallUs = $segmentTotals.RenderWallUs
        RenderCycles = $segmentTotals.RenderCycles
        FinalRenderWallUs = $finalRenderWallUs
        FinalRenderCycles = $finalRenderCycles
        TailWallUs = $segmentTotals.TailWallUs
        TailCycles = $segmentTotals.TailCycles
        DeleteWallUs = $segmentTotals.DeleteWallUs
        DeleteCycles = $segmentTotals.DeleteCycles
        CreateWallUs = $segmentTotals.CreateWallUs
        CreateCycles = $segmentTotals.CreateCycles
        GameplayWallUs = $segmentTotals.GameplayWallUs
        GameplayCycles = $segmentTotals.GameplayCycles
    }
}

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

function Get-GameplaySemanticState {
    param([string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $baseAddress = 0x02000000
    function Read-U8([int]$Address) { return [uint32]$bytes[$Address - $baseAddress] }
    function Read-U16([int]$Address) { return [uint32][BitConverter]::ToUInt16($bytes, $Address - $baseAddress) }
    function Read-U32([int]$Address) { return [uint32][BitConverter]::ToUInt32($bytes, $Address - $baseAddress) }
    function Is-MainRamAddress([uint32]$Address, [int]$Length = 1) {
        $offset = [int64]$Address - $baseAddress
        return $offset -ge 0 -and $Length -gt 0 -and $offset + $Length -le $bytes.Length
    }

    $state = [ordered]@{}
    foreach ($field in @(
        @("gameFrame", 0x0208B668, 4), @("stageID", 0x02085A14, 4),
        @("stageGroup", 0x02085A18, 4), @("vsMode", 0x02085A84, 4),
        @("sceneActive", 0x0203BD28, 4), @("scenePrevious", 0x0203BD2C, 2),
        @("sceneNext", 0x0203BD30, 2), @("sceneCurrent", 0x0203BD34, 2),
        @("inputPlayer0Held", 0x02087660, 2), @("inputPlayer1Held", 0x02087662, 2),
        @("inputPlayer0Pressed", 0x02087664, 2), @("inputPlayer1Pressed", 0x02087666, 2),
        @("playerCount", 0x0208B348, 4), @("transition0", 0x0208B354, 4),
        @("transition1", 0x0208B358, 4), @("powerups", 0x0208B324, 4),
        @("dead", 0x0208B328, 2), @("inventoryPowerups", 0x0208B32C, 2),
        @("characters", 0x0208B330, 2), @("damageGuardTimers", 0x0208B344, 4),
        @("lives0", 0x0208B364, 4), @("lives1", 0x0208B368, 4),
        @("stars0", 0x0208B36C, 4), @("stars1", 0x0208B370, 4),
        @("coins0", 0x0208B37C, 4), @("coins1", 0x0208B380, 4),
        @("score0", 0x0208B384, 4), @("score1", 0x0208B388, 4),
        @("displayedStars0", 0x0208B38C, 4), @("displayedStars1", 0x0208B390, 4),
        @("deaths0", 0x0208B394, 4), @("deaths1", 0x0208B398, 4),
        @("collectedStars0", 0x0208B39C, 4), @("collectedStars1", 0x0208B3A0, 4),
        @("netRandomValue", 0x02088A68, 4), @("netRandomCallCount", 0x02088A48, 1),
        @("mvlGlobal965C", 0x020CA698, 1), @("mvlGlobal9670", 0x020CA6AC, 1),
        @("mvlGlobal9674", 0x020CA6B0, 1), @("mvlGlobal9694", 0x020CA6D0, 2)
    )) {
        $state[$field[0]] = if ($field[2] -eq 1) { Read-U8 $field[1] } elseif ($field[2] -eq 2) { Read-U16 $field[1] } else { Read-U32 $field[1] }
    }

    $objectBases = [System.Collections.Generic.HashSet[uint32]]::new()
    foreach ($listAddress in @(0x0208FB18, 0x0208FB28, 0x0208FB38, 0x0208FB48)) {
        $node = Read-U32 $listAddress
        $seenNodes = [System.Collections.Generic.HashSet[uint32]]::new()
        for ($index = 0; $index -lt 512 -and (Is-MainRamAddress $node 12); $index++) {
            if (!$seenNodes.Add($node)) { break }
            $objectBase = Read-U32 ($node + 8)
            if (Is-MainRamAddress $objectBase 0xC00) { [void]$objectBases.Add($objectBase) }
            $node = Read-U32 ($node + 4)
        }
    }

    $players = @($objectBases | Where-Object { Read-U16 ($_ + 0x0C) -eq 0x15 } | Sort-Object { Read-U8 ($_ + 0x11E) })
    $selected = @()
    for ($player = 0; $player -lt [Math]::Min(2, $players.Count); $player++) {
        $selected += [pscustomobject]@{ Prefix = "player$player"; Base = $players[$player]; Player = $true }
    }
    foreach ($candidate in @(
        [pscustomobject]@{ Prefix = "star"; ObjectID = 0x22; Settings = 1 },
        [pscustomobject]@{ Prefix = "hazard"; ObjectID = 0x53; Settings = 0 }
    )) {
        $match = @($objectBases | Where-Object {
            (Read-U16 ($_ + 0x0C)) -eq $candidate.ObjectID -and (Read-U32 ($_ + 8)) -eq $candidate.Settings
        } | Sort-Object { Read-U32 ($_ + 4) } | Select-Object -Last 1)
        if ($match.Count -eq 1) { $selected += [pscustomobject]@{ Prefix = $candidate.Prefix; Base = $match[0]; Player = $false } }
    }

    foreach ($actor in $selected) {
        foreach ($offset in @(0x04, 0x08, 0x0C, 0x0E, 0x10, 0x60, 0x64, 0x68, 0x70, 0x74, 0x78, 0x80, 0x84, 0x88, 0xD0, 0xD4, 0xD8)) {
            $width = if ($offset -eq 0x0C -or $offset -eq 0x0E) { 2 } else { 4 }
            $state["$($actor.Prefix).$('{0:X3}' -f $offset)"] = if ($width -eq 2) { Read-U16 ($actor.Base + $offset) } else { Read-U32 ($actor.Base + $offset) }
        }
        if ($actor.Player) {
            foreach ($offset in @(0x688, 0x728, 0x72C, 0x730, 0x778, 0x77C, 0x780, 0x784, 0x788, 0x790, 0x79C, 0x990, 0x994)) {
                $width = if ($offset -eq 0x79C) { 2 } else { 4 }
                $state["$($actor.Prefix).$('{0:X3}' -f $offset)"] = if ($width -eq 2) { Read-U16 ($actor.Base + $offset) } else { Read-U32 ($actor.Base + $offset) }
            }
            foreach ($offset in @(0x192, 0x75C, 0x7A8, 0x7A9, 0x7AA, 0x7AB, 0x7AC, 0x7AD, 0x7B0, 0x7B2, 0x7B3, 0x7B4, 0x7B5, 0xBA6, 0xBA7, 0xBA8, 0xBAD, 0xBB2, 0xBB3)) {
                $state["$($actor.Prefix).$('{0:X3}' -f $offset)"] = Read-U8 ($actor.Base + $offset)
            }
        }
    }
    return $state
}

function Compare-GameplaySemanticStates {
    param([string]$Left, [string]$Right)

    $leftState = Get-GameplaySemanticState -Path $Left
    $rightState = Get-GameplaySemanticState -Path $Right
    $differences = foreach ($name in $leftState.Keys) {
        if (!$rightState.Contains($name) -or $leftState[$name] -ne $rightState[$name]) {
            [pscustomobject]@{ Field = $name; Left = $leftState[$name]; Right = $rightState[$name] }
        }
    }
    return [pscustomobject]@{
        Differences = @($differences)
        DifferentFields = @($differences).Count
        ComparedFields = $leftState.Count
    }
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
    $oldHistoricalInputAB = $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_HISTORICAL_INPUT_AB
    $oldGuestOwnedHistoryAB = $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_GUEST_HISTORY_AB
    $oldFrameBoundary = $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_FRAME_BOUNDARY
    $oldStageTrace = $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_STAGE_TRACE
    $oldHistoryBaseTick = $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_BASE_TICK
    $oldHistoryStartOffset = $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_START_OFFSET
    try {
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE = "1"
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_START_FRAME = "$ProbeStartFrame"
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_TARGET_ROLE = $TargetRole
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_DIR = $resolvedLogDir
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_RESTORE_AFTER_EXTRA = "1"
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_EXTRA_TICKS = "$ExtraTicks"
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_RENDERLESS_AB = if ($RenderlessAB) { "1" } else { $null }
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_HISTORICAL_INPUT_AB = if ($HistoricalInputAB) { "1" } else { $null }
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_GUEST_HISTORY_AB = if ($GuestOwnedHistoryAB) { "1" } else { $null }
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_FRAME_BOUNDARY = if ($FrameBoundary) { "1" } else { $null }
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_STAGE_TRACE = if ($StageTrace) { "1" } else { $null }
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_BASE_TICK = "0x$($HistoryBaseTick.ToString('X4'))"
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_START_OFFSET = "$HistoryStartOffset"

        $smokeArgs = @{
            Frames = $Frames
            Exe = $Exe
            HostRom = $hostRom
            ClientRom = $clientRom
            SkipRomEnsure = $true
            InputDelayFrames = 4
            InputMaxFrameLead = 2
            MvlMatchSeed = $MvlMatchSeed
            GameStateTraceInterval = 30
            GameStateTraceStartFrame = 900
            SkipGameStateComparison = $true
            NoFrameLimit = $true
            FixedFrameTime = $true
            NoAudioSync = $true
            LogDir = Join-Path $resolvedLogDir "split"
        }
        if ($ScreenshotInterval -gt 0) {
            $smokeArgs.ScreenshotInterval = $ScreenshotInterval
            $smokeArgs.SoftwareRenderer = $true
        } else {
            $smokeArgs.NoDrawScreen = $true
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
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_HISTORICAL_INPUT_AB = $oldHistoricalInputAB
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_GUEST_HISTORY_AB = $oldGuestOwnedHistoryAB
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_FRAME_BOUNDARY = $oldFrameBoundary
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_STAGE_TRACE = $oldStageTrace
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_BASE_TICK = $oldHistoryBaseTick
        $env:MELONDS_NSML_ROM_GAME_TICK_PROBE_START_OFFSET = $oldHistoryStartOffset
    }
}

$controlRole = if ($TargetRole -eq "host") { "client" } else { "host" }
$targetRows = if ($TargetRole -ne "both") {
    Import-Csv -LiteralPath (Join-Path $resolvedLogDir "rom-game-tick-probe-$TargetRole.csv")
} else { @() }
$controlRows = if ($TargetRole -ne "both") {
    Import-Csv -LiteralPath (Join-Path $resolvedLogDir "rom-game-tick-probe-$controlRole.csv")
} else { @() }
$excludedRanges = @(
    [pscustomobject]@{ Start = 0x000019C0; End = 0x00001D00 },
    [pscustomobject]@{ Start = 0x00288000; End = 0x0028A000 }
)

if ($RenderlessAB) {
    if ($TargetRole -eq "both") {
        $roleSummaries = foreach ($role in @("host", "client")) {
            $rows = Import-Csv -LiteralPath (Join-Path $resolvedLogDir "rom-game-tick-probe-$role.csv")
            $beforeRows = @($rows | Where-Object { $_.phase -eq "before-renderless-ab-tick" })
            $afterRows = @($rows | Where-Object { $_.phase -eq "after-renderless-tick" })
            if ($beforeRows.Count -ne 1 -or $afterRows.Count -ne 1) {
                throw "$role symmetric phase missing or duplicated: before=$($beforeRows.Count) after=$($afterRows.Count)"
            }
            [pscustomobject]@{
                Role = $role
                ExtraTicksRequested = $ExtraTicks
                ExtraTicksSeen = [uint32]$afterRows[0].extra_ticks_seen
                InputSequenceHash = $afterRows[0].input_sequence_hash
                HistoryIndex = [uint32]$afterRows[0].history_index
                HistoryRunWallUs = [uint64]$afterRows[0].history_run_wall_us
                HistoryRunMaxUs = [uint64]$afterRows[0].history_run_max_us
                HistoryRunFrames = [uint32]$afterRows[0].history_run_frames
                HistoryUsPerTick = [math]::Round([uint64]$afterRows[0].history_run_wall_us / $ExtraTicks, 2)
                BeforeDisplayFrame = [uint32]$beforeRows[0].frame
                AfterDisplayFrame = [uint32]$afterRows[0].frame
                BeforeGameFrame = [uint32]$beforeRows[0].game_frame_counter
                AfterGameFrame = [uint32]$afterRows[0].game_frame_counter
                GameFrameAdvance = [uint32]$afterRows[0].game_frame_counter - [uint32]$beforeRows[0].game_frame_counter
            }
        }
        $roleSummaries | Export-Csv -LiteralPath (Join-Path $resolvedLogDir "summary.csv") -NoTypeInformation -Encoding UTF8
        $roleSummaries | Format-Table -AutoSize
        $stageSummaries = @(
            foreach ($role in @("host", "client")) {
                Get-StageTimingSummary -Root $resolvedLogDir -Role $role -TickCount $ExtraTicks
            }
        ) | Where-Object { $null -ne $_ }
        if ($stageSummaries.Count -gt 0) {
            $stageSummaries | Export-Csv -LiteralPath (Join-Path $resolvedLogDir "stage-summary.csv") -NoTypeInformation -Encoding UTF8
            $stageSummaries | Format-Table -AutoSize
        }
        foreach ($roleSummary in $roleSummaries) {
            if ($roleSummary.ExtraTicksSeen -ne $ExtraTicks -or $roleSummary.HistoryIndex -ne $ExtraTicks) {
                throw "$($roleSummary.Role) did not consume the full symmetric history"
            }
            if ($MaxTargetHistoryRunMs -gt 0 -and $roleSummary.HistoryRunWallUs -gt $MaxTargetHistoryRunMs * 1000) {
                throw "$($roleSummary.Role) symmetric history transaction exceeded budget: actualUs=$($roleSummary.HistoryRunWallUs) budgetMs=$MaxTargetHistoryRunMs"
            }
            if ($MaxTargetHistoryFrameMs -gt 0 -and $roleSummary.HistoryRunMaxUs -gt $MaxTargetHistoryFrameMs * 1000) {
                throw "$($roleSummary.Role) symmetric history frame exceeded budget: actualUs=$($roleSummary.HistoryRunMaxUs) budgetMs=$MaxTargetHistoryFrameMs"
            }
        }
        if (($roleSummaries.InputSequenceHash | Select-Object -Unique).Count -ne 1) {
            throw "symmetric input sequence hashes differ between roles"
        }
        Write-Host "NSMB MvL symmetric ROM history performance probe completed: log=$resolvedLogDir"
        return
    }
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
    $semanticAfterCross = Compare-GameplaySemanticStates -Left $targetAfterPath -Right $controlAfterPath
    $semanticRecoveryCross = Compare-GameplaySemanticStates -Left $targetRecoveryPath -Right $controlRecoveryPath
    @($semanticAfterCross.Differences | ForEach-Object { [pscustomobject]@{ Phase = "after"; Field = $_.Field; Target = $_.Left; Control = $_.Right } }) +
        @($semanticRecoveryCross.Differences | ForEach-Object { [pscustomobject]@{ Phase = "recovery"; Field = $_.Field; Target = $_.Left; Control = $_.Right } }) |
        Export-Csv -LiteralPath (Join-Path $resolvedLogDir "semantic-diff.csv") -NoTypeInformation -Encoding UTF8
    $rawDelta = Compare-ProbeDeltas -LeftBefore $targetBeforePath -LeftAfter $targetAfterPath -RightBefore $controlBeforePath -RightAfter $controlAfterPath
    $curatedDelta = Compare-ProbeDeltas -LeftBefore $targetBeforePath -LeftAfter $targetAfterPath -RightBefore $controlBeforePath -RightAfter $controlAfterPath -ExcludedRanges $excludedRanges
    $summary = [pscustomobject]@{
        Mode = if ($FrameBoundary) { "renderless-frame-boundary-ab" } elseif ($GuestOwnedHistoryAB) { "renderless-guest-history-ab" } elseif ($HistoricalInputAB) { "renderless-historical-input-ab" } else { "renderless-ab" }
        ExtraTicksRequested = $ExtraTicks
        TargetExtraTicksSeen = [uint32]$targetAfterRows[0].extra_ticks_seen
        ControlTicksSeen = [uint32]$controlAfterRows[0].extra_ticks_seen
        TargetInputSequenceHash = $targetAfterRows[0].input_sequence_hash
        ControlInputSequenceHash = $controlAfterRows[0].input_sequence_hash
        InputSequenceHashesMatch = $targetAfterRows[0].input_sequence_hash -eq $controlAfterRows[0].input_sequence_hash
        TargetHistoryIndex = [uint32]$targetAfterRows[0].history_index
        ControlHistoryIndex = [uint32]$controlAfterRows[0].history_index
        GuestHistoryConsumed = if ($GuestOwnedHistoryAB) {
            [uint32]$targetAfterRows[0].history_index -eq $ExtraTicks -and [uint32]$controlAfterRows[0].history_index -eq $ExtraTicks
        } else { $false }
        TargetHistoryRunWallUs = [uint64]$targetAfterRows[0].history_run_wall_us
        ControlHistoryRunWallUs = [uint64]$controlAfterRows[0].history_run_wall_us
        TargetHistoryRunMaxUs = [uint64]$targetAfterRows[0].history_run_max_us
        ControlHistoryRunMaxUs = [uint64]$controlAfterRows[0].history_run_max_us
        TargetHistoryRunFrames = [uint32]$targetAfterRows[0].history_run_frames
        ControlHistoryRunFrames = [uint32]$controlAfterRows[0].history_run_frames
        TargetHistoryUsPerTick = [math]::Round([uint64]$targetAfterRows[0].history_run_wall_us / $ExtraTicks, 2)
        GuestHistoryWallSpeedup = if ([uint64]$targetAfterRows[0].history_run_wall_us -gt 0) {
            [math]::Round([uint64]$controlAfterRows[0].history_run_wall_us / [uint64]$targetAfterRows[0].history_run_wall_us, 2)
        } else { 0 }
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
        SemanticAfterDifferentFields = $semanticAfterCross.DifferentFields
        SemanticRecoveryDifferentFields = $semanticRecoveryCross.DifferentFields
        SemanticComparedFields = $semanticAfterCross.ComparedFields
        GameplaySemanticsMatch = $semanticAfterCross.DifferentFields -eq 0 -and $semanticRecoveryCross.DifferentFields -eq 0
        CuratedRenderlessChangedBytes = $curatedDelta.LeftChangedBytes
        CuratedRenderlessChangedPages = $curatedDelta.LeftChangedPages
        CuratedNormalChangedBytes = $curatedDelta.RightChangedBytes
        CuratedNormalChangedPages = $curatedDelta.RightChangedPages
        CuratedSharedChangedBytes = $curatedDelta.SharedChangedBytes
        CuratedChangeMaskMismatchBytes = $curatedDelta.ChangeMaskMismatchBytes
        CuratedXorMismatchBytes = $curatedDelta.XorMismatchBytes
        GameFrameCountersMatch = [uint32]$targetAfterRows[0].game_frame_counter -eq [uint32]$controlAfterRows[0].game_frame_counter
        RequestedGameFrameAdvanceObserved = if ($FrameBoundary) {
            [uint32]$targetAfterRows[0].game_frame_counter -eq [uint32]$controlAfterRows[0].game_frame_counter -and `
                [uint32]$targetAfterRows[0].history_index -eq $ExtraTicks -and [uint32]$controlAfterRows[0].history_index -eq $ExtraTicks
        } else {
            ([uint32]$targetAfterRows[0].game_frame_counter - [uint32]$targetBeforeRows[0].game_frame_counter -eq $ExtraTicks) -and `
                ([uint32]$controlAfterRows[0].game_frame_counter - [uint32]$controlBeforeRows[0].game_frame_counter -eq $ExtraTicks)
        }
    }
    $summary | Export-Csv -LiteralPath (Join-Path $resolvedLogDir "summary.csv") -NoTypeInformation -Encoding UTF8
    $summary | Format-List
    if ($MaxTargetHistoryRunMs -gt 0 -and $summary.TargetHistoryRunWallUs -gt $MaxTargetHistoryRunMs * 1000) {
        throw "target guest-history transaction exceeded budget: actualUs=$($summary.TargetHistoryRunWallUs) budgetMs=$MaxTargetHistoryRunMs"
    }
    if ($MaxTargetHistoryFrameMs -gt 0 -and $summary.TargetHistoryRunMaxUs -gt $MaxTargetHistoryFrameMs * 1000) {
        throw "target guest-history frame exceeded budget: actualUs=$($summary.TargetHistoryRunMaxUs) budgetMs=$MaxTargetHistoryFrameMs"
    }
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
