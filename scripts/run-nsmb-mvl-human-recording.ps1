param(
    [int]$Frames = 999999,
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-netaid.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-netaid.tmp.nds",
    [string]$InputScript = "tests\nsmb_us_direct_mvl_minimal_bootstrap.inputs",
    [string]$LogDir = "",
    [int]$AIPlayLogInterval = 1,
    [int]$AIPlayLogFlushInterval = 60,
    [int]$AIPlayLogMaxObjects = 128,
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
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

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
$sessionPath = Join-Path $logRoot "recording-session.json"
$singleWindow = -not $DualWindow
if ($singleWindow) {
    New-Item -ItemType Directory -Force -Path $clientLog | Out-Null
} else {
    New-Item -ItemType Directory -Force -Path $hostLog, $clientLog | Out-Null
}

$manualScript = Join-Path $PSScriptRoot "run-nsmb-mvl-manual-local.ps1"
$manualArgs = @{
    Frames = $Frames
    Exe = $Exe
    HostRom = $HostRom
    ClientRom = $ClientRom
    InputScript = $InputScript
    LogDir = $LogDir
    MvlStage = 0
    ClientAIPlayLog = $clientAIPlayLog
    AIPlayLogInterval = $AIPlayLogInterval
    AIPlayLogFlushInterval = $AIPlayLogFlushInterval
    AIPlayLogMaxObjects = $AIPlayLogMaxObjects
    NetworkPumpThread = $true
    NetworkPumpSleepUs = 50
}
if ($singleWindow) {
    $manualArgs.ClientOnly = $true
    $manualArgs.InputDelayFrames = 0
} else {
    $manualArgs.HostAIPlayLog = $hostAIPlayLog
}
$packetCaptureEnabled = (-not $NoPacketCapture) -and (-not $singleWindow)
if ($packetCaptureEnabled) { $manualArgs.PacketCapture = $true }
if ($GenerateMvlConfiguredRoms) { $manualArgs.GenerateMvlConfiguredRoms = $true }
if ($MvlMatchSeed -ne "") { $manualArgs.MvlMatchSeed = $MvlMatchSeed }
if ($AllowJit -or -not $NoJit) { $manualArgs.AllowJit = $true }
if (-not $singleWindow) {
    if ($HumanSide -eq "client") { $manualArgs.NeutralizeHostInput = $true }
    if ($HumanSide -eq "host") { $manualArgs.NeutralizeClientInput = $true }
}

$hostManifest = Join-Path $hostLog "recording.json"
$clientManifest = Join-Path $clientLog "recording.json"
$indexPath = Join-Path $logRoot "recordings-index.json"
$auditPath = Join-Path $logRoot "recording-audit.json"
$visualStateAuditPath = Join-Path $logRoot "visual-state-audit.json"
$hostPacketCapture = Join-Path $hostLog "host.packet-capture.csv"
$clientPacketCapture = Join-Path $clientLog "client.packet-capture.csv"
$packetReplay = Join-Path $logRoot "packet-replay.csv"
$hostPlayer = if ($HumanSide -eq "client") { 0 } else { 0 }
$clientPlayer = if ($HumanSide -eq "host") { 1 } else { 1 }
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
    $postCommands += @(
        "python scripts\nsmb_mvl_ai_create_recording_manifest.py `"$clientAIPlayLog`" `"$clientManifest`" --kind human --player 1 --label-source player --stage 0 --log-dir `"$clientLog`" --frames $Frames --client-input-script `"$InputScript`" --client-rom `"$ClientRom`"$matchSeedManifestArg$scenarioManifestArg",
        "python scripts\nsmb_mvl_ai_make_recordings_index.py `"$indexPath`" `"$clientManifest`" --stage 0",
        "python scripts\nsmb_mvl_ai_build_dataset.py `"$indexPath`" `"$logRoot\ai-dataset-player1.csv`" --player 1 --label-source player --require-player-found",
        "python scripts\nsmb_mvl_ai_audit_visual_state.py `"$clientAIPlayLog`" --output `"$visualStateAuditPath`""
    )
} else {
    $postCommands += @(
        "python scripts\nsmb_mvl_ai_create_recording_manifest.py `"$hostAIPlayLog`" `"$hostManifest`" --kind human --player $hostPlayer --label-source player --stage 0 --log-dir `"$hostLog`" --frames $Frames --host-input-script `"$InputScript`" --client-input-script `"$InputScript`" --host-rom `"$HostRom`" --client-rom `"$ClientRom`"$matchSeedManifestArg$scenarioManifestArg$packetReplayArgs",
        "python scripts\nsmb_mvl_ai_create_recording_manifest.py `"$clientAIPlayLog`" `"$clientManifest`" --kind human --player $clientPlayer --label-source player --stage 0 --log-dir `"$clientLog`" --frames $Frames --host-input-script `"$InputScript`" --client-input-script `"$InputScript`" --host-rom `"$HostRom`" --client-rom `"$ClientRom`"$matchSeedManifestArg$scenarioManifestArg$packetReplayArgs",
        "python scripts\nsmb_mvl_ai_make_recordings_index.py `"$indexPath`" `"$hostManifest`" `"$clientManifest`" --stage 0",
        "python scripts\nsmb_mvl_ai_build_dataset.py `"$indexPath`" `"$logRoot\ai-dataset-player1.csv`" --player 1 --label-source player --require-player-found",
        "python scripts\nsmb_mvl_ai_audit_visual_state.py `"$hostAIPlayLog`" `"$clientAIPlayLog`" --output `"$visualStateAuditPath`""
    )
}
$auditCommand = "python scripts\nsmb_mvl_ai_audit_recordings.py `"$indexPath`" --stage 0 --min-rows 1 --min-gameplay-rows 1 --min-player-found-ratio 0.5 --min-label-ratio 0.5 --min-nonzero-label-rows 1 --output `"$auditPath`""
if ($packetCaptureEnabled) {
    $auditCommand += " --require-packet-replay"
}
$postCommands += $auditCommand

