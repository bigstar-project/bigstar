param(
    [string]$LogDir = "",
    [string]$HostLogDir = "",
    [string]$ClientLogDir = "",
    [int]$FromFrame = 200,
    [int]$ToFrame = 0,
    [int]$PositionTolerance = 0,
    [switch]$RequireRemoteInputHits,
    [switch]$RequirePlayer0Input,
    [switch]$RequirePlayer1Input,
    [switch]$RequireStarPickup,
    [switch]$RequireStarRespawn,
    [int]$RequireNoLifeLossUntilFrame = 0,
    [switch]$RequireStageVisibleScreenshots,
    [switch]$RequirePlayerVisibleScreenshots,
    [double]$MinStageTerrainRatio = 0.2,
    [double]$MaxStageSkyRatio = 0.8,
    [double]$MaxStageGreenBackdropRatio = 0.5,
    [double]$MaxStageDominantRatio = 0.85,
    [int]$MinPlayerRedPixels = 40,
    [int]$MinPlayerDarkPixels = 0
)

$ErrorActionPreference = "Stop"

function Fail($message) {
    Write-Error $message
    exit 1
}

function Read-StateCsv($path) {
    if (!(Test-Path $path)) {
        Fail "missing game-state CSV: $path"
    }
    Import-Csv $path
}

function HexToInt64($value) {
    if ($null -eq $value -or $value -eq "") {
        return $null
    }
    $text = [string]$value
    if ($text.StartsWith("0x")) {
        $raw = [Convert]::ToUInt32($text.Substring(2), 16)
        if ([uint64]$raw -ge [uint64]2147483648) {
            return [int64]$raw - [int64]4294967296
        }
        return [int64]$raw
    }
    return [int64]$text
}

function Assert-Close($frame, $field, $a, $b, $tolerance) {
    $ia = HexToInt64 $a
    $ib = HexToInt64 $b
    if ($null -eq $ia -or $null -eq $ib) {
        Fail "frame $frame $field is missing: host='$a' client='$b'"
    }
    $diff = [Math]::Abs($ia - $ib)
    if ($diff -gt $tolerance) {
        Fail "frame $frame $field mismatch: host=$a client=$b diff=$diff tolerance=$tolerance"
    }
}

if (!$HostLogDir -and !$ClientLogDir) {
    if (!$LogDir) {
        Fail "LogDir or both HostLogDir/ClientLogDir must be provided"
    }
    $HostLogDir = $LogDir
    $ClientLogDir = $LogDir
}

if (!$HostLogDir -or !$ClientLogDir) {
    Fail "both HostLogDir and ClientLogDir must be provided when using split logs"
}

$hostRoot = Resolve-Path $HostLogDir
$clientRoot = Resolve-Path $ClientLogDir
$hostStdout = Join-Path $hostRoot "host.stdout.txt"
$clientStdout = Join-Path $clientRoot "client.stdout.txt"
$hostStatePath = Join-Path $hostRoot "host.game-state.csv"
$clientStatePath = Join-Path $clientRoot "client.game-state.csv"

foreach ($path in @($hostStdout, $clientStdout)) {
    if (!(Test-Path $path)) {
        Fail "missing stdout log: $path"
    }
    $bad = Select-String -Path $path -Pattern "通信が切断されました|communication.*(lost|disconnect)|disconnect screen|black-like|blank-like|timeout waiting for remote input" -CaseSensitive:$false
    if ($bad) {
        Fail "disconnect/blank marker found in $path`: $($bad[0].Line)"
    }
}

$hostRows = Read-StateCsv $hostStatePath
$clientRows = Read-StateCsv $clientStatePath
$clientByFrame = @{}
foreach ($row in $clientRows) {
    $clientByFrame[[int]$row.frame] = $row
}

