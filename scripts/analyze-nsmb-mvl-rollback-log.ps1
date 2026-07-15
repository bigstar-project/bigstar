param(
    [Parameter(Mandatory=$true)] [string]$LogDir,
    [double]$SlowFrameThresholdMs = 33.0,
    [double]$MaxSingleFrameMs = 100.0,
    [int]$MaxConsecutiveSlowFrames = 120,
    [int]$PhaseSpikeStartFrame = 900,
    [int]$FreezeMinRows = 20,
    [int]$GameplayFreezeMinRows = 5
)

$ErrorActionPreference = "Stop"

$root = (Resolve-Path $LogDir).Path

function Get-Text {
    param([string]$Path)
    if (Test-Path $Path) {
        return [string](Get-Content $Path -Raw)
    }
    return ""
}

function Get-FirstMatchLine {
    param([string]$Text, [string]$Pattern)
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match $Pattern) {
            return $line
        }
    }
    return ""
}

function Get-LastMatchLine {
    param([string]$Text, [string]$Pattern)
    $last = ""
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match $Pattern) {
            $last = $line
        }
    }
    return $last
}

function Get-Backend {
    param([string]$Text)
    $line = Get-FirstMatchLine -Text $Text -Pattern "NSMB MvL Netplay: enabled"
    if ($line -match "rollbackBackend=([^ ]+)") {
        return $Matches[1]
    }
    return ""
}

function Get-MaxSlowRun {
    param([string]$Text, [double]$ThresholdMs)

    $maxRun = 0
    $run = 0
    $lastFrame = -1
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -notmatch "NSMB PerfSpike: .*frame=([0-9]+) frameTimeUs=([0-9]+)") {
            continue
        }
        $frame = [int]$Matches[1]
        $frameMs = [double]$Matches[2] / 1000.0
        if ($frameMs -lt $ThresholdMs) {
            continue
        }
        if ($lastFrame -ge 0 -and $frame -eq ($lastFrame + 1)) {
            $run++
        } else {
            $run = 1
        }
        $lastFrame = $frame
        if ($run -gt $maxRun) {
            $maxRun = $run
        }
    }
    return $maxRun
}

function Get-MaxFrameMs {
    param([string]$Text)

    $maxFrameMs = 0.0
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match "NSMB Test: active frame timing .*maxFrameMs=([0-9.]+)") {
            $value = [double]::Parse($Matches[1], [System.Globalization.CultureInfo]::InvariantCulture)
            if ($value -gt $maxFrameMs) {
                $maxFrameMs = $value
            }
        }
        if ($line -match "NSMB PerfSpike: .*frameTimeUs=([0-9]+)") {
            $value = [double]$Matches[1] / 1000.0
            if ($value -gt $maxFrameMs) {
                $maxFrameMs = $value
            }
        }
    }
    return $maxFrameMs
}

function Get-MaxPhaseSpike {
    param([string]$Text, [int]$StartFrame)

    $best = [pscustomobject]@{
        Frame = -1
        TotalMs = 0.0
        DominantPhase = ""
        DominantMs = 0.0
    }
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -notmatch "NSMB PerfPhaseSpike: .*frame=([0-9]+) totalMs=([0-9.]+) mpMs=([0-9.]+) inputMs=([0-9.]+) beforeHookMs=([0-9.]+) runFrameMs=([0-9.]+) afterHookMs=([0-9.]+) drawMs=([0-9.]+) audioMs=([0-9.]+) limitMs=([0-9.]+) unaccountedMs=([0-9.]+)") {
            continue
        }

        $frame = [int]$Matches[1]
        if ($frame -lt $StartFrame) {
            continue
        }
        $totalMs = [double]::Parse($Matches[2], [System.Globalization.CultureInfo]::InvariantCulture)
        if ($totalMs -le $best.TotalMs) {
            continue
        }

        $phases = [ordered]@{
            mp = [double]::Parse($Matches[3], [System.Globalization.CultureInfo]::InvariantCulture)
            input = [double]::Parse($Matches[4], [System.Globalization.CultureInfo]::InvariantCulture)
            beforeHook = [double]::Parse($Matches[5], [System.Globalization.CultureInfo]::InvariantCulture)
            runFrame = [double]::Parse($Matches[6], [System.Globalization.CultureInfo]::InvariantCulture)
            afterHook = [double]::Parse($Matches[7], [System.Globalization.CultureInfo]::InvariantCulture)
            draw = [double]::Parse($Matches[8], [System.Globalization.CultureInfo]::InvariantCulture)
            audio = [double]::Parse($Matches[9], [System.Globalization.CultureInfo]::InvariantCulture)
            limit = [double]::Parse($Matches[10], [System.Globalization.CultureInfo]::InvariantCulture)
            unaccounted = [double]::Parse($Matches[11], [System.Globalization.CultureInfo]::InvariantCulture)
        }
        $dominant = $phases.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 1
        $best = [pscustomobject]@{
            Frame = $frame
            TotalMs = $totalMs
            DominantPhase = [string]$dominant.Key
            DominantMs = [double]$dominant.Value
        }
    }
    return $best
}

