param(
    [int]$Frames = 1080,
    [int]$ProbeStartFrame = 951,
    [ValidateSet("host", "client")] [string]$TargetRole = "host",
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-netaid.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-netaid.tmp.nds",
    [switch]$AllowJit,
    [string]$LogDir = "logs\nsmb-mvl-game-tick-probe"
)

$ErrorActionPreference = "Stop"

function Get-ProbeSnapshotPath {
    param(
        [string]$Root,
        [pscustomobject]$Row
    )

    return Join-Path $Root "game-tick-probe-$($Row.role)-frame$($Row.frame)-$($Row.phase).bin"
}

function Compare-ProbeSnapshots {
    param(
        [string]$Left,
        [string]$Right
    )

    $leftBytes = [System.IO.File]::ReadAllBytes($Left)
    $rightBytes = [System.IO.File]::ReadAllBytes($Right)
    if ($leftBytes.Length -ne $rightBytes.Length) {
        throw "snapshot length mismatch: $Left=$($leftBytes.Length) $Right=$($rightBytes.Length)"
    }

    $differentBytes = 0
    $differentPages = [System.Collections.Generic.HashSet[int]]::new()
    $firstDifference = -1
    for ($offset = 0; $offset -lt $leftBytes.Length; $offset++) {
        if ($leftBytes[$offset] -eq $rightBytes[$offset]) {
            continue
        }
        if ($firstDifference -lt 0) {
            $firstDifference = $offset
        }
        $differentBytes++
        [void]$differentPages.Add([Math]::Floor($offset / 4096))
    }

    return [pscustomobject]@{
        DifferentBytes = $differentBytes
        DifferentPages = $differentPages.Count
        FirstDifference = $firstDifference
    }
}

