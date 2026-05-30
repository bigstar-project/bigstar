param(
    [Parameter(Mandatory = $true)]
    [string]$Path,

    [int]$FirstFrame = 0,
    [int]$LastFrame = 0
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $Path)) {
    throw "packet capture not found: $Path"
}

$rows = Import-Csv $Path
if (-not $rows) {
    throw "packet capture is empty: $Path"
}

$offsetCounts = @{}
$offsetValues = @{}
$packetCount = 0
$nonZeroPacketCount = 0
$keyCounts = @{}

foreach ($row in $rows) {
    $frame = [int]$row.frame
    if ($FirstFrame -gt 0 -and $frame -lt $FirstFrame) {
        continue
    }
    if ($LastFrame -gt 0 -and $frame -gt $LastFrame) {
        continue
    }

    $hex = [string]$row.packet_hex
    if ($hex.Length -lt 104) {
        continue
    }

    $packetCount++
    $keys = [string]$row.keys
    if (-not $keyCounts.ContainsKey($keys)) {
        $keyCounts[$keys] = 0
    }
    $keyCounts[$keys]++

    $hasNonZeroPayload = $false
    for ($i = 8; $i -lt 52; $i++) {
        $byteText = $hex.Substring($i * 2, 2)
        $value = [Convert]::ToInt32($byteText, 16)
        if ($value -eq 0) {
            continue
        }

        $hasNonZeroPayload = $true
        if (-not $offsetCounts.ContainsKey($i)) {
            $offsetCounts[$i] = 0
        }
        $offsetCounts[$i]++
        if (-not $offsetValues.ContainsKey($i)) {
            $offsetValues[$i] = @{}
        }
        if (-not $offsetValues[$i].ContainsKey($byteText)) {
            $offsetValues[$i][$byteText] = 0
        }
        $offsetValues[$i][$byteText]++
    }

    if ($hasNonZeroPayload) {
        $nonZeroPacketCount++
    }
}

Write-Host "path=$Path"
Write-Host "frames=$FirstFrame..$LastFrame packets=$packetCount nonZeroPayloadPackets=$nonZeroPacketCount"

Write-Host "keys:"
$keyCounts.GetEnumerator() |
    Sort-Object Name |
    ForEach-Object { Write-Host ("  {0}: {1}" -f $_.Name, $_.Value) }

Write-Host "payload nonzero offsets:"
if ($offsetCounts.Count -eq 0) {
    Write-Host "  <none>"
} else {
    $offsetCounts.GetEnumerator() |
        Sort-Object {[int]$_.Name} |
        ForEach-Object {
            $offset = $_.Name
            $values = $offsetValues[$offset].GetEnumerator() |
                Sort-Object Name |
                ForEach-Object { "{0}={1}" -f $_.Name, $_.Value }
            Write-Host ("  {0}: {1} ({2})" -f $offset, $_.Value, ($values -join ", "))
        }
}