function Get-LastHeartbeatFrame {
    param([string]$Text)
    $last = -1
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match "NSMB Heartbeat: .*frame=([0-9]+)") {
            $last = [int]$Matches[1]
        }
    }
    return $last
}

function Convert-TraceNumber {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return 0
    }
    if ($Value.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
        return [Convert]::ToInt64($Value.Substring(2), 16)
    }
    return [Convert]::ToInt64($Value, 10)
}

function Get-LongestGameplayHeartbeatPlateau {
    param([string]$Text)

    $best = [pscustomobject]@{ Rows = 0; StartFrame = -1; EndFrame = -1 }
    $lastSig = ""
    $runRows = 0
    $runStart = -1
    $lastFrame = -1
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -notmatch "NSMB GameplayHeartbeat: .*frame=([0-9]+) p0=([^ ]+) p1=([^ ]+) objects=([^ ]+)") {
            continue
        }
        $frame = [int]$Matches[1]
        $sig = "$($Matches[2])|$($Matches[3])|$($Matches[4])"
        if ($sig -eq $lastSig) {
            $runRows++
        } else {
            $lastSig = $sig
            $runRows = 1
            $runStart = $frame
        }
        $lastFrame = $frame
        if ($runRows -gt $best.Rows) {
            $best = [pscustomobject]@{ Rows = $runRows; StartFrame = $runStart; EndFrame = $lastFrame }
        }
    }
    return $best
}

function Get-GameplayHeartbeatObjectRows {
    param([string]$Text)

    $rows = @{}
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -notmatch "NSMB GameplayHeartbeat: .*frame=([0-9]+) .*objects=([0-9]+)/([0-9]+)/([0-9]+)/([0-9]+)/([0-9]+)/([0-9]+)") {
            continue
        }
        $frame = [int]$Matches[1]
        $rows[$frame] = [pscustomobject]@{
            Frame = $frame
            Total = [int]$Matches[2]
            Active = [int]$Matches[3]
            Dead = [int]$Matches[4]
            NotCreated = [int]$Matches[5]
            SkipUpdate = [int]$Matches[6]
            SkipRender = [int]$Matches[7]
            ActiveKeys = @(Get-GameplayHeartbeatActiveKeys -Line $line)
            Hazards = Get-GameplayHeartbeatHazards -Line $line
            Line = $line
        }
    }
    return $rows
}

function Get-GameplayHeartbeatActiveKeys {
    param([string]$Line)

    if ($Line -notmatch " activeIds=([^ ]+)") {
        return @()
    }

    $keys = @()
    foreach ($token in ($Matches[1] -split ",")) {
        if ($token -notmatch "^([0-9A-Fa-f]{3}):([0-9A-Fa-f]{8})$") {
            continue
        }
        $key = "$($Matches[1].ToUpperInvariant()):$($Matches[2].ToUpperInvariant())"
        if ($key -eq "000:00000000") {
            continue
        }
        $keys += $key
    }
    return $keys
}

function Get-GameplayHeartbeatHazards {
    param([string]$Line)

    if ($Line -notmatch " hazards=([^ ]+)") {
        return ""
    }
    return $Matches[1]
}