function Compare-ProbeDeltas {
    param(
        [string]$LeftBefore,
        [string]$LeftAfter,
        [string]$RightBefore,
        [string]$RightAfter
    )

    $leftBeforeBytes = [System.IO.File]::ReadAllBytes($LeftBefore)
    $leftAfterBytes = [System.IO.File]::ReadAllBytes($LeftAfter)
    $rightBeforeBytes = [System.IO.File]::ReadAllBytes($RightBefore)
    $rightAfterBytes = [System.IO.File]::ReadAllBytes($RightAfter)
    $lengths = @($leftBeforeBytes.Length, $leftAfterBytes.Length, $rightBeforeBytes.Length, $rightAfterBytes.Length) | Select-Object -Unique
    if ($lengths.Count -ne 1) {
        throw "delta snapshot length mismatch"
    }

    $leftChanged = 0
    $rightChanged = 0
    $sharedChanged = 0
    $changeMaskMismatch = 0
    $xorMismatch = 0
    $leftPages = [System.Collections.Generic.HashSet[int]]::new()
    $rightPages = [System.Collections.Generic.HashSet[int]]::new()
    for ($offset = 0; $offset -lt $leftBeforeBytes.Length; $offset++) {
        $leftDidChange = $leftBeforeBytes[$offset] -ne $leftAfterBytes[$offset]
        $rightDidChange = $rightBeforeBytes[$offset] -ne $rightAfterBytes[$offset]
        if ($leftDidChange) {
            $leftChanged++
            [void]$leftPages.Add([Math]::Floor($offset / 4096))
        }
        if ($rightDidChange) {
            $rightChanged++
            [void]$rightPages.Add([Math]::Floor($offset / 4096))
        }
        if ($leftDidChange -and $rightDidChange) {
            $sharedChanged++
        }
        if ($leftDidChange -ne $rightDidChange) {
            $changeMaskMismatch++
        }
        if (($leftBeforeBytes[$offset] -bxor $leftAfterBytes[$offset]) -ne
            ($rightBeforeBytes[$offset] -bxor $rightAfterBytes[$offset])) {
            $xorMismatch++
        }
    }

    return [pscustomobject]@{
        LeftChangedBytes = $leftChanged
        LeftChangedPages = $leftPages.Count
        RightChangedBytes = $rightChanged
        RightChangedPages = $rightPages.Count
        SharedChangedBytes = $sharedChanged
        ChangeMaskMismatchBytes = $changeMaskMismatch
        XorMismatchBytes = $xorMismatch
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedLogDir = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $LogDir))
if (Test-Path -LiteralPath $resolvedLogDir) {
    Remove-Item -LiteralPath $resolvedLogDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $resolvedLogDir | Out-Null

$oldProbe = $env:MELONDS_NSML_GAME_TICK_PROBE
$oldProbeStart = $env:MELONDS_NSML_GAME_TICK_PROBE_START_FRAME
$oldProbeRole = $env:MELONDS_NSML_GAME_TICK_PROBE_TARGET_ROLE
$oldProbeDir = $env:MELONDS_NSML_GAME_TICK_PROBE_DIR
$oldProbeRestore = $env:MELONDS_NSML_GAME_TICK_PROBE_RESTORE_AFTER_EXTRA
$oldProbeFreeze = $env:MELONDS_NSML_GAME_TICK_PROBE_FREEZE_HARDWARE
try {
    $env:MELONDS_NSML_GAME_TICK_PROBE = "1"
    $env:MELONDS_NSML_GAME_TICK_PROBE_START_FRAME = "$ProbeStartFrame"
    $env:MELONDS_NSML_GAME_TICK_PROBE_TARGET_ROLE = $TargetRole
    $env:MELONDS_NSML_GAME_TICK_PROBE_DIR = $resolvedLogDir
    $env:MELONDS_NSML_GAME_TICK_PROBE_RESTORE_AFTER_EXTRA = "1"
    $env:MELONDS_NSML_GAME_TICK_PROBE_FREEZE_HARDWARE = "1"

    $smokeArgs = @{
        Frames = $Frames
        Exe = $Exe
        HostRom = $HostRom
        ClientRom = $ClientRom
        SkipRomEnsure = $true
        InputDelayFrames = 4
        InputMaxFrameLead = 2
        GameStateTraceInterval = 30
        GameStateTraceStartFrame = 900
        SkipGameStateComparison = $true
        NoFrameLimit = $true
        FixedFrameTime = $true
        NoDrawScreen = $true
        NoAudioSync = $true
        LogDir = Join-Path $resolvedLogDir "split"
    }
    if ($AllowJit) {
        $smokeArgs.AllowJit = $true
    }
    & (Join-Path $repoRoot "scripts\run-nsmb-mvl-split-local-input-smoke.ps1") @smokeArgs
} finally {
    $env:MELONDS_NSML_GAME_TICK_PROBE = $oldProbe
    $env:MELONDS_NSML_GAME_TICK_PROBE_START_FRAME = $oldProbeStart
    $env:MELONDS_NSML_GAME_TICK_PROBE_TARGET_ROLE = $oldProbeRole
    $env:MELONDS_NSML_GAME_TICK_PROBE_DIR = $oldProbeDir
    $env:MELONDS_NSML_GAME_TICK_PROBE_RESTORE_AFTER_EXTRA = $oldProbeRestore
    $env:MELONDS_NSML_GAME_TICK_PROBE_FREEZE_HARDWARE = $oldProbeFreeze
}

$controlRole = if ($TargetRole -eq "host") { "client" } else { "host" }
$targetCsv = Join-Path $resolvedLogDir "game-tick-probe-$TargetRole.csv"
$controlCsv = Join-Path $resolvedLogDir "game-tick-probe-$controlRole.csv"
if (-not (Test-Path -LiteralPath $targetCsv) -or -not (Test-Path -LiteralPath $controlCsv)) {
    throw "probe CSV missing: target=$targetCsv control=$controlCsv"
}

$targetRows = Import-Csv -LiteralPath $targetCsv
$controlRows = Import-Csv -LiteralPath $controlCsv
$requiredTargetPhases = @("target-after-normal-update", "after-extra-update", "next-before-update", "next-after-update-before-render", "next-after-update")
$requiredControlPhases = @("target-after-normal-update", "next-before-update", "next-after-update-before-render", "next-after-update")
$targetByPhase = @{}
$controlByPhase = @{}
foreach ($phase in $requiredTargetPhases) {
    $row = @($targetRows | Where-Object { $_.phase -eq $phase })
    if ($row.Count -ne 1) {
        throw "target phase missing or duplicated: role=$TargetRole phase=$phase count=$($row.Count)"
    }
    $targetByPhase[$phase] = $row[0]
}
foreach ($phase in $requiredControlPhases) {
    $row = @($controlRows | Where-Object { $_.phase -eq $phase })
    if ($row.Count -ne 1) {
        throw "control phase missing or duplicated: role=$controlRole phase=$phase count=$($row.Count)"
    }
    $controlByPhase[$phase] = $row[0]
}

$targetPaths = @{}
$controlPaths = @{}
foreach ($phase in $requiredTargetPhases) {
    $targetPaths[$phase] = Get-ProbeSnapshotPath -Root $resolvedLogDir -Row $targetByPhase[$phase]
}
foreach ($phase in $requiredControlPhases) {
    $controlPaths[$phase] = Get-ProbeSnapshotPath -Root $resolvedLogDir -Row $controlByPhase[$phase]
}

$targetCarry = Compare-ProbeSnapshots `
    -Left $targetPaths["after-extra-update"] `
    -Right $targetPaths["next-after-update-before-render"]
$crossPeer = Compare-ProbeSnapshots `
    -Left $targetPaths["next-after-update-before-render"] `
    -Right $controlPaths["next-after-update-before-render"]
$baselineCrossPeer = Compare-ProbeSnapshots `
    -Left $targetPaths["target-after-normal-update"] `
    -Right $controlPaths["target-after-normal-update"]
$fullAfterRenderCrossPeer = Compare-ProbeSnapshots `
    -Left $targetPaths["next-after-update"] `
    -Right $controlPaths["next-after-update"]
$deltaComparison = Compare-ProbeDeltas `
    -LeftBefore $targetPaths["target-after-normal-update"] `
    -LeftAfter $targetPaths["after-extra-update"] `
    -RightBefore $targetPaths["next-before-update"] `
    -RightAfter $targetPaths["next-after-update-before-render"]

$summary = [pscustomobject]@{
    TargetRole = $TargetRole
    ControlRole = $controlRole
    TargetFrame = [int]$targetByPhase["target-after-normal-update"].frame
    NextFrame = [int]$targetByPhase["next-after-update-before-render"].frame
    TargetGameFrameBefore = [uint32]$targetByPhase["target-after-normal-update"].game_frame_counter
    TargetGameFrameAfterExtra = [uint32]$targetByPhase["after-extra-update"].game_frame_counter
    TargetGameFrameAfterNormalReplay = [uint32]$targetByPhase["next-after-update-before-render"].game_frame_counter
    ControlGameFrameAfterUpdate = [uint32]$controlByPhase["next-after-update-before-render"].game_frame_counter
    ExtraToSameInstanceNormalDifferentBytes = $targetCarry.DifferentBytes
    ExtraToSameInstanceNormalDifferentPages = $targetCarry.DifferentPages
    BaselineCrossPeerDifferentBytes = $baselineCrossPeer.DifferentBytes
    BaselineCrossPeerDifferentPages = $baselineCrossPeer.DifferentPages
    BeforeRenderCrossPeerDifferentBytes = $crossPeer.DifferentBytes
    BeforeRenderCrossPeerDifferentPages = $crossPeer.DifferentPages
    FullAfterRenderCrossPeerDifferentBytes = $fullAfterRenderCrossPeer.DifferentBytes
    FullAfterRenderCrossPeerDifferentPages = $fullAfterRenderCrossPeer.DifferentPages
    IsolatedUpdateChangedBytes = $deltaComparison.LeftChangedBytes
    IsolatedUpdateChangedPages = $deltaComparison.LeftChangedPages
    NormalUpdateChangedBytes = $deltaComparison.RightChangedBytes
    NormalUpdateChangedPages = $deltaComparison.RightChangedPages
    SharedChangedBytes = $deltaComparison.SharedChangedBytes
    ChangeMaskMismatchBytes = $deltaComparison.ChangeMaskMismatchBytes
    XorMismatchBytes = $deltaComparison.XorMismatchBytes
    ExtraAdvancedGameFrameCounter = [uint32]$targetByPhase["after-extra-update"].game_frame_counter -ne
        [uint32]$targetByPhase["target-after-normal-update"].game_frame_counter
    ExtraAndNormalGameFrameCounterMatch = [uint32]$targetByPhase["next-after-update-before-render"].game_frame_counter -eq
        [uint32]$targetByPhase["after-extra-update"].game_frame_counter
}
$summary | Export-Csv -LiteralPath (Join-Path $resolvedLogDir "summary.csv") -NoTypeInformation -Encoding UTF8
$summary | Format-List

Write-Host "NSMB MvL game-tick probe completed: log=$resolvedLogDir"
