param(
    [Parameter(Mandatory=$true)] [string]$LogDir,
    [double]$SlowFrameThresholdMs = 33.0,
    [double]$MaxSingleFrameMs = 100.0,
    [int]$MaxConsecutiveSlowFrames = 120,
    [int]$PhaseSpikeStartFrame = 900,
    [int]$FreezeMinRows = 20
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
    $line = Get-FirstMatchLine -Text $Text -Pattern "NSMB PoC: enabled"
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
    $hasResultScene = Test-TraceHasResultScene -CsvPath $csvPath
    $abort = Get-FirstMatchLine -Text $combined -Pattern "ARM[79]: (data|prefetch) abort"
    $slowRun = Get-MaxSlowRun -Text $stdout -ThresholdMs $SlowFrameThresholdMs
    $maxFrameMs = Get-MaxFrameMs -Text $stdout
    $maxPhaseSpike = Get-MaxPhaseSpike -Text $stdout -StartFrame $PhaseSpikeStartFrame
    $timing = Get-LastMatchLine -Text $stdout -Pattern "NSMB Test: active frame timing"
    $rollback = Get-LastMatchLine -Text $stdout -Pattern "NSMB Rollback: frame="
    $heartbeat = Get-LastHeartbeatFrame -Text $stdout
    $wrapperFailure = Get-FirstMatchLine -Text $combined -Pattern "missing frame limit|stalled|timed out|active frame spike too high|gameplay mismatch|star pickup check failed|player death check failed"

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