function ConvertTo-CountMap {
    param([string[]]$Keys)

    $map = @{}
    foreach ($key in $Keys) {
        if (-not $map.ContainsKey($key)) {
            $map[$key] = 0
        }
        $map[$key]++
    }
    return $map
}

function Remove-GameplayHeartbeatIgnoredKeys {
    param([hashtable]$Map)

    $filtered = @{}
    foreach ($key in $Map.Keys) {
        if ($key -like "012:*" -or $key -like "10B:*") {
            continue
        }
        $filtered[$key] = $Map[$key]
    }
    return $filtered
}

function Format-CountMapOnly {
    param(
        [hashtable]$Left,
        [hashtable]$Right,
        [int]$Limit = 8
    )

    $items = @()
    foreach ($key in ($Left.Keys | Sort-Object)) {
        $rightCount = 0
        if ($Right.ContainsKey($key)) {
            $rightCount = $Right[$key]
        }
        $delta = $Left[$key] - $rightCount
        if ($delta -le 0) {
            continue
        }
        if ($delta -gt 1) {
            $items += "$key`x$delta"
        } else {
            $items += $key
        }
        if ($items.Count -ge $Limit) {
            break
        }
    }

    if ($items.Count -eq 0) {
        return "-"
    }
    return ($items -join ",")
}

function Add-CountMapOnly {
    param(
        [hashtable]$Target,
        [hashtable]$Left,
        [hashtable]$Right
    )

    foreach ($key in $Left.Keys) {
        $rightCount = 0
        if ($Right.ContainsKey($key)) {
            $rightCount = $Right[$key]
        }
        $delta = $Left[$key] - $rightCount
        if ($delta -le 0) {
            continue
        }
        if (-not $Target.ContainsKey($key)) {
            $Target[$key] = 0
        }
        $Target[$key] += $delta
    }
}

function Format-TopCountMap {
    param(
        [hashtable]$Map,
        [int]$Limit = 8
    )

    $items = @(
        $Map.GetEnumerator() |
            Sort-Object @{ Expression = "Value"; Descending = $true }, @{ Expression = "Key"; Descending = $false } |
            Select-Object -First $Limit
    )
    if ($items.Count -eq 0) {
        return "-"
    }
    return (($items | ForEach-Object { "$($_.Key)x$($_.Value)" }) -join ",")
}

