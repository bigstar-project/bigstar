param(
    [Parameter(Mandatory = $true)]
    [string]$RecordingManifest,
    [string]$LogDir = "",
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$HostRom = "",
    [string]$ClientRom = "",
    [string]$HostInputScript = "",
    [string]$ClientInputScript = "",
    [string]$PacketReplayFile = "",
    [string]$HostPacketReplayFile = "",
    [string]$ClientPacketReplayFile = "",
    [int]$Frames = 0,
    [int]$WaitTimeoutMs = 300000,
    [int]$AIPlayLogInterval = 1,
    [int]$AIPlayLogMaxObjects = 128,
    [int]$PositionTolerance = 0,
    [int]$CheckpointInterval = 0,
    [int]$CheckpointStartFrame = 0,
    [int]$MaxCheckpoints = 0,
    [ValidateSet("", "host", "client")]
    [string]$VerifySide = "",
    [switch]$IgnoreHash,
    [switch]$IgnoreObjectCounts,
    [switch]$CheckCategoryCounts,
    [switch]$ScanFrames,
    [string]$MismatchReportJson = "",
    [string]$MismatchReportCsv = "",
    [int]$MaxMismatchFrames = 20,
    [switch]$GenerateMvlConfiguredRoms,
    [switch]$AllowJit,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$manifestPath = (Resolve-Path -LiteralPath $RecordingManifest).Path
$manifestDir = Split-Path -Parent $manifestPath
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json

function Get-JsonValue {
    param(
        [object]$Object,
        [string]$Name,
        [object]$Default = ""
    )

    if ($null -eq $Object) {
        return $Default
    }
    if ($Object.PSObject.Properties.Name -contains $Name) {
        $value = $Object.$Name
        if ($null -ne $value) {
            return $value
        }
    }
    return $Default
}

function Resolve-ManifestPath {
    param(
        [string]$Value,
        [string]$BaseDir,
        [switch]$Required
    )

    if ([string]::IsNullOrWhiteSpace($Value)) {
        if ($Required) {
            throw "missing required manifest path"
        }
        return ""
    }
    $candidate = if ([System.IO.Path]::IsPathRooted($Value)) {
        $Value
    } else {
        Join-Path $BaseDir $Value
    }
    if (-not [System.IO.Path]::IsPathRooted($Value) -and -not (Test-Path -LiteralPath $candidate)) {
        $repoCandidate = Join-Path $repoRoot $Value
        if (Test-Path -LiteralPath $repoCandidate) {
            $candidate = $repoCandidate
        }
    }
    if ($Required -and -not (Test-Path -LiteralPath $candidate)) {
        throw "manifest path does not exist: $candidate"
    }
    return $candidate
}

function Resolve-RepoPath {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return ""
    }
    if ([System.IO.Path]::IsPathRooted($Value)) {
        return $Value
    }
    return Join-Path $repoRoot $Value
}

function Resolve-OutputRoot {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $name = [System.IO.Path]::GetFileNameWithoutExtension($manifestPath)
        $relative = "logs\nsmb-mvl-recording-replay-$name-$timestamp"
        return @{
            Relative = $relative
            Absolute = Join-Path $repoRoot $relative
        }
    }
    if ([System.IO.Path]::IsPathRooted($Value)) {
        return @{
            Relative = $Value
            Absolute = $Value
        }
    }
    return @{
        Relative = $Value
        Absolute = Join-Path $repoRoot $Value
    }
}

$replay = Get-JsonValue $manifest "replay" $null
$mode = [string](Get-JsonValue $replay "mode" "")
if ($mode -eq "") {
    $mode = "input_script"
}
if ($mode -ne "input_script" -and $mode -ne "packet_replay") {
    throw "unsupported recording replay mode: $mode"
}

$manifestHostInput = [string](Get-JsonValue $manifest "hostInputScript" "")
$manifestClientInput = [string](Get-JsonValue $manifest "clientInputScript" "")
$replayHostInput = [string](Get-JsonValue $replay "hostInputScript" "")
$replayClientInput = [string](Get-JsonValue $replay "clientInputScript" "")
if ($manifestHostInput -eq "" -and $replayHostInput -ne "") { $manifestHostInput = $replayHostInput }
if ($manifestClientInput -eq "" -and $replayClientInput -ne "") { $manifestClientInput = $replayClientInput }

$defaultInputScript = "tests\nsmb_us_direct_mvl_minimal_bootstrap.inputs"
$effectiveHostInput = if ($HostInputScript -ne "") {
    Resolve-RepoPath $HostInputScript
} elseif ($manifestHostInput -ne "") {
    Resolve-ManifestPath $manifestHostInput $manifestDir -Required
} elseif ($mode -eq "packet_replay") {
    Resolve-RepoPath $defaultInputScript
} else {
    Resolve-ManifestPath $manifestHostInput $manifestDir -Required
}
$effectiveClientInput = if ($ClientInputScript -ne "") {
    Resolve-RepoPath $ClientInputScript
} elseif ($manifestClientInput -ne "") {
    Resolve-ManifestPath $manifestClientInput $manifestDir -Required
} elseif ($mode -eq "packet_replay") {
    Resolve-RepoPath $defaultInputScript
} else {
    Resolve-ManifestPath $manifestClientInput $manifestDir -Required
}