$checked = 0
foreach ($hostRow in $hostRows) {
    $frame = [int]$hostRow.frame
    if ($frame -lt $FromFrame) {
        continue
    }
    if ($ToFrame -gt 0 -and $frame -gt $ToFrame) {
        continue
    }
    if (!$clientByFrame.ContainsKey($frame)) {
        continue
    }
    $client = $clientByFrame[$frame]
    foreach ($field in @("playerActor0X", "playerActor0Y", "playerActor1X", "playerActor1Y")) {
        Assert-Close $frame $field $hostRow.$field $client.$field $PositionTolerance
    }
    foreach ($field in @(
        "player0BattleStars",
        "player1BattleStars",
        "player0CollectedStars",
        "player1CollectedStars",
        "player0Coins",
        "player1Coins",
        "player0Dead",
        "player1Dead",
        "vsStarActorFound",
        "vsStarActorX",
        "vsStarActorY"
    )) {
        if ($hostRow.$field -ne $client.$field) {
            Fail "frame $frame $field mismatch: host=$($hostRow.$field) client=$($client.$field)"
        }
    }
    $checked++
}

if ($checked -eq 0) {
    Fail "no common game-state frames checked from frame $FromFrame"
}

if ($RequireRemoteInputHits) {
    $hostReplay = Join-Path $hostRoot "host.stdout.txt.packet-replay.csv"
    $clientReplay = Join-Path $clientRoot "client.stdout.txt.packet-replay.csv"
    foreach ($path in @($hostReplay, $clientReplay)) {
        if (!(Test-Path $path)) {
            Fail "missing packet replay log: $path"
        }
    }

    $hostRemoteHits = Import-Csv $hostReplay | Where-Object {
        $_.player -eq "1" -and $_.op -eq "keys" -and $_.hit -eq "1" -and $_.value -ne "00000000"
    } | Select-Object -First 1
    $clientRemoteHits = Import-Csv $clientReplay | Where-Object {
        $_.player -eq "0" -and $_.op -eq "keys" -and $_.hit -eq "1" -and $_.value -ne "00000000"
    } | Select-Object -First 1
    if (!$hostRemoteHits) {
        Fail "host did not replay non-zero remote player1 keys"
    }
    if (!$clientRemoteHits) {
        Fail "client did not replay non-zero remote player0 keys"
    }
}

function Assert-InputObserved($rows, $field, $label) {
    $hit = $rows | Where-Object {
        $_.PSObject.Properties.Name -contains $field -and
        (HexToInt64 ($_.$field)) -ne 0
    } | Select-Object -First 1
    if (!$hit) {
        Fail "$label input was required but $field never became non-zero"
    }
}

if ($RequirePlayer0Input) {
    Assert-InputObserved $hostRows "inputPlayer0Held" "player0"
}

if ($RequirePlayer1Input) {
    Assert-InputObserved $hostRows "inputPlayer1Held" "player1"
}

if ($RequireStarPickup) {
    $pickup = $hostRows | Where-Object {
        (HexToInt64 ($_.player0BattleStars)) -gt 0 -or
        (HexToInt64 ($_.player1BattleStars)) -gt 0 -or
        (HexToInt64 ($_.player0CollectedStars)) -gt 0 -or
        (HexToInt64 ($_.player1CollectedStars)) -gt 0
    } | Select-Object -First 1
    if (!$pickup) {
        Fail "star pickup was required but no battle/collected star counter changed"
    }
}

if ($RequireStarRespawn) {
    $firstPickup = $hostRows | Where-Object {
        (HexToInt64 ($_.player0BattleStars)) -gt 0 -or
        (HexToInt64 ($_.player1BattleStars)) -gt 0 -or
        (HexToInt64 ($_.player0CollectedStars)) -gt 0 -or
        (HexToInt64 ($_.player1CollectedStars)) -gt 0
    } | Select-Object -First 1
    if (!$firstPickup) {
        Fail "star respawn was required but no pickup was observed first"
    }

    $initialStar = $hostRows | Where-Object { $_.vsStarActorFound -eq "0x1" } | Select-Object -First 1
    if (!$initialStar) {
        Fail "star respawn was required but no initial star actor was observed"
    }

    $respawn = $hostRows | Where-Object {
        [int]$_.frame -gt [int]$firstPickup.frame -and
        $_.vsStarActorFound -eq "0x1" -and
        ($_.vsStarActorX -ne $initialStar.vsStarActorX -or $_.vsStarActorY -ne $initialStar.vsStarActorY)
    } | Select-Object -First 1
    if (!$respawn) {
        Fail "star respawn was required but no later star actor position change was observed after frame $($firstPickup.frame)"
    }
}