function Get-GameplayHeartbeatObjectDiff {
    param([string]$HostText, [string]$ClientText)

    $hostRows = Get-GameplayHeartbeatObjectRows -Text $HostText
    $clientRows = Get-GameplayHeartbeatObjectRows -Text $ClientText
    $significantHostOnly = @{}
    $significantClientOnly = @{}
    $summary = [pscustomobject]@{
        SharedFrames = 0
        DiffFrames = 0
        SignificantDiffFrames = 0
        MaxActiveDelta = 0
        FirstDiffFrame = -1
        FirstDiff = ""
        FirstActiveIdDiff = ""
        FirstSignificantActiveIdDiffFrame = -1
        FirstSignificantActiveIdDiff = ""
        TopSignificantHostOnly = ""
        TopSignificantClientOnly = ""
        FirstHazardDiffFrame = -1
        FirstHazardDiff = ""
    }
    foreach ($frame in ($hostRows.Keys | Sort-Object)) {
        if (-not $clientRows.ContainsKey($frame)) {
            continue
        }
        $summary.SharedFrames++
        $hostRow = $hostRows[$frame]
        $clientRow = $clientRows[$frame]
        $activeDelta = [Math]::Abs($hostRow.Active - $clientRow.Active)
        if ($activeDelta -gt $summary.MaxActiveDelta) {
            $summary.MaxActiveDelta = $activeDelta
        }
        if ($hostRow.Total -ne $clientRow.Total -or
            $hostRow.Active -ne $clientRow.Active -or
            $hostRow.Dead -ne $clientRow.Dead -or
            $hostRow.NotCreated -ne $clientRow.NotCreated) {
            $summary.DiffFrames++
            $hostMap = ConvertTo-CountMap -Keys $hostRow.ActiveKeys
            $clientMap = ConvertTo-CountMap -Keys $clientRow.ActiveKeys
            if ($summary.FirstDiffFrame -lt 0) {
                $summary.FirstDiffFrame = $frame
                $summary.FirstDiff = "host=$($hostRow.Total)/$($hostRow.Active)/$($hostRow.Dead)/$($hostRow.NotCreated) client=$($clientRow.Total)/$($clientRow.Active)/$($clientRow.Dead)/$($clientRow.NotCreated)"

                if ($hostRow.ActiveKeys.Count -gt 0 -or $clientRow.ActiveKeys.Count -gt 0) {
                    $hostOnly = Format-CountMapOnly -Left $hostMap -Right $clientMap
                    $clientOnly = Format-CountMapOnly -Left $clientMap -Right $hostMap
                    $summary.FirstActiveIdDiff = "hostOnly=$hostOnly clientOnly=$clientOnly"
                }
            }

            if ($summary.FirstSignificantActiveIdDiffFrame -lt 0 -and
                ($hostRow.ActiveKeys.Count -gt 0 -or $clientRow.ActiveKeys.Count -gt 0)) {
                $filteredHostMap = Remove-GameplayHeartbeatIgnoredKeys -Map $hostMap
                $filteredClientMap = Remove-GameplayHeartbeatIgnoredKeys -Map $clientMap
                $hostOnly = Format-CountMapOnly -Left $filteredHostMap -Right $filteredClientMap
                $clientOnly = Format-CountMapOnly -Left $filteredClientMap -Right $filteredHostMap
                if ($hostOnly -ne "-" -or $clientOnly -ne "-") {
                    $summary.FirstSignificantActiveIdDiffFrame = $frame
                    $summary.FirstSignificantActiveIdDiff = "hostOnly=$hostOnly clientOnly=$clientOnly"
                }
            }

            if ($hostRow.ActiveKeys.Count -gt 0 -or $clientRow.ActiveKeys.Count -gt 0) {
                $filteredHostMap = Remove-GameplayHeartbeatIgnoredKeys -Map $hostMap
                $filteredClientMap = Remove-GameplayHeartbeatIgnoredKeys -Map $clientMap
                $hostOnly = Format-CountMapOnly -Left $filteredHostMap -Right $filteredClientMap
                $clientOnly = Format-CountMapOnly -Left $filteredClientMap -Right $filteredHostMap
                if ($hostOnly -ne "-" -or $clientOnly -ne "-") {
                    $summary.SignificantDiffFrames++
                    Add-CountMapOnly -Target $significantHostOnly -Left $filteredHostMap -Right $filteredClientMap
                    Add-CountMapOnly -Target $significantClientOnly -Left $filteredClientMap -Right $filteredHostMap
                }
            }

            if ($summary.FirstHazardDiffFrame -lt 0 -and
                $hostRow.Hazards -and $clientRow.Hazards -and
                $hostRow.Hazards -ne $clientRow.Hazards) {
                $summary.FirstHazardDiffFrame = $frame
                $summary.FirstHazardDiff = "hostHazards=$($hostRow.Hazards) clientHazards=$($clientRow.Hazards)"
            }
        }
    }
    $summary.TopSignificantHostOnly = Format-TopCountMap -Map $significantHostOnly
    $summary.TopSignificantClientOnly = Format-TopCountMap -Map $significantClientOnly
    return $summary
}

