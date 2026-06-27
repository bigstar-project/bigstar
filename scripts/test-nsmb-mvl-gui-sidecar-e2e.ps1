param(
    [int]$Frames = 1200,
    [int]$TimeoutSeconds = 180,
    [string]$LogDir = "",
    [int]$InputDelayFrames = 4,
    [int]$InputMaxFrameLead = 4,
    [int]$InputBundleHistory = 8,
    [int]$MvlStage = 4,
    [ValidateSet(1, 2, 3)] [int]$MvlWins = 3,
    [ValidateSet(3, 5, 10)] [int]$MvlBigStars = 10,
    [ValidateSet("3", "5", "endless", "Endless")] [string]$MvlLives = "3",
    [ValidateSet("random", "select")] [string]$MvlCourseMode = "random",
    [string]$MvlMatchSeed = "771210505",
    [string]$Exe = "tools\nsmb-mvl-gui\src-tauri\target\release\melonDS.exe",
    [string]$BridgeExe = "tools\nsmb-mvl-gui\src-tauri\target\release\nsmb-net-bridge.exe",
    [string]$SignalUrl = "wss://nsmb-mvl-signaling-signaling-prod.uniunitaro.workers.dev/session",
    [ValidateSet("HostFirst", "ClientFirst")]
    [string]$MelonLaunchOrder = "ClientFirst",
    [int]$MelonLaunchGapMs = 1500
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot

function Resolve-RepoPath {
    param(
        [string]$Path,
        [switch]$MustExist
    )
    if ([System.IO.Path]::IsPathRooted($Path)) {
        $resolved = $Path
    } else {
        $resolved = Join-Path $repoRoot $Path
    }
    if ($MustExist -and -not (Test-Path -LiteralPath $resolved)) {
        throw "Path not found: $resolved"
    }
    return $resolved
}

function Read-TextIfExists {
    param([string]$Path)
    if (Test-Path -LiteralPath $Path) {
        $stream = [System.IO.File]::Open(
            $Path,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::ReadWrite
        )
        try {
            $reader = [System.IO.StreamReader]::new($stream)
            try {
                return $reader.ReadToEnd()
            } finally {
                $reader.Dispose()
            }
        } finally {
            $stream.Dispose()
        }
    }
    return ""
}

function Stop-E2EProcesses {
    param(
        [string]$RoomCode,
        [string]$ResolvedLogDir
    )
    $escapedLog = $ResolvedLogDir.Replace('\', '\\')
    Get-CimInstance Win32_Process |
        Where-Object {
            ($_.Name -eq "nsmb-net-bridge.exe" -and $_.CommandLine -like "*$RoomCode*") -or
            ($_.Name -eq "melonDS.exe" -and (
                $_.CommandLine -like "*$ResolvedLogDir*" -or
                $_.CommandLine -like "*$escapedLog*"
            ))
        } |
        ForEach-Object {
            Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
        }
}

function Assert-MelonLogPassed {
    param(
        [string]$Role,
        [string]$Text,
        [int]$ExpectedFrames
    )
    $forbidden = @(
        "input frame throttle timeout",
        "remote start ready wait timeout",
        "peer disconnected"
    )
    foreach ($pattern in $forbidden) {
        if ($Text.Contains($pattern)) {
            throw "$Role melonDS log contains failure pattern: $pattern"
        }
    }
    if (!$Text.Contains("NSMB PoC: peer connected")) {
        throw "$Role melonDS log did not reach ENet peer connected"
    }
    if (!$Text.Contains("NSMB InputNetplay: remote gameplay start ready accepted")) {
        throw "$Role melonDS log did not accept remote start ready"
    }
    if (!$Text.Contains("NSMB Test: frame limit reached at frame=$ExpectedFrames")) {
        throw "$Role melonDS log did not reach frame limit $ExpectedFrames"
    }
}

if ($LogDir -eq "") {
    $LogDir = "logs\nsmb-mvl-gui-sidecar-e2e-$('{0:yyyyMMddHHmmss}' -f (Get-Date))"
}
$resolvedLogDir = Resolve-RepoPath $LogDir
$roomCode = "e2e-$('{0:yyyyMMddHHmmss}' -f (Get-Date))-$([System.Guid]::NewGuid().ToString('N').Substring(0, 8))"
$triage = Join-Path $PSScriptRoot "run-nsmb-mvl-local-triage.ps1"

try {
    & $triage `
        -Mode WebRtc `
        -SignalUrl $SignalUrl `
        -RoomCode $roomCode `
        -Frames $Frames `
        -InputDelayFrames $InputDelayFrames `
        -InputMaxFrameLead $InputMaxFrameLead `
        -InputBundleHistory $InputBundleHistory `
        -LogDir $resolvedLogDir `
        -MvlStage $MvlStage `
        -MvlWins $MvlWins `
        -MvlBigStars $MvlBigStars `
        -MvlLives $MvlLives `
        -MvlCourseMode $MvlCourseMode `
        -MvlMatchSeed $MvlMatchSeed `
        -MelonLaunchOrder $MelonLaunchOrder `
        -MelonLaunchGapMs $MelonLaunchGapMs `
        -SkipRomEnsure `
        -Exe (Resolve-RepoPath $Exe -MustExist) `
        -BridgeExe (Resolve-RepoPath $BridgeExe -MustExist)

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $hostLog = Join-Path $resolvedLogDir "host\melonds.stdout.txt"
    $clientLog = Join-Path $resolvedLogDir "client\melonds.stdout.txt"
    while ((Get-Date) -lt $deadline) {
        $hostText = Read-TextIfExists $hostLog
        $clientText = Read-TextIfExists $clientLog
        $failurePattern = "input frame throttle timeout|remote start ready wait timeout|peer disconnected"
        if ($hostText -match $failurePattern -or $clientText -match $failurePattern) {
            Assert-MelonLogPassed -Role "host" -Text $hostText -ExpectedFrames $Frames
            Assert-MelonLogPassed -Role "client" -Text $clientText -ExpectedFrames $Frames
        }
        if ($hostText.Contains("NSMB Test: frame limit reached at frame=$Frames") -and
            $clientText.Contains("NSMB Test: frame limit reached at frame=$Frames")) {
            Assert-MelonLogPassed -Role "host" -Text $hostText -ExpectedFrames $Frames
            Assert-MelonLogPassed -Role "client" -Text $clientText -ExpectedFrames $Frames
            Write-Host "NSMB MvL GUI sidecar e2e passed: frames=$Frames log=$resolvedLogDir"
            exit 0
        }
        Start-Sleep -Milliseconds 500
    }

    $hostTail = (Read-TextIfExists $hostLog).Split("`n") | Select-Object -Last 20
    $clientTail = (Read-TextIfExists $clientLog).Split("`n") | Select-Object -Last 20
    throw "Timed out waiting for frame limit. hostTail=$($hostTail -join ' | ') clientTail=$($clientTail -join ' | ')"
} finally {
    Stop-E2EProcesses -RoomCode $roomCode -ResolvedLogDir $resolvedLogDir
}
