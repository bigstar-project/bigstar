param(
    [Parameter(Mandatory = $true)]
    [string]$LogDir,
    [int]$Top = 40
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path $LogDir
$files = Get-ChildItem -Path $root -Recurse -File |
    Where-Object { $_.Extension -in @(".txt", ".log") -or $_.Name -like "*.stdout.txt" }

$ranges = @{}
$frames = @{}
$summaryCount = 0

foreach ($file in $files) {
    foreach ($line in Get-Content $file.FullName) {
        if ($line -match "NSMB RollbackDeltaPages: frame=([0-9]+).*uncoveredPages=([0-9]+).*uncoveredBytes=([0-9]+)") {
            $summaryCount++
            continue
        }
        if ($line -notmatch "NSMB RollbackDeltaPagesUncovered: frame=([0-9]+) runs=(.+)$") {
            continue
        }

        $frame = [int]$Matches[1]
        $runs = $Matches[2] -split ";"
        foreach ($run in $runs) {
            if ($run -notmatch "0x([0-9A-Fa-f]+)\+0x([0-9A-Fa-f]+)\(([^)]+)\)") {
                continue
            }
            $addr = [Convert]::ToUInt32($Matches[1], 16)
            $len = [Convert]::ToUInt32($Matches[2], 16)
            $region = $Matches[3]
            $key = ("0x{0:X8}+0x{1:X}" -f $addr, $len)
            if (-not $ranges.ContainsKey($key)) {
                $ranges[$key] = [pscustomobject]@{
                    Range = $key
                    Address = $addr
                    Length = $len
                    Region = $region
                    Hits = 0
                    FirstFrame = $frame
                    LastFrame = $frame
                }
            }
            $item = $ranges[$key]
            $item.Hits++
            if ($frame -lt $item.FirstFrame) { $item.FirstFrame = $frame }
            if ($frame -gt $item.LastFrame) { $item.LastFrame = $frame }
            $frames[$frame] = $true
        }
    }
}

Write-Host "NSMB rollback delta page analysis: log=$root summaries=$summaryCount uncoveredFrames=$($frames.Count) ranges=$($ranges.Count)"
$ranges.Values |
    Sort-Object -Property Hits, Length -Descending |
    Select-Object -First $Top Range, Region, Hits, FirstFrame, LastFrame |
    Format-Table -AutoSize