function Get-LongestActorPlateau {
    param([string]$CsvPath, [int]$Player)

    if (-not (Test-Path $CsvPath)) {
        return [pscustomobject]@{ Player = $Player; Rows = 0; StartFrame = -1; EndFrame = -1; X = ""; Y = "" }
    }

    $rows = Import-Csv $CsvPath
    $foundField = "playerActor$($Player)Found"
    $xField = "playerActor$($Player)X"
    $yField = "playerActor$($Player)Y"
    $deadField = "player$($Player)Dead"
    $inputField = "inputPlayer$($Player)Held"

    $bestRows = 0
    $bestStart = -1
    $bestEnd = -1
    $bestX = ""
    $bestY = ""
    $runRows = 0
    $runStart = -1
    $lastKey = ""

    foreach ($row in $rows) {
        $isFound = (Convert-TraceNumber $row.$foundField) -ne 0
        $isDead = (Convert-TraceNumber $row.$deadField) -ne 0
        $hasInput = $true
        if ($row.PSObject.Properties.Name -contains $inputField) {
            $hasInput = (Convert-TraceNumber $row.$inputField) -ne 0
        }
        if (-not $isFound -or $isDead -or -not $hasInput) {
            $runRows = 0
            $lastKey = ""
            continue
        }

        $key = "$($row.$xField),$($row.$yField)"
        $frame = [int]$row.frame
        if ($key -eq $lastKey) {
            $runRows++
        } else {
            $runRows = 1
            $runStart = $frame
            $lastKey = $key
        }

        if ($runRows -gt $bestRows) {
            $bestRows = $runRows
            $bestStart = $runStart
            $bestEnd = $frame
            $bestX = $row.$xField
            $bestY = $row.$yField
        }
    }

    return [pscustomobject]@{
        Player = $Player
        Rows = $bestRows
        StartFrame = $bestStart
        EndFrame = $bestEnd
        X = $bestX
        Y = $bestY
    }
}

function Test-TraceHasResultScene {
    param([string]$CsvPath)

    if (-not (Test-Path $CsvPath)) {
        return $false
    }

    foreach ($row in (Import-Csv $CsvPath)) {
        if ($row.sceneCurrentSceneID -eq "0xa") {
            return $true
        }
    }
    return $false
}

$roleRows = @()
foreach ($role in @("host", "client")) {
    $roleDir = Join-Path $root $role
    $stdout = Get-Text (Join-Path $roleDir "$role.stdout.txt")
    $wrapperOut = Get-Text (Join-Path (Join-Path $root "wrapper") "$role-wrapper.out.txt")
    $wrapperErr = Get-Text (Join-Path (Join-Path $root "wrapper") "$role-wrapper.err.txt")
    $combined = "$stdout`n$wrapperOut`n$wrapperErr"
    $csvPath = Join-Path $roleDir "$role.game-state.csv"
    $plateau0 = Get-LongestActorPlateau -CsvPath $csvPath -Player 0
    $plateau1 = Get-LongestActorPlateau -CsvPath $csvPath -Player 1
    $maxPlateau = @($plateau0, $plateau1) | Sort-Object Rows -Descending | Select-Object -First 1
    $gameplayPlateau = Get-LongestGameplayHeartbeatPlateau -Text $stdout
    $hasResultScene = Test-TraceHasResultScene -CsvPath $csvPath
    $abort = Get-FirstMatchLine -Text $combined -Pattern "ARM[79]: (data|prefetch) abort"
    $slowRun = Get-MaxSlowRun -Text $stdout -ThresholdMs $SlowFrameThresholdMs
    $maxFrameMs = Get-MaxFrameMs -Text $stdout
    $maxPhaseSpike = Get-MaxPhaseSpike -Text $stdout -StartFrame $PhaseSpikeStartFrame
    $timing = Get-LastMatchLine -Text $stdout -Pattern "NSMB Test: active frame timing"
    $rollback = Get-LastMatchLine -Text $stdout -Pattern "NSMB Rollback: frame="
    $heartbeat = Get-LastHeartbeatFrame -Text $stdout
    $wrapperFailure = Get-FirstMatchLine -Text $combined -Pattern "missing .*frame limit|missing frame limit|stalled|timed out|active frame spike too high|gameplay mismatch|star pickup check failed|player death check failed"

    $status = "ok"
    if ($abort) {
        $status = "abort"
    } elseif ($wrapperFailure -match "stalled") {
        $status = "stalled"
    } elseif ($maxFrameMs -gt $MaxSingleFrameMs -or $slowRun -gt $MaxConsecutiveSlowFrames -or $wrapperFailure -match "active frame spike too high") {
        $status = "perf-fail"
    } elseif ($wrapperFailure) {
        $status = "failed"
    } elseif ($maxPlateau.Rows -ge $FreezeMinRows -and -not $hasResultScene) {
        $status = "freeze-suspect"
    }

    $roleRows += [pscustomobject]@{
        Role = $role
        Status = $status
        Backend = Get-Backend -Text $stdout
        LastHeartbeatFrame = $heartbeat
        MaxFrameMs = [Math]::Round($maxFrameMs, 3)
        MaxConsecutiveSlowFrames = $slowRun
        MaxPhaseSpikeFrame = $maxPhaseSpike.Frame
        MaxPhaseSpikeMs = [Math]::Round($maxPhaseSpike.TotalMs, 3)
        MaxPhaseSpikeDominant = if ($maxPhaseSpike.DominantPhase) {
            "$($maxPhaseSpike.DominantPhase)=$([Math]::Round($maxPhaseSpike.DominantMs, 3))ms"
        } else {
            ""
        }
        HasResultScene = $hasResultScene
        LongestActorPlateau = $maxPlateau.Rows
        LongestGameplayPlateau = $gameplayPlateau.Rows
        GameplayPlateauStart = $gameplayPlateau.StartFrame
        GameplayPlateauEnd = $gameplayPlateau.EndFrame
        PlateauPlayer = $maxPlateau.Player
        PlateauStart = $maxPlateau.StartFrame
        PlateauEnd = $maxPlateau.EndFrame
        Abort = $abort
        WrapperFailure = $wrapperFailure
        Rollback = $rollback
        ActiveTiming = $timing
    }
}

