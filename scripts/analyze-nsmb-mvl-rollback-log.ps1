param(
    [Parameter(Mandatory=$true)] [string]$LogDir,
    [double]$SlowFrameThresholdMs = 33.0,
    [int]$MaxConsecutiveSlowFrames = 120,
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
    $abort = Get-FirstMatchLine -Text $combined -Pattern "ARM[79]: (data|prefetch) abort"
    $slowRun = Get-MaxSlowRun -Text $stdout -ThresholdMs $SlowFrameThresholdMs
    $timing = Get-LastMatchLine -Text $stdout -Pattern "NSMB Test: active frame timing"
    $rollback = Get-LastMatchLine -Text $stdout -Pattern "NSMB Rollback: frame="
    $heartbeat = Get-LastHeartbeatFrame -Text $stdout
    $wrapperFailure = Get-FirstMatchLine -Text $combined -Pattern "missing frame limit|stalled|timed out|gameplay mismatch|star pickup check failed|player death check failed"

    $status = "ok"
    if ($abort) {
        $status = "abort"
    } elseif ($wrapperFailure -match "stalled") {
        $status = "stalled"
    } elseif ($slowRun -gt $MaxConsecutiveSlowFrames) {
        $status = "perf-fail"
    } elseif ($maxPlateau.Rows -ge $FreezeMinRows) {
        $status = "freeze-suspect"
    } elseif ($wrapperFailure) {
        $status = "failed"
    }

    $roleRows += [pscustomobject]@{
        Role = $role
        Status = $status
        Backend = Get-Backend -Text $stdout
        LastHeartbeatFrame = $heartbeat
        MaxConsecutiveSlowFrames = $slowRun
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
} elseif ($roleRows.Status -contains "freeze-suspect") {
    $overall = "freeze-suspect"
} elseif ($roleRows.Status -contains "failed") {
    $overall = "failed"
}

Write-Host "rollback log analysis: status=$overall log=$root"
$roleRows | Format-List
