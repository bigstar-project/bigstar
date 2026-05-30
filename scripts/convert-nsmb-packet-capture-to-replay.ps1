param(
    [Parameter(Mandatory = $true)]
    [string]$HostCapture,

    [Parameter(Mandatory = $true)]
    [string]$ClientCapture,

    [Parameter(Mandatory = $true)]
    [string]$Output,

    [int]$FirstFrame = 0,
    [int]$LastFrame = 0
)

$ErrorActionPreference = "Stop"

function Import-PacketCaptureRows {
    param(
        [string]$Path,
        [int]$Player
    )

    if (-not (Test-Path $Path)) {
        throw "packet capture not found: $Path"
    }

    foreach ($row in Import-Csv $Path) {
        $frame = [int]$row.frame
        if ($FirstFrame -gt 0 -and $frame -lt $FirstFrame) {
            continue
        }
        if ($LastFrame -gt 0 -and $frame -gt $LastFrame) {
            continue
        }

        $packetHex = ([string]$row.packet_hex).Trim()
        if ($packetHex.Length -lt 104) {
            continue
        }

        $tickText = ([string]$row.tick).Trim()
        if ($tickText.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
            $tick = [Convert]::ToInt32($tickText.Substring(2), 16)
        } else {
            $tick = [int]$tickText
        }

        [pscustomobject]@{
            tick = $tick
            player = $Player
            frame = $frame
            keys = ([string]$row.keys).Trim()
            action = ([string]$row.action).Trim()
            packet_hex = $packetHex.Substring(0, 104).ToUpperInvariant()
        }
    }
}

$rows = @(
    Import-PacketCaptureRows -Path $HostCapture -Player 0
    Import-PacketCaptureRows -Path $ClientCapture -Player 1
) | Sort-Object tick, player, frame

if (-not $rows) {
    throw "no replay rows produced"
}

$outDir = Split-Path -Parent $Output
if ($outDir) {
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("tick,player,frame,keys,action,packet_hex")
foreach ($row in $rows) {
    $lines.Add(("{0},{1},{2},{3},{4},{5}" -f
        $row.tick,
        $row.player,
        $row.frame,
        $row.keys,
        $row.action,
        $row.packet_hex))
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllLines($Output, [string[]]$lines, $utf8NoBom)

Write-Host ("wrote {0} replay rows to {1}" -f $rows.Count, $Output)
