param(
    [Parameter(Mandatory = $true)]
    [string]$LogDir,
    [int]$FromFrame = 200,
    [int]$ToFrame = 0,
    [int]$PositionTolerance = 0,
    [switch]$RequireRemoteInputHits,
    [switch]$RequireStarPickup,
    [switch]$RequireStarRespawn
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

$root = Resolve-Path $LogDir
$hostStdout = Join-Path $root "host.stdout.txt"
$clientStdout = Join-Path $root "client.stdout.txt"
$hostStatePath = Join-Path $root "host.game-state.csv"
$clientStatePath = Join-Path $root "client.game-state.csv"

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
    $hostReplay = Join-Path $root "host.stdout.txt.packet-replay.csv"
    $clientReplay = Join-Path $root "client.stdout.txt.packet-replay.csv"
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

Write-Host "NSMB MvL LAN result verified: frames=$checked from=$FromFrame tolerance=$PositionTolerance remoteInputHits=$($RequireRemoteInputHits.IsPresent) starPickup=$($RequireStarPickup.IsPresent) starRespawn=$($RequireStarRespawn.IsPresent)"
