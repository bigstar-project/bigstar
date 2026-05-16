param(
    [int]$Frames = 4200,
    [string]$Exe = "build\debug-windows-x86_64\melonDS.exe",
    [string]$Rom = "roms\nsmb.nds",
    [string]$InputScript = "tests\nsmb_mario_vs_luigi.inputs",
    [string]$LogDir = "logs",
    [switch]$GameStateTrace,
    [int]$GameStateTraceInterval = 60,
    [switch]$GameStateTraceExtended,
    [string]$RamDumpFrames = "",
    [int]$RamDumpInterval = 0
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stdout = Join-Path $LogDir "nsmb-mvl-route.stdout.txt"
$hashLog = Join-Path $LogDir "nsmb-mvl-route.hash.csv"
$gameStateTracePath = Join-Path $LogDir "nsmb-mvl-route.game-state.csv"
$screenDir = Join-Path $LogDir "screens-mvl-route"
$ramDumpDir = Join-Path $LogDir "ram-mvl-route"
Remove-Item -Force $stdout, $hashLog, $gameStateTracePath -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $screenDir -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $ramDumpDir -ErrorAction SilentlyContinue

$env:MELONDS_NSML_TEST = "1"
$env:MELONDS_NSML_TEST_INSTANCES = "2"
$env:MELONDS_NSML_TEST_FRAMES = "$Frames"
$env:MELONDS_NSML_INPUT_SCRIPT = (Resolve-Path $InputScript).Path
$env:MELONDS_NSML_HASH_LOG = (Join-Path (Resolve-Path $LogDir).Path "nsmb-mvl-route.hash.csv")
$env:MELONDS_NSML_HASH_INTERVAL = "300"
$env:MELONDS_NSML_SCREENSHOT_DIR = (Join-Path (Resolve-Path $LogDir).Path "screens-mvl-route")
$env:MELONDS_NSML_SCREENSHOT_INTERVAL = "120"
if ($GameStateTrace) {
    $env:MELONDS_NSML_GAME_STATE_TRACE = (Join-Path (Resolve-Path $LogDir).Path "nsmb-mvl-route.game-state.csv")
    $env:MELONDS_NSML_GAME_STATE_TRACE_INTERVAL = "$GameStateTraceInterval"
    if ($GameStateTraceExtended) {
        $env:MELONDS_NSML_GAME_STATE_TRACE_EXTENDED = "1"
    } else {
        Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE_EXTENDED -ErrorAction SilentlyContinue
    }
} else {
    Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE_INTERVAL -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE_EXTENDED -ErrorAction SilentlyContinue
}
if ($RamDumpFrames -or $RamDumpInterval -gt 0) {
    $env:MELONDS_NSML_RAM_DUMP_DIR = (Join-Path (Resolve-Path $LogDir).Path "ram-mvl-route")
    $env:MELONDS_NSML_RAM_DUMP_FRAMES = $RamDumpFrames
    $env:MELONDS_NSML_RAM_DUMP_INTERVAL = "$RamDumpInterval"
} else {
    Remove-Item Env:\MELONDS_NSML_RAM_DUMP_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_RAM_DUMP_FRAMES -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_RAM_DUMP_INTERVAL -ErrorAction SilentlyContinue
}

& (Resolve-Path $Exe).Path (Resolve-Path $Rom).Path *> $stdout
$exitCode = $LASTEXITCODE

if ($exitCode -ne 0) {
    throw "melonDS exited with code $exitCode. See $stdout"
}

if (-not (Test-Path $hashLog)) {
    throw "hash log was not created: $hashLog"
}

$hashRows = Import-Csv $hashLog
if (-not ($hashRows | Where-Object { $_.instance -eq "0" })) {
    throw "hash log did not contain instance 0 rows: $hashLog"
}

if (-not ($hashRows | Where-Object { $_.instance -eq "1" })) {
    throw "hash log did not contain instance 1 rows: $hashLog"
}

if (-not (Select-String -Path $stdout -Pattern "NSMB Test: frame limit reached" -Quiet)) {
    throw "frame-limit completion marker was not found in $stdout"
}

$inst0Screens = Get-ChildItem $screenDir -Filter "inst0_*.png" -ErrorAction SilentlyContinue
$inst1Screens = Get-ChildItem $screenDir -Filter "inst1_*.png" -ErrorAction SilentlyContinue
if (-not $inst0Screens -or -not $inst1Screens) {
    throw "expected screenshots for both instances in $screenDir"
}

Write-Host "NSMB Mario vs Luigi route smoke passed: frames=$Frames rows=$($hashRows.Count) screenshots=$($inst0Screens.Count + $inst1Screens.Count)"
