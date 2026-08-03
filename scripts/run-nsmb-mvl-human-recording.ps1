param(
    [int]$Frames = 999999,
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-netaid.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-netaid.tmp.nds",
    [switch]$CopyRomToLog,
    [string]$InputScript = "tests\nsmb_us_direct_mvl_minimal_bootstrap.inputs",
    [string]$LogDir = "",
    [int]$AIPlayLogInterval = 1,
    [int]$AIPlayLogFlushInterval = 60,
    [int]$AIPlayLogMaxObjects = 128,
    [int]$ScreenshotInterval = 0,
    [switch]$SoftwareRenderer = $true,
    [string]$Scenario = "",
    [ValidateSet("host", "client", "both")]
    [string]$HumanSide = "client",
    [switch]$NoPacketCapture,
    [int]$PacketReplayFirstFrame = 0,
    [int]$PacketReplayLastFrame = 0,
    [switch]$GenerateMvlConfiguredRoms,
    [string]$MvlMatchSeed = "",
    [switch]$AllowJit,
    [switch]$NoJit,
    [switch]$DualWindow,
    [switch]$SingleWindow,
    [switch]$ForcePlayerPowerups,
    [int]$ForcePlayerPowerupsStartFrame = 0,
    [int]$ForcePlayerPowerupsEndFrame = 0,
    [int]$ForcePlayerPowerup0 = 0,
    [int]$ForcePlayerPowerup1 = 0,
    [switch]$ForcePlayerStarCounters,
    [int]$ForcePlayerStarCountersStartFrame = 900,
    [int]$ForcePlayerStarCountersEndFrame = 1500,
    [int]$ForcePlayerBattleStars0 = 0,
    [int]$ForcePlayerBattleStars1 = 0,
    [int]$ForcePlayerDisplayedStars0 = 0,
    [int]$ForcePlayerDisplayedStars1 = 0,
    [int]$ForcePlayerCollectedStars0 = 0,
    [int]$ForcePlayerCollectedStars1 = 0,
    [ValidateSet("none", "rule")]
    [string]$OpponentAI = "none",
    [ValidateSet("", "MARIO", "LUIGI", "0", "1")]
    [string]$OpponentAIPlayer = "",
    [switch]$AuditPlayLog,
    [switch]$NoGzipPlayLog,
    [switch]$BuildLegacyDataset,
    [switch]$NoAutoPostprocess,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

if ($DualWindow -and $SingleWindow) {
    throw "-DualWindow and -SingleWindow cannot be used together"
}
if ($ScreenshotInterval -gt 0 -and -not $SoftwareRenderer) {
    throw "-ScreenshotInterval requires -SoftwareRenderer because the OpenGL compute renderer does not produce test screenshots reliably"
}

$repoRoot = Split-Path -Parent $PSScriptRoot
if ($LogDir -eq "") {
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $LogDir = "logs\nsmb-mvl-human-recording-stage0-$timestamp"
}
if ($MvlMatchSeed -eq "") {
    $seedBytes = [byte[]]::new(4)
    $rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    try {
        $rng.GetBytes($seedBytes)
    } finally {
        $rng.Dispose()
    }
    $MvlMatchSeed = "0x$('{0:x8}' -f [BitConverter]::ToUInt32($seedBytes, 0))"
}

$logRoot = Join-Path $repoRoot $LogDir
$hostLog = Join-Path $logRoot "host"
$clientLog = Join-Path $logRoot "client"
$hostAIPlayLog = Join-Path $hostLog "ai-playlog.jsonl"
$clientAIPlayLog = Join-Path $clientLog "ai-playlog.jsonl"
$hostAIObservationV3Log = Join-Path $hostLog "ai-observations-v3.jsonl"
$clientAIObservationV3Log = Join-Path $clientLog "ai-observations-v3.jsonl"
$sessionPath = Join-Path $logRoot "recording-session.json"
$singleWindow = [bool]$SingleWindow
if ($singleWindow) {
    New-Item -ItemType Directory -Force -Path $clientLog | Out-Null
} else {
    New-Item -ItemType Directory -Force -Path $hostLog, $clientLog | Out-Null
}
if ($BuildLegacyDataset -and -not $AuditPlayLog) {
    throw "-BuildLegacyDataset requires -AuditPlayLog because the legacy CSV path is built from ai-playlog v1 manifests."
}

$manualScript = Join-Path $PSScriptRoot "run-nsmb-mvl-manual-local.ps1"
$manualArgs = @{
    Frames = $Frames
    Exe = $Exe
    HostRom = $HostRom
    ClientRom = $ClientRom
    CopyRomToLog = [bool]$CopyRomToLog
    InputScript = $InputScript
    LogDir = $LogDir
    MvlStage = 0
    ClientAIObservationV3Log = $clientAIObservationV3Log
    AIPlayLogInterval = $AIPlayLogInterval
    AIPlayLogFlushInterval = $AIPlayLogFlushInterval
    AIPlayLogMaxObjects = $AIPlayLogMaxObjects
    ScreenshotInterval = $ScreenshotInterval
    NetworkPumpThread = $true
    NetworkPumpSleepUs = 50
    SkipFrameLimitCheck = $true
    Wait = (-not $NoAutoPostprocess)
}
if ($AuditPlayLog) {
    $manualArgs.ClientAIPlayLog = $clientAIPlayLog
}
if ($singleWindow) {
    $manualArgs.ClientOnly = $true
    $manualArgs.InputDelayFrames = 0
} else {
    $manualArgs.HostAIObservationV3Log = $hostAIObservationV3Log
    if ($AuditPlayLog) {
        $manualArgs.HostAIPlayLog = $hostAIPlayLog
    }
}
$packetCaptureEnabled = (-not $NoPacketCapture) -and (-not $singleWindow)
if ($packetCaptureEnabled) { $manualArgs.PacketCapture = $true }
if ($GenerateMvlConfiguredRoms) { $manualArgs.GenerateMvlConfiguredRoms = $true }
if ($MvlMatchSeed -ne "") { $manualArgs.MvlMatchSeed = $MvlMatchSeed }
if ($AllowJit -or -not $NoJit) { $manualArgs.AllowJit = $true }
$manualArgs.SoftwareRenderer = [bool]$SoftwareRenderer
if ($ForcePlayerPowerups) {
    $manualArgs.ForcePlayerPowerups = $true
    $manualArgs.ForcePlayerPowerupsStartFrame = $ForcePlayerPowerupsStartFrame
    $manualArgs.ForcePlayerPowerupsEndFrame = $ForcePlayerPowerupsEndFrame
    $manualArgs.ForcePlayerPowerup0 = $ForcePlayerPowerup0
    $manualArgs.ForcePlayerPowerup1 = $ForcePlayerPowerup1
}
if ($ForcePlayerStarCounters) {
    $manualArgs.ForcePlayerStarCounters = $true
    $manualArgs.ForcePlayerStarCountersStartFrame = $ForcePlayerStarCountersStartFrame
    $manualArgs.ForcePlayerStarCountersEndFrame = $ForcePlayerStarCountersEndFrame
    $manualArgs.ForcePlayerBattleStars0 = $ForcePlayerBattleStars0
    $manualArgs.ForcePlayerBattleStars1 = $ForcePlayerBattleStars1
    $manualArgs.ForcePlayerDisplayedStars0 = $ForcePlayerDisplayedStars0
    $manualArgs.ForcePlayerDisplayedStars1 = $ForcePlayerDisplayedStars1
    $manualArgs.ForcePlayerCollectedStars0 = $ForcePlayerCollectedStars0
    $manualArgs.ForcePlayerCollectedStars1 = $ForcePlayerCollectedStars1
}
if (-not $singleWindow) {
    if ($HumanSide -eq "client") { $manualArgs.NeutralizeHostInput = $true }
    if ($HumanSide -eq "host") { $manualArgs.NeutralizeClientInput = $true }
}

$hostManifest = Join-Path $hostLog "recording.json"
$clientManifest = Join-Path $clientLog "recording.json"
$indexPath = Join-Path $logRoot "recordings-index.json"
$auditPath = Join-Path $logRoot "recording-audit.json"
$visualStateAuditPath = Join-Path $logRoot "visual-state-audit.json"
$fireballAuditPath = Join-Path $logRoot "fireball-audit.json"
$datasetTerrainAuditPath = Join-Path $logRoot "dataset-terrain-audit.json"
$compactDatasetPlayer1Path = Join-Path $logRoot "ai-compact-dataset-player1.npz"
$hostPacketCapture = Join-Path $hostLog "host.packet-capture.csv"
$clientPacketCapture = Join-Path $clientLog "client.packet-capture.csv"
$packetReplay = Join-Path $logRoot "packet-replay.csv"
$hostPlayer = if ($HumanSide -eq "client") { 0 } else { 0 }
$clientPlayer = if ($HumanSide -eq "host") { 1 } else { 1 }
$resolvedOpponentAIPlayer = if ($OpponentAIPlayer -ne "") {
    $OpponentAIPlayer
} elseif ($HumanSide -eq "client") {
    "MARIO"
} else {
    "LUIGI"
}
$savedOpponentAIEnv = @{
    MELONDS_NSML_RULE_AI = [Environment]::GetEnvironmentVariable("MELONDS_NSML_RULE_AI", "Process")
    MELONDS_NSML_RULE_AI_PLAYER = [Environment]::GetEnvironmentVariable("MELONDS_NSML_RULE_AI_PLAYER", "Process")
}
if ($OpponentAI -eq "rule") {
    $env:MELONDS_NSML_RULE_AI = "1"
    $env:MELONDS_NSML_RULE_AI_PLAYER = $resolvedOpponentAIPlayer
} else {
    Remove-Item Env:\MELONDS_NSML_RULE_AI -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_RULE_AI_PLAYER -ErrorAction SilentlyContinue
}
$postCommands = @()
if ($packetCaptureEnabled) {
    $convertCommand = "powershell -NoProfile -ExecutionPolicy Bypass -File scripts\convert-nsmb-packet-capture-to-replay.ps1 -HostCapture `"$hostPacketCapture`" -ClientCapture `"$clientPacketCapture`" -Output `"$packetReplay`""
    if ($PacketReplayFirstFrame -gt 0) { $convertCommand += " -FirstFrame $PacketReplayFirstFrame" }
    if ($PacketReplayLastFrame -gt 0) { $convertCommand += " -LastFrame $PacketReplayLastFrame" }
    $postCommands += $convertCommand
}
$packetReplayArgs = if (-not $packetCaptureEnabled) {
    ""
} else {
    " --replay-mode packet_replay --packet-replay-file `"$packetReplay`" --host-packet-capture `"$hostPacketCapture`" --client-packet-capture `"$clientPacketCapture`""
}
$matchSeedManifestArg = if ($MvlMatchSeed -ne "") {
    " --match-seed `"$MvlMatchSeed`""
} else {
    ""
}
$scenarioManifestArg = if ($Scenario -ne "") {
    " --scenario `"$Scenario`""
} else {
    ""
}
if ($singleWindow) {
    $recordingPostCommands = @(
        "python scripts\nsmb_mvl_ai_build_compact_dataset_v3.py `"$clientAIObservationV3Log`" `"$compactDatasetPlayer1Path`" --player 1 --require-player-found"
    )
    if ($AuditPlayLog) {
        $recordingPostCommands = @(
        "python scripts\nsmb_mvl_ai_create_recording_manifest.py `"$clientAIPlayLog`" `"$clientManifest`" --kind human --player 1 --label-source player --stage 0 --log-dir `"$clientLog`" --frames $Frames --client-input-script `"$InputScript`" --client-rom `"$ClientRom`"$matchSeedManifestArg$scenarioManifestArg",
        "python scripts\nsmb_mvl_ai_make_recordings_index.py `"$indexPath`" `"$clientManifest`" --stage 0",
        "python scripts\nsmb_mvl_ai_build_compact_dataset_v3.py `"$clientAIObservationV3Log`" `"$compactDatasetPlayer1Path`" --player 1 --require-player-found",
        "python scripts\nsmb_mvl_ai_audit_visual_state.py `"$clientAIPlayLog`" --output `"$visualStateAuditPath`"",
        "python scripts\nsmb_mvl_ai_audit_fireballs.py `"$clientAIPlayLog`" --output `"$fireballAuditPath`""
        )
        if ($BuildLegacyDataset) {
            $recordingPostCommands = @(
                $recordingPostCommands[0],
                $recordingPostCommands[1],
                "python scripts\nsmb_mvl_ai_build_dataset.py `"$indexPath`" `"$logRoot\ai-dataset-player1.csv`" --player 1 --label-source player --require-player-found",
                $recordingPostCommands[2],
                "python scripts\nsmb_mvl_ai_audit_dataset_terrain.py `"$logRoot\ai-dataset-player1.csv`" --output `"$datasetTerrainAuditPath`"",
                $recordingPostCommands[3],
                $recordingPostCommands[4]
            )
        }
    }
    $postCommands += $recordingPostCommands
} else {
    $recordingPostCommands = @(
        "python scripts\nsmb_mvl_ai_build_compact_dataset_v3.py `"$clientAIObservationV3Log`" `"$compactDatasetPlayer1Path`" --player 1 --require-player-found"
    )
    if ($AuditPlayLog) {
        $recordingPostCommands = @(
        "python scripts\nsmb_mvl_ai_create_recording_manifest.py `"$hostAIPlayLog`" `"$hostManifest`" --kind human --player $hostPlayer --label-source player --stage 0 --log-dir `"$hostLog`" --frames $Frames --host-input-script `"$InputScript`" --client-input-script `"$InputScript`" --host-rom `"$HostRom`" --client-rom `"$ClientRom`"$matchSeedManifestArg$scenarioManifestArg$packetReplayArgs",
        "python scripts\nsmb_mvl_ai_create_recording_manifest.py `"$clientAIPlayLog`" `"$clientManifest`" --kind human --player $clientPlayer --label-source player --stage 0 --log-dir `"$clientLog`" --frames $Frames --host-input-script `"$InputScript`" --client-input-script `"$InputScript`" --host-rom `"$HostRom`" --client-rom `"$ClientRom`"$matchSeedManifestArg$scenarioManifestArg$packetReplayArgs",
        "python scripts\nsmb_mvl_ai_make_recordings_index.py `"$indexPath`" `"$hostManifest`" `"$clientManifest`" --stage 0",
        "python scripts\nsmb_mvl_ai_build_compact_dataset_v3.py `"$clientAIObservationV3Log`" `"$compactDatasetPlayer1Path`" --player 1 --require-player-found",
        "python scripts\nsmb_mvl_ai_audit_visual_state.py `"$hostAIPlayLog`" `"$clientAIPlayLog`" --output `"$visualStateAuditPath`"",
        "python scripts\nsmb_mvl_ai_audit_fireballs.py `"$hostAIPlayLog`" `"$clientAIPlayLog`" --output `"$fireballAuditPath`""
        )
        if ($BuildLegacyDataset) {
            $recordingPostCommands = @(
                $recordingPostCommands[0],
                $recordingPostCommands[1],
                $recordingPostCommands[2],
                "python scripts\nsmb_mvl_ai_build_dataset.py `"$indexPath`" `"$logRoot\ai-dataset-player1.csv`" --player 1 --label-source player --require-player-found",
                $recordingPostCommands[3],
                "python scripts\nsmb_mvl_ai_audit_dataset_terrain.py `"$logRoot\ai-dataset-player1.csv`" --output `"$datasetTerrainAuditPath`"",
                $recordingPostCommands[4],
                $recordingPostCommands[5]
            )
        }
    }
    $postCommands += $recordingPostCommands
}
if ($AuditPlayLog) {
    $auditCommand = "python scripts\nsmb_mvl_ai_audit_recordings.py `"$indexPath`" --stage 0 --min-rows 1 --min-gameplay-rows 1 --min-player-found-ratio 0.5 --min-label-ratio 0.5 --min-nonzero-label-rows 1 --output `"$auditPath`""
    if ($packetCaptureEnabled) {
        $auditCommand += " --require-packet-replay"
    }
    $postCommands += $auditCommand
}
if (-not $NoGzipPlayLog) {
    $postCommands += "powershell -NoProfile -ExecutionPolicy Bypass -File scripts\compress-nsmb-mvl-ai-playlogs.ps1 -Session `"$sessionPath`""
}

$session = [ordered]@{
    schema = "nsmb_mvl_ai_human_recording_session_v1"
    stageScope = 0
    recordingMode = $(if ($singleWindow) { "single_client_authoritative" } else { "dual_window_netplay" })
    humanSide = $HumanSide
    neutralizeHostInput = ((-not $singleWindow) -and ($HumanSide -eq "client"))
    neutralizeClientInput = ((-not $singleWindow) -and ($HumanSide -eq "host"))
    allowJit = ($AllowJit -or -not $NoJit)
    noJit = [bool]$NoJit
    softwareRenderer = [bool]$SoftwareRenderer
    auditPlayLog = [bool]$AuditPlayLog
    copyRomToLog = [bool]$CopyRomToLog
    logDir = $LogDir
    hostAIPlayLog = $(if ((-not $singleWindow) -and $AuditPlayLog) { $hostAIPlayLog } else { "" })
    clientAIPlayLog = $(if ($AuditPlayLog) { $clientAIPlayLog } else { "" })
    hostAIObservationV3Log = $(if ($singleWindow) { "" } else { $hostAIObservationV3Log })
    clientAIObservationV3Log = $clientAIObservationV3Log
    aiPlayLogInterval = $AIPlayLogInterval
    aiPlayLogFlushInterval = $AIPlayLogFlushInterval
    aiPlayLogMaxObjects = $AIPlayLogMaxObjects
    screenshotInterval = $ScreenshotInterval
    gzipPlayLog = (-not $NoGzipPlayLog)
    buildLegacyDataset = [bool]$BuildLegacyDataset
    scenario = $Scenario
    opponentAI = $OpponentAI
    opponentAIPlayer = $(if ($OpponentAI -eq "rule") { $resolvedOpponentAIPlayer } else { "" })
    forcedPlayerStarCounters = [ordered]@{
        enabled = [bool]$ForcePlayerStarCounters
        startFrame = $ForcePlayerStarCountersStartFrame
        endFrame = $ForcePlayerStarCountersEndFrame
        battleStars0 = $ForcePlayerBattleStars0
        battleStars1 = $ForcePlayerBattleStars1
        displayedStars0 = $ForcePlayerDisplayedStars0
        displayedStars1 = $ForcePlayerDisplayedStars1
        collectedStars0 = $ForcePlayerCollectedStars0
        collectedStars1 = $ForcePlayerCollectedStars1
    }
    frames = $Frames
    inputScript = $InputScript
    hostRom = $HostRom
    clientRom = $ClientRom
    mvlMatchSeed = $MvlMatchSeed
    packetCapture = $packetCaptureEnabled
    hostPacketCapture = $(if ($packetCaptureEnabled) { $hostPacketCapture } else { "" })
    clientPacketCapture = $(if ($packetCaptureEnabled) { $clientPacketCapture } else { "" })
    packetReplay = $(if ($packetCaptureEnabled) { $packetReplay } else { "" })
    audit = $(if ($AuditPlayLog) { $auditPath } else { "" })
    visualStateAudit = $(if ($AuditPlayLog) { $visualStateAuditPath } else { "" })
    fireballAudit = $(if ($AuditPlayLog) { $fireballAuditPath } else { "" })
    datasetTerrainAudit = $(if ($BuildLegacyDataset) { $datasetTerrainAuditPath } else { "" })
    compactDatasetPlayer1 = $compactDatasetPlayer1Path
    postCommands = $postCommands
}
ConvertTo-Json -InputObject $session -Depth 6 | Set-Content -Path $sessionPath -Encoding UTF8

Write-Host "Starting stage 0 human recording. log=$LogDir mode=$($session.recordingMode)"
if ($singleWindow) {
    Write-Host "Single client window is authoritative; dual-window host/client sync is not used for this recording."
}
if ($NoAutoPostprocess) {
    Write-Host "After closing melonDS, run: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run-nsmb-mvl-recording-postcommands.ps1 -Session `"$sessionPath`""
} else {
    Write-Host "After melonDS closes, postCommands will run automatically. session=$sessionPath"
}
if ($DryRun) {
    ConvertTo-Json -InputObject $session -Depth 8
    foreach ($name in $savedOpponentAIEnv.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedOpponentAIEnv[$name], "Process")
    }
    return
}
try {
    & $manualScript @manualArgs
    if (-not $NoAutoPostprocess) {
        Write-Host "melonDS exited; running recording postCommands."
        & (Join-Path $PSScriptRoot "run-nsmb-mvl-recording-postcommands.ps1") -Session $sessionPath
    }
} finally {
    foreach ($name in $savedOpponentAIEnv.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedOpponentAIEnv[$name], "Process")
    }
}