$manifestPacketReplay = [string](Get-JsonValue $manifest "packetReplayFile" "")
$manifestHostPacketReplay = [string](Get-JsonValue $manifest "hostPacketReplayFile" "")
$manifestClientPacketReplay = [string](Get-JsonValue $manifest "clientPacketReplayFile" "")
$replayPacketReplay = [string](Get-JsonValue $replay "packetReplayFile" "")
$replayHostPacketReplay = [string](Get-JsonValue $replay "hostPacketReplayFile" "")
$replayClientPacketReplay = [string](Get-JsonValue $replay "clientPacketReplayFile" "")
if ($manifestPacketReplay -eq "" -and $replayPacketReplay -ne "") { $manifestPacketReplay = $replayPacketReplay }
if ($manifestHostPacketReplay -eq "" -and $replayHostPacketReplay -ne "") { $manifestHostPacketReplay = $replayHostPacketReplay }
if ($manifestClientPacketReplay -eq "" -and $replayClientPacketReplay -ne "") { $manifestClientPacketReplay = $replayClientPacketReplay }

$commonPacketReplay = if ($PacketReplayFile -ne "") {
    Resolve-RepoPath $PacketReplayFile
} elseif ($manifestPacketReplay -ne "") {
    Resolve-ManifestPath $manifestPacketReplay $manifestDir -Required
} else {
    ""
}
$effectiveHostPacketReplay = if ($HostPacketReplayFile -ne "") {
    Resolve-RepoPath $HostPacketReplayFile
} elseif ($manifestHostPacketReplay -ne "") {
    Resolve-ManifestPath $manifestHostPacketReplay $manifestDir -Required
} else {
    $commonPacketReplay
}
$effectiveClientPacketReplay = if ($ClientPacketReplayFile -ne "") {
    Resolve-RepoPath $ClientPacketReplayFile
} elseif ($manifestClientPacketReplay -ne "") {
    Resolve-ManifestPath $manifestClientPacketReplay $manifestDir -Required
} else {
    $commonPacketReplay
}
if ($mode -eq "packet_replay" -and ($effectiveHostPacketReplay -eq "" -or $effectiveClientPacketReplay -eq "")) {
    throw "packet_replay mode requires packetReplayFile or host/client packet replay files"
}

$defaultHostRom = "roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-netaid.tmp.nds"
$defaultClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-netaid.tmp.nds"
$replayHostRom = [string](Get-JsonValue $replay "hostRom" "")
$replayClientRom = [string](Get-JsonValue $replay "clientRom" "")
$effectiveHostRom = if ($HostRom -ne "") {
    Resolve-RepoPath $HostRom
} elseif ($replayHostRom -ne "") {
    Resolve-ManifestPath $replayHostRom $manifestDir
} else {
    Resolve-RepoPath $defaultHostRom
}
$effectiveClientRom = if ($ClientRom -ne "") {
    Resolve-RepoPath $ClientRom
} elseif ($replayClientRom -ne "") {
    Resolve-ManifestPath $replayClientRom $manifestDir
} else {
    Resolve-RepoPath $defaultClientRom
}

$summary = Get-JsonValue $manifest "summary" $null
$summaryFrameEnd = [int](Get-JsonValue $summary "frameEnd" 0)
$replayFrames = [int](Get-JsonValue $replay "frames" 0)
$effectiveFrames = if ($Frames -gt 0) {
    $Frames
} elseif ($replayFrames -gt 0) {
    $replayFrames
} elseif ($summaryFrameEnd -gt 0) {
    $summaryFrameEnd
} else {
    2600
}

$stageScope = [int](Get-JsonValue $manifest "stageScope" 0)
if ($stageScope -ne 0) {
    throw "recording replay currently supports only stage 0 manifests: stageScope=$stageScope"
}

$matchSeed = [string](Get-JsonValue $replay "matchSeed" "")
$outputRoot = Resolve-OutputRoot $LogDir
$logRoot = [string]$outputRoot.Absolute
$logDirArg = [string]$outputRoot.Relative
$hostAIPlayLog = Join-Path (Join-Path $logRoot "host") "ai-playlog.jsonl"
$clientAIPlayLog = Join-Path (Join-Path $logRoot "client") "ai-playlog.jsonl"

