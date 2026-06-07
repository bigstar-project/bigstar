param(
    [int]$Frames = 999999,
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-netaid.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-netaid.tmp.nds",
    [string]$InputScript = "tests\nsmb_us_direct_mvl_minimal_bootstrap.inputs",
    [string]$LogDir = "",
    [int]$AIPlayLogInterval = 1,
    [int]$AIPlayLogMaxObjects = 128,
    [ValidateSet("host", "client", "both")]
    [string]$HumanSide = "host",
    [switch]$GenerateMvlConfiguredRoms,
    [string]$MvlMatchSeed = "",
    [switch]$AllowJit
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if ($LogDir -eq "") {
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $LogDir = "logs\nsmb-mvl-human-recording-stage0-$timestamp"
}

$logRoot = Join-Path $repoRoot $LogDir
$hostLog = Join-Path $logRoot "host"
$clientLog = Join-Path $logRoot "client"
$hostAIPlayLog = Join-Path $hostLog "ai-playlog.jsonl"
$clientAIPlayLog = Join-Path $clientLog "ai-playlog.jsonl"
$sessionPath = Join-Path $logRoot "recording-session.json"
New-Item -ItemType Directory -Force -Path $hostLog, $clientLog | Out-Null

$manualScript = Join-Path $PSScriptRoot "run-nsmb-mvl-manual-local.ps1"
$manualArgs = @(
    "-Frames", "$Frames",
    "-Exe", $Exe,
    "-HostRom", $HostRom,
    "-ClientRom", $ClientRom,
    "-InputScript", $InputScript,
    "-LogDir", $LogDir,
    "-MvlStage", "0",
    "-HostAIPlayLog", $hostAIPlayLog,
    "-ClientAIPlayLog", $clientAIPlayLog,
    "-AIPlayLogInterval", "$AIPlayLogInterval",
    "-AIPlayLogMaxObjects", "$AIPlayLogMaxObjects"
)
if ($GenerateMvlConfiguredRoms) { $manualArgs += "-GenerateMvlConfiguredRoms" }
if ($MvlMatchSeed -ne "") { $manualArgs += @("-MvlMatchSeed", $MvlMatchSeed) }
if ($AllowJit) { $manualArgs += "-AllowJit" }

$hostManifest = Join-Path $hostLog "recording.json"
$clientManifest = Join-Path $clientLog "recording.json"
$indexPath = Join-Path $logRoot "recordings-index.json"
$hostPlayer = if ($HumanSide -eq "client") { 0 } else { 0 }
$clientPlayer = if ($HumanSide -eq "host") { 1 } else { 1 }
$postCommands = @(
    "python scripts\nsmb_mvl_ai_create_recording_manifest.py `"$hostAIPlayLog`" `"$hostManifest`" --kind human --player $hostPlayer --label-source player --stage 0 --log-dir `"$hostLog`" --frames $Frames --host-input-script `"$InputScript`" --client-input-script `"$InputScript`" --host-rom `"$HostRom`" --client-rom `"$ClientRom`" --match-seed `"$MvlMatchSeed`"",
    "python scripts\nsmb_mvl_ai_create_recording_manifest.py `"$clientAIPlayLog`" `"$clientManifest`" --kind human --player $clientPlayer --label-source player --stage 0 --log-dir `"$clientLog`" --frames $Frames --host-input-script `"$InputScript`" --client-input-script `"$InputScript`" --host-rom `"$HostRom`" --client-rom `"$ClientRom`" --match-seed `"$MvlMatchSeed`"",
    "python scripts\nsmb_mvl_ai_make_recordings_index.py `"$indexPath`" `"$hostManifest`" `"$clientManifest`" --stage 0",
    "python scripts\nsmb_mvl_ai_build_dataset.py `"$indexPath`" `"$logRoot\ai-dataset-player1.csv`" --player 1 --label-source player --require-player-found"
)

$session = [ordered]@{
    schema = "nsmb_mvl_ai_human_recording_session_v1"
    stageScope = 0
    humanSide = $HumanSide
    logDir = $LogDir
    hostAIPlayLog = $hostAIPlayLog
    clientAIPlayLog = $clientAIPlayLog
    aiPlayLogInterval = $AIPlayLogInterval
    aiPlayLogMaxObjects = $AIPlayLogMaxObjects
    frames = $Frames
    inputScript = $InputScript
    hostRom = $HostRom
    clientRom = $ClientRom
    mvlMatchSeed = $MvlMatchSeed
    postCommands = $postCommands
}
$session | ConvertTo-Json -Depth 6 | Set-Content -Path $sessionPath -Encoding UTF8

Write-Host "Starting stage 0 human recording. log=$LogDir"
Write-Host "After closing melonDS, run the commands in $sessionPath to create manifests and dataset."
& $manualScript @manualArgs