$session = [ordered]@{
    schema = "nsmb_mvl_ai_human_recording_session_v1"
    stageScope = 0
    recordingMode = $(if ($singleWindow) { "single_client_authoritative" } else { "dual_window_netplay_experimental" })
    humanSide = $HumanSide
    neutralizeHostInput = ((-not $singleWindow) -and ($HumanSide -eq "client"))
    neutralizeClientInput = ((-not $singleWindow) -and ($HumanSide -eq "host"))
    allowJit = ($AllowJit -or -not $NoJit)
    noJit = [bool]$NoJit
    logDir = $LogDir
    hostAIPlayLog = $(if ($singleWindow) { "" } else { $hostAIPlayLog })
    clientAIPlayLog = $clientAIPlayLog
    aiPlayLogInterval = $AIPlayLogInterval
    aiPlayLogFlushInterval = $AIPlayLogFlushInterval
    aiPlayLogMaxObjects = $AIPlayLogMaxObjects
    scenario = $Scenario
    frames = $Frames
    inputScript = $InputScript
    hostRom = $HostRom
    clientRom = $ClientRom
    mvlMatchSeed = $MvlMatchSeed
    packetCapture = $packetCaptureEnabled
    hostPacketCapture = $(if ($packetCaptureEnabled) { $hostPacketCapture } else { "" })
    clientPacketCapture = $(if ($packetCaptureEnabled) { $clientPacketCapture } else { "" })
    packetReplay = $(if ($packetCaptureEnabled) { $packetReplay } else { "" })
    audit = $auditPath
    visualStateAudit = $visualStateAuditPath
    postCommands = $postCommands
}
$session | ConvertTo-Json -Depth 6 | Set-Content -Path $sessionPath -Encoding UTF8

Write-Host "Starting stage 0 human recording. log=$LogDir mode=$($session.recordingMode)"
if ($singleWindow) {
    Write-Host "Single client window is authoritative; dual-window host/client sync is not used for this recording."
}
Write-Host "After closing melonDS, run: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run-nsmb-mvl-recording-postcommands.ps1 -Session `"$sessionPath`""
if ($DryRun) {
    $session | ConvertTo-Json -Depth 8
    return
}
& $manualScript @manualArgs