$overall = "ok"
if ($roleRows.Status -contains "abort") {
    $overall = "abort"
} elseif ($roleRows.Status -contains "stalled") {
    $overall = "stalled"
} elseif ($roleRows.Status -contains "perf-fail") {
    $overall = "perf-fail"
} elseif ($roleRows.Status -contains "failed") {
    $overall = "failed"
} elseif ($roleRows.Status -contains "freeze-suspect") {
    $overall = "freeze-suspect"
}

Write-Host "rollback log analysis: status=$overall log=$root"
$roleRows | Format-List

$hostStdout = Get-Text (Join-Path (Join-Path $root "host") "host.stdout.txt")
$clientStdout = Get-Text (Join-Path (Join-Path $root "client") "client.stdout.txt")
$gameplayObjectDiff = Get-GameplayHeartbeatObjectDiff -HostText $hostStdout -ClientText $clientStdout
if ($gameplayObjectDiff.SharedFrames -gt 0) {
    $activeIdDiff = ""
    if ($gameplayObjectDiff.FirstActiveIdDiff) {
        $activeIdDiff = " $($gameplayObjectDiff.FirstActiveIdDiff)"
    }
    $significantDiff = ""
    if ($gameplayObjectDiff.FirstSignificantActiveIdDiffFrame -ge 0) {
        $significantDiff = " firstSignificantActiveIdDiffFrame=$($gameplayObjectDiff.FirstSignificantActiveIdDiffFrame) $($gameplayObjectDiff.FirstSignificantActiveIdDiff)"
    }
    $hazardDiff = ""
    if ($gameplayObjectDiff.FirstHazardDiffFrame -ge 0) {
        $hazardDiff = " firstHazardDiffFrame=$($gameplayObjectDiff.FirstHazardDiffFrame) $($gameplayObjectDiff.FirstHazardDiff)"
    }
    Write-Host "gameplay heartbeat object diff: sharedFrames=$($gameplayObjectDiff.SharedFrames) diffFrames=$($gameplayObjectDiff.DiffFrames) significantDiffFrames=$($gameplayObjectDiff.SignificantDiffFrames) maxActiveDelta=$($gameplayObjectDiff.MaxActiveDelta) firstDiffFrame=$($gameplayObjectDiff.FirstDiffFrame) $($gameplayObjectDiff.FirstDiff)$activeIdDiff$significantDiff topSignificantHostOnly=$($gameplayObjectDiff.TopSignificantHostOnly) topSignificantClientOnly=$($gameplayObjectDiff.TopSignificantClientOnly)$hazardDiff"
}
