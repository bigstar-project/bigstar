param(
    [Parameter(Mandatory = $true)]
    [string]$LogDir,
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,
    [int]$EndFrame = 0
)

$ErrorActionPreference = "Stop"

function Resolve-FullPath {
    param([string]$Path, [switch]$MustExist)

    if (-not [System.IO.Path]::IsPathRooted($Path)) {
        $Path = Join-Path (Get-Location) $Path
    }
    $Path = [System.IO.Path]::GetFullPath($Path)
    if ($MustExist -and -not (Test-Path -LiteralPath $Path)) {
        throw "path does not exist: $Path"
    }
    return $Path
}

function Convert-RawKeysToName {
    param([int]$RawKeys)

    $held = (-bnot $RawKeys) -band 0xFFF
    $names = @()
    $mapping = @(
        @{ Mask = 0x001; Name = "A" },
        @{ Mask = 0x002; Name = "B" },
        @{ Mask = 0x004; Name = "SELECT" },
        @{ Mask = 0x008; Name = "START" },
        @{ Mask = 0x010; Name = "RIGHT" },
        @{ Mask = 0x020; Name = "LEFT" },
        @{ Mask = 0x040; Name = "UP" },
        @{ Mask = 0x080; Name = "DOWN" },
        @{ Mask = 0x100; Name = "R" },
        @{ Mask = 0x200; Name = "L" },
        @{ Mask = 0x400; Name = "X" },
        @{ Mask = 0x800; Name = "Y" }
    )
    foreach ($entry in $mapping) {
        if (($held -band $entry.Mask) -ne 0) {
            $names += $entry.Name
        }
    }
    if ($names.Count -eq 0) {
        return "NONE"
    }
    return $names -join "+"
}

function Read-AuthoritativeTransitions {
    param([string]$Path)

    $transitions = @{}
    foreach ($line in Get-Content -LiteralPath $Path -Encoding UTF8) {
        if ($line -match '^NSMB Rollback: prediction mismatch frame=(\d+).* actual=\{keys=0x([0-9A-Fa-f]+) ') {
            $frame = [int]$matches[1]
            $transitions[$frame] = [Convert]::ToInt32($matches[2], 16)
        }
    }
    return $transitions
}

function Add-PlayerSpans {
    param(
        [System.Collections.Generic.List[string]]$Lines,
        [string]$Instance,
        [System.Collections.IDictionary]$Transitions,
        [int]$LastFrame
    )

    $Lines.Add("$Instance 0-619 NONE")
    $Lines.Add("$Instance 620-627 DOWN")
    $Lines.Add("$Instance 628-659 NONE")
    $Lines.Add("$Instance 660-667 A")
    $Lines.Add("$Instance 668-839 NONE")

    $spanStart = 840
    $rawKeys = 0xFFF
    foreach ($frame in @($Transitions.Keys | ForEach-Object { [int]$_ } | Sort-Object)) {
        if ($frame -gt $LastFrame) {
            break
        }
        if ($frame -lt $spanStart) {
            $rawKeys = [int]$Transitions[$frame]
            continue
        }
        $nextRawKeys = [int]$Transitions[$frame]
        if ($nextRawKeys -eq $rawKeys) {
            continue
        }
        if ($frame -gt $spanStart) {
            $Lines.Add("$Instance $spanStart-$($frame - 1) $(Convert-RawKeysToName $rawKeys)")
        }
        $spanStart = $frame
        $rawKeys = $nextRawKeys
    }
    if ($spanStart -le $LastFrame) {
        $Lines.Add("$Instance $spanStart-$LastFrame $(Convert-RawKeysToName $rawKeys)")
    }
}

$resolvedLogDir = Resolve-FullPath $LogDir -MustExist
$clientLog = Join-Path $resolvedLogDir "client\client.stdout.txt"
$hostLog = Join-Path $resolvedLogDir "host\host.stdout.txt"
if (-not (Test-Path -LiteralPath $clientLog) -or -not (Test-Path -LiteralPath $hostLog)) {
    throw "host/client stdout logs are required under: $resolvedLogDir"
}

# A player's authoritative transitions appear as remote confirmations at the
# opposite peer. Player 0 is local to host, so recover it from the client log;
# player 1 is local to client, so recover it from the host log.
$player0 = Read-AuthoritativeTransitions $clientLog
$player1 = Read-AuthoritativeTransitions $hostLog
if ($player0.Count -eq 0 -or $player1.Count -eq 0) {
    throw "prediction mismatch transitions were not found in both logs"
}

if ($EndFrame -le 0) {
    $heartbeatFrames = @()
    foreach ($path in @($hostLog, $clientLog)) {
        foreach ($line in Get-Content -LiteralPath $path -Encoding UTF8) {
            if ($line -match '^NSMB Heartbeat: inst=\d+ frame=(\d+)') {
                $heartbeatFrames += [int]$matches[1]
            }
        }
    }
    $EndFrame = ($heartbeatFrames | Measure-Object -Maximum).Maximum
}
if ($EndFrame -lt 840) {
    throw "EndFrame must include gameplay: $EndFrame"
}

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("# Partial lower-bound reconstruction from $resolvedLogDir.")
$lines.Add("# Player-0 confirmations come from client; player-1 from host.")
$lines.Add("# WARNING: inputs received before speculative use produce no mismatch and are absent.")
$lines.Add("")
Add-PlayerSpans $lines "inst0" $player0 $EndFrame
$lines.Add("")
Add-PlayerSpans $lines "inst1" $player1 $EndFrame
$lines.Add("")

$resolvedOutput = Resolve-FullPath $OutputPath
$parent = Split-Path -Parent $resolvedOutput
if (-not (Test-Path -LiteralPath $parent)) {
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
}
[System.IO.File]::WriteAllLines($resolvedOutput, $lines, [System.Text.UTF8Encoding]::new($false))
Write-Warning "This is not an exact recording: already-confirmed input transitions are absent."
Write-Host "exported partial manual input fixture: $resolvedOutput p0Transitions=$($player0.Count) p1Transitions=$($player1.Count) endFrame=$EndFrame"