$playLogText = [string](Get-JsonValue $manifest "playLog" "")
$effectiveVerifySide = if ($VerifySide -ne "") {
    $VerifySide
} elseif ($playLogText -match "(^|[\\/])client([\\/]|$)") {
    "client"
} else {
    "host"
}
$actualPlayLog = if ($effectiveVerifySide -eq "client") { $clientAIPlayLog } else { $hostAIPlayLog }
$effectiveMismatchReportJson = if ($MismatchReportJson -ne "") {
    Resolve-RepoPath $MismatchReportJson
} elseif ($ScanFrames) {
    Join-Path $logRoot "replay-mismatch.json"
} else {
    ""
}
$effectiveMismatchReportCsv = if ($MismatchReportCsv -ne "") {
    Resolve-RepoPath $MismatchReportCsv
} elseif ($ScanFrames) {
    Join-Path $logRoot "replay-mismatch.csv"
} else {
    ""
}

$splitScript = Join-Path $PSScriptRoot "run-nsmb-mvl-split-local-input-smoke.ps1"
$splitArgs = @(
    "-Frames", "$effectiveFrames",
    "-WaitTimeoutMs", "$WaitTimeoutMs",
    "-Exe", $Exe,
    "-HostRom", $effectiveHostRom,
    "-ClientRom", $effectiveClientRom,
    "-HostInputScript", $effectiveHostInput,
    "-ClientInputScript", $effectiveClientInput,
    "-MvlStage", "0",
    "-LogDir", $logDirArg,
    "-HostAIPlayLog", $hostAIPlayLog,
    "-ClientAIPlayLog", $clientAIPlayLog,
    "-AIPlayLogInterval", "$AIPlayLogInterval",
    "-AIPlayLogMaxObjects", "$AIPlayLogMaxObjects",
    "-NoGameStateTrace",
    "-SkipMovementProbe",
    "-NoDrawScreen",
    "-NoAudioSync"
)
if ($matchSeed -ne "") { $splitArgs += @("-MvlMatchSeed", $matchSeed) }
if ($GenerateMvlConfiguredRoms) { $splitArgs += "-GenerateMvlConfiguredRoms" }
if ($AllowJit) { $splitArgs += "-AllowJit" }
if ($mode -eq "packet_replay") {
    $splitArgs += @(
        "-HostPacketReplayFile", $effectiveHostPacketReplay,
        "-ClientPacketReplayFile", $effectiveClientPacketReplay
    )
}

$plan = [ordered]@{
    schema = "nsmb_mvl_ai_recording_replay_run_v1"
    recordingManifest = $manifestPath
    replayMode = $mode
    verifySide = $effectiveVerifySide
    expectedPlayLog = Resolve-ManifestPath $playLogText $manifestDir
    actualPlayLog = $actualPlayLog
    scanFrames = [bool]$ScanFrames
    mismatchReportJson = $effectiveMismatchReportJson
    mismatchReportCsv = $effectiveMismatchReportCsv
    logDir = $logRoot
    frames = $effectiveFrames
    stageScope = $stageScope
    hostInputScript = $effectiveHostInput
    clientInputScript = $effectiveClientInput
    hostPacketReplayFile = $effectiveHostPacketReplay
    clientPacketReplayFile = $effectiveClientPacketReplay
    hostRom = $effectiveHostRom
    clientRom = $effectiveClientRom
    splitScript = $splitScript
    splitArgs = $splitArgs
}

if ($DryRun) {
    $plan | ConvertTo-Json -Depth 8
    return
}

New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
$runManifest = Join-Path $logRoot "replay-run.json"
$plan | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $runManifest -Encoding UTF8

& $splitScript @splitArgs
if (-not (Test-Path -LiteralPath $actualPlayLog)) {
    throw "replay did not create AI play log: $actualPlayLog"
}

$verifyScript = Join-Path $PSScriptRoot "nsmb_mvl_ai_verify_replay.py"
$verifyArgs = @(
    $manifestPath,
    $actualPlayLog,
    "--position-tolerance", "$PositionTolerance"
)
if ($IgnoreHash) { $verifyArgs += "--ignore-hash" }
if ($IgnoreObjectCounts) { $verifyArgs += "--ignore-object-counts" }
if ($CheckCategoryCounts) { $verifyArgs += "--check-category-counts" }
if ($ScanFrames) { $verifyArgs += "--scan-frames" }
if ($effectiveMismatchReportJson -ne "") { $verifyArgs += @("--mismatch-report-json", $effectiveMismatchReportJson) }
if ($effectiveMismatchReportCsv -ne "") { $verifyArgs += @("--mismatch-report-csv", $effectiveMismatchReportCsv) }
if ($MaxMismatchFrames -ne 20) { $verifyArgs += @("--max-mismatch-frames", "$MaxMismatchFrames") }
if ($CheckpointInterval -gt 0) {
    $verifyArgs += @(
        "--checkpoint-interval", "$CheckpointInterval",
        "--checkpoint-start-frame", "$CheckpointStartFrame"
    )
    if ($MaxCheckpoints -gt 0) {
        $verifyArgs += @("--max-checkpoints", "$MaxCheckpoints")
    }
}

python $verifyScript @verifyArgs
if ($LASTEXITCODE -ne 0) {
    throw "replay verification failed: $LASTEXITCODE"
}

Write-Host "NSMB MvL recording replay verified: manifest=$manifestPath actual=$actualPlayLog log=$logRoot"