if ($RequireNoLifeLossUntilFrame -gt 0) {
    foreach ($entry in @(
        @{ Label = "host"; Rows = $hostRows; Stdout = $hostStdout },
        @{ Label = "client"; Rows = $clientRows; Stdout = $clientStdout }
    )) {
        $badLife = $entry.Rows | Where-Object {
            [int]$_.frame -ge $FromFrame -and
            [int]$_.frame -le $RequireNoLifeLossUntilFrame -and (
                $_.player0Lives -ne "0x5" -or
                $_.player1Lives -ne "0x5" -or
                $_.player0Deaths -ne "0x0" -or
                $_.player1Deaths -ne "0x0" -or
                $_.player0Dead -ne "0x0" -or
                $_.player1Dead -ne "0x0"
            )
        } | Select-Object -First 1
        if ($badLife) {
            Fail "$($entry.Label) life/death state changed before frame $RequireNoLifeLossUntilFrame`: frame=$($badLife.frame) lives=$($badLife.player0Lives)/$($badLife.player1Lives) deaths=$($badLife.player0Deaths)/$($badLife.player1Deaths) dead=$($badLife.player0Dead)/$($badLife.player1Dead)"
        }

        $badCall = Select-String -Path $entry.Stdout -Pattern "NSMB LifeCall: ([0-9]+),[0-9A-Fa-f]+,(Game::losePlayerLife|Game::addPlayerDeath)," | Where-Object {
            [int]$_.Matches[0].Groups[1].Value -ge $FromFrame -and
            [int]$_.Matches[0].Groups[1].Value -le $RequireNoLifeLossUntilFrame
        } | Select-Object -First 1
        if ($badCall) {
            Fail "$($entry.Label) life loss call before frame $RequireNoLifeLossUntilFrame`: $($badCall.Line)"
        }
    }
}

if ($RequireStageVisibleScreenshots -or $RequirePlayerVisibleScreenshots) {
    foreach ($entry in @(
        @{ Label = "host"; Root = $hostRoot; Dir = "screens-host" },
        @{ Label = "client"; Root = $clientRoot; Dir = "screens-client" }
    )) {
        $screenDir = Join-Path ([string]$entry.Root) $entry.Dir
        if (!(Test-Path $screenDir)) {
            Fail "$($entry.Label) screenshot directory is missing: $screenDir"
        }
        $latest = Get-ChildItem -Path $screenDir -Filter "*.png" | Sort-Object Name | Select-Object -Last 1
        if (!$latest) {
            Fail "$($entry.Label) screenshot directory has no PNGs: $screenDir"
        }
        $probeArgs = @(
            "tools\nsmb_screenshot_probe.py",
            $latest.FullName,
            "--band-start", "64",
            "--band-end", "192"
        )
        if ($RequireStageVisibleScreenshots) {
            $probeArgs += @(
                "--min-terrain-ratio", "$MinStageTerrainRatio",
                "--max-sky-ratio", "$MaxStageSkyRatio",
                "--max-green-backdrop-ratio", "$MaxStageGreenBackdropRatio",
                "--max-dominant-ratio", "$MaxStageDominantRatio"
            )
        }
        if ($RequirePlayerVisibleScreenshots) {
            $probeArgs += @(
                "--min-red-player-pixels", "$MinPlayerRedPixels",
                "--min-dark-model-pixels", "$MinPlayerDarkPixels"
            )
        }
        $probe = & python @probeArgs 2>&1
        if ($LASTEXITCODE -ne 0) {
            Fail "$($entry.Label) screenshot probe failed for $($latest.FullName): $($probe -join ' | ')"
        }
    }
}

Write-Host "NSMB MvL LAN result verified: frames=$checked from=$FromFrame tolerance=$PositionTolerance remoteInputHits=$($RequireRemoteInputHits.IsPresent) player0Input=$($RequirePlayer0Input.IsPresent) player1Input=$($RequirePlayer1Input.IsPresent) starPickup=$($RequireStarPickup.IsPresent) starRespawn=$($RequireStarRespawn.IsPresent) noLifeLossUntil=$RequireNoLifeLossUntilFrame stageVisible=$($RequireStageVisibleScreenshots.IsPresent) playerVisible=$($RequirePlayerVisibleScreenshots.IsPresent)"
