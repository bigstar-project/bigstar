param(
    [Parameter(Mandatory = $true)]
    [string]$LogDir,

    [int]$MinRows = 1
)

$ErrorActionPreference = "Stop"

function Read-ActorRows {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Missing lifecycle log: $Path"
    }

    $rows = @{}
    foreach ($match in Select-String -LiteralPath $Path -Pattern "^NSMB WorldObjects:") {
        $line = [string]$match.Line
        $frameMatch = [regex]::Match($line, "frame=(\d+)")
        if (-not $frameMatch.Success) {
            continue
        }

        $frame = [int]$frameMatch.Groups[1].Value
        $counts = @{}
        foreach ($actorMatch in [regex]::Matches($line, "actor=\d+/([0-9A-Fa-f]{3})/([0-9A-Fa-f]{8})/")) {
            $key = (
                $actorMatch.Groups[1].Value +
                "/" +
                $actorMatch.Groups[2].Value
            ).ToUpperInvariant()
            if (-not $counts.ContainsKey($key)) {
                $counts[$key] = 0
            }
            $counts[$key]++
        }
        $rows[$frame] = $counts
    }

    return ,$rows
}

$resolvedLogDir = (Resolve-Path -LiteralPath $LogDir).Path
$hostRows = Read-ActorRows -Path (Join-Path $resolvedLogDir "host\host.stdout.txt")
$clientRows = Read-ActorRows -Path (Join-Path $resolvedLogDir "client\client.stdout.txt")
$sharedFrames = @($hostRows.Keys | Where-Object { $clientRows.ContainsKey($_) } | Sort-Object)

$diffs = foreach ($frame in $sharedFrames) {
    $hostCounts = $hostRows[$frame]
    $clientCounts = $clientRows[$frame]
    $actorKeys = @($hostCounts.Keys + $clientCounts.Keys | Sort-Object -Unique)
    foreach ($actorKey in $actorKeys) {
        $hostCount = if ($hostCounts.ContainsKey($actorKey)) { $hostCounts[$actorKey] } else { 0 }
        $clientCount = if ($clientCounts.ContainsKey($actorKey)) { $clientCounts[$actorKey] } else { 0 }
        if ($hostCount -eq $clientCount) {
            continue
        }

        [pscustomobject]@{
            Frame       = $frame
            Actor       = $actorKey
            HostCount   = $hostCount
            ClientCount = $clientCount
        }
    }
}

Write-Host ("lifecycle samples: host={0} client={1} shared={2}" -f $hostRows.Count, $clientRows.Count, $sharedFrames.Count)

$summary = @(
    $diffs |
        Group-Object Actor |
        ForEach-Object {
            $rows = @($_.Group)
            $hostValues = @($rows.HostCount | Sort-Object -Unique) -join ","
            $clientValues = @($rows.ClientCount | Sort-Object -Unique) -join ","
            [pscustomobject]@{
                Actor        = $_.Name
                Rows         = $rows.Count
                FirstFrame   = ($rows.Frame | Measure-Object -Minimum).Minimum
                LastFrame    = ($rows.Frame | Measure-Object -Maximum).Maximum
                HostValues   = $hostValues
                ClientValues = $clientValues
            }
        } |
        Where-Object { $_.Rows -ge $MinRows } |
        Sort-Object @{ Expression = "Rows"; Descending = $true }, Actor
)

if ($summary.Count -eq 0) {
    Write-Host "No lifecycle actor-count differences matched the requested threshold."
    return
}

$summary | Format-Table -AutoSize
