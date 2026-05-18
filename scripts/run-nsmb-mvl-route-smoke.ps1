param(
    [int]$Frames = 4200,
    [string]$Exe = "build\debug-windows-x86_64\melonDS.exe",
    [string]$Rom = "roms\nsmb.nds",
    [string]$InputScript = "tests\nsmb_mario_vs_luigi.inputs",
    [string]$Seed = "0x00000100",
    [switch]$NoRngPatch,
    [string]$LogDir = "logs",
    [switch]$GameStateTrace,
    [int]$GameStateTraceInterval = 60,
    [switch]$GameStateTraceExtended,
    [string]$RamDumpFrames = "",
    [int]$RamDumpInterval = 0,
    [string]$StateSaveDir = "",
    [int]$StateSaveFrame = 0,
    [int]$VsStarSnapFrame = 0,
    [int]$VsStarSnapPlayerSlot = 0,
    [int]$PlayerSnapToStarFrame = 0,
    [int]$PlayerSnapToStarSlot = 0,
    [int]$PlayerStickToStarStartFrame = 0,
    [int]$PlayerStickToStarEndFrame = 0,
    [int]$PlayerStickToStarSlot = 0,
    [switch]$FrameBarrier,
    [switch]$SerialRun,
    [switch]$CallTrace,
    [string]$CallTraceAddrs = "",
    [int]$CallTraceStartFrame = 0,
    [int]$CallTraceEndFrame = -1,
    [int]$CallTraceDumpLen = 32
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stdout = Join-Path $LogDir "nsmb-mvl-route.stdout.txt"
$hashLog = Join-Path $LogDir "nsmb-mvl-route.hash.csv"
$gameStateTracePath = Join-Path $LogDir "nsmb-mvl-route.game-state.csv"
$callTracePath = Join-Path $LogDir "nsmb-mvl-route.call-trace.csv"
$screenDir = Join-Path $LogDir "screens-mvl-route"
$ramDumpDir = Join-Path $LogDir "ram-mvl-route"
Remove-Item -Force $stdout, $hashLog, $gameStateTracePath, $callTracePath -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $screenDir -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $ramDumpDir -ErrorAction SilentlyContinue

foreach ($name in @(
    "MELONDS_NSML_POC",
    "MELONDS_NSML_ROLE",
    "MELONDS_NSML_PEER",
    "MELONDS_NSML_PORT",
    "MELONDS_NSML_LOCAL_INSTANCE",
    "MELONDS_NSML_DELAY",
    "MELONDS_NSML_NETPLAY_START_FRAME",
    "MELONDS_NSML_NETPLAY_WARMUP_FRAMES",
    "MELONDS_NSML_NO_LOCAL_WAIT",
    "MELONDS_NSML_WAIT_FOR_PEER",
    "MELONDS_NSML_WAIT_FOR_PEER_AT_NETPLAY_START",
    "MELONDS_NSML_DEFER_NETWORK_UNTIL_START",
    "MELONDS_NSML_FRAME_BARRIER",
    "MELONDS_NSML_SERIAL_RUN",
    "MELONDS_NSML_NETPLAY_FRAME_BARRIER",
    "MELONDS_NSML_STATE_SYNC",
    "MELONDS_NSML_STATE_APPLY",
    "MELONDS_NSML_STATE_SYNC_INTERVAL",
    "MELONDS_NSML_STATE_SYNC_EXTENDED",
    "MELONDS_NSML_STATE_LOAD_DIR",
    "MELONDS_NSML_STATE_LOAD_FRAME",
    "MELONDS_NSML_STATE_SAVE_DIR",
    "MELONDS_NSML_STATE_SAVE_FRAME",
    "MELONDS_NSML_PLAYER_SNAP_TO_STAR_FRAME",
    "MELONDS_NSML_PLAYER_SNAP_TO_STAR_SLOT",
    "MELONDS_NSML_PLAYER_STICK_TO_STAR_START_FRAME",
    "MELONDS_NSML_PLAYER_STICK_TO_STAR_END_FRAME",
    "MELONDS_NSML_PLAYER_STICK_TO_STAR_SLOT",
    "MELONDS_NSML_CALL_TRACE",
    "MELONDS_NSML_CALL_TRACE_LOG",
    "MELONDS_NSML_CALL_TRACE_ADDRS",
    "MELONDS_NSML_CALL_TRACE_START_FRAME",
    "MELONDS_NSML_CALL_TRACE_END_FRAME",
    "MELONDS_NSML_CALL_TRACE_DUMP_LEN"
)) {
    Remove-Item "Env:\$name" -ErrorAction SilentlyContinue
}

$env:MELONDS_NSML_TEST = "1"
$env:MELONDS_NSML_TEST_INSTANCES = "2"
$env:MELONDS_NSML_TEST_FRAMES = "$Frames"
$env:MELONDS_NSML_INPUT_SCRIPT = (Resolve-Path $InputScript).Path
$env:MELONDS_NSML_HASH_LOG = (Join-Path (Resolve-Path $LogDir).Path "nsmb-mvl-route.hash.csv")
$env:MELONDS_NSML_HASH_INTERVAL = "300"
$env:MELONDS_NSML_SCREENSHOT_DIR = (Join-Path (Resolve-Path $LogDir).Path "screens-mvl-route")
$env:MELONDS_NSML_SCREENSHOT_INTERVAL = "120"
$env:MELONDS_NSML_FIXED_RTC = "2020-01-01T00:00:00"
$env:MELONDS_NSML_DISABLE_JIT = "1"
if ($FrameBarrier) {
    $env:MELONDS_NSML_FRAME_BARRIER = "1"
} else {
    Remove-Item Env:\MELONDS_NSML_FRAME_BARRIER -ErrorAction SilentlyContinue
}
if ($SerialRun) {
    $env:MELONDS_NSML_SERIAL_RUN = "1"
} else {
    Remove-Item Env:\MELONDS_NSML_SERIAL_RUN -ErrorAction SilentlyContinue
}
if ($NoRngPatch) {
    Remove-Item Env:\MELONDS_NSML_NET_RANDOM_AUTO -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_NET_RANDOM_VALUE -ErrorAction SilentlyContinue
} else {
    $env:MELONDS_NSML_NET_RANDOM_AUTO = "1"
    $env:MELONDS_NSML_NET_RANDOM_VALUE = $Seed
}
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
if ($StateSaveDir -and $StateSaveFrame -gt 0) {
    New-Item -ItemType Directory -Force -Path $StateSaveDir | Out-Null
    $env:MELONDS_NSML_STATE_SAVE_DIR = (Resolve-Path $StateSaveDir).Path
    $env:MELONDS_NSML_STATE_SAVE_FRAME = "$StateSaveFrame"
} else {
    Remove-Item Env:\MELONDS_NSML_STATE_SAVE_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_STATE_SAVE_FRAME -ErrorAction SilentlyContinue
}
if ($VsStarSnapFrame -gt 0) {
    $env:MELONDS_NSML_VS_STAR_SNAP_FRAME = "$VsStarSnapFrame"
    $env:MELONDS_NSML_VS_STAR_SNAP_PLAYER_SLOT = "$VsStarSnapPlayerSlot"
} else {
    Remove-Item Env:\MELONDS_NSML_VS_STAR_SNAP_FRAME -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_VS_STAR_SNAP_PLAYER_SLOT -ErrorAction SilentlyContinue
}
if ($PlayerSnapToStarFrame -gt 0) {
    $env:MELONDS_NSML_PLAYER_SNAP_TO_STAR_FRAME = "$PlayerSnapToStarFrame"
    $env:MELONDS_NSML_PLAYER_SNAP_TO_STAR_SLOT = "$PlayerSnapToStarSlot"
} else {
    Remove-Item Env:\MELONDS_NSML_PLAYER_SNAP_TO_STAR_FRAME -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_PLAYER_SNAP_TO_STAR_SLOT -ErrorAction SilentlyContinue
}
if ($PlayerStickToStarStartFrame -gt 0) {
    $env:MELONDS_NSML_PLAYER_STICK_TO_STAR_START_FRAME = "$PlayerStickToStarStartFrame"
    if ($PlayerStickToStarEndFrame -gt 0) {
        $env:MELONDS_NSML_PLAYER_STICK_TO_STAR_END_FRAME = "$PlayerStickToStarEndFrame"
    } else {
        $env:MELONDS_NSML_PLAYER_STICK_TO_STAR_END_FRAME = "$PlayerStickToStarStartFrame"
    }
    $env:MELONDS_NSML_PLAYER_STICK_TO_STAR_SLOT = "$PlayerStickToStarSlot"
} else {
    Remove-Item Env:\MELONDS_NSML_PLAYER_STICK_TO_STAR_START_FRAME -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_PLAYER_STICK_TO_STAR_END_FRAME -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_PLAYER_STICK_TO_STAR_SLOT -ErrorAction SilentlyContinue
}
if ($CallTrace) {
    $env:MELONDS_NSML_CALL_TRACE = "1"
    $env:MELONDS_NSML_CALL_TRACE_LOG = (Join-Path (Resolve-Path $LogDir).Path "nsmb-mvl-route.call-trace.csv")
    if ($CallTraceAddrs) {
        $env:MELONDS_NSML_CALL_TRACE_ADDRS = $CallTraceAddrs
    } else {
        Remove-Item Env:\MELONDS_NSML_CALL_TRACE_ADDRS -ErrorAction SilentlyContinue
    }
    $env:MELONDS_NSML_CALL_TRACE_START_FRAME = "$CallTraceStartFrame"
    if ($CallTraceEndFrame -ge 0) {
        $env:MELONDS_NSML_CALL_TRACE_END_FRAME = "$CallTraceEndFrame"
    } else {
        Remove-Item Env:\MELONDS_NSML_CALL_TRACE_END_FRAME -ErrorAction SilentlyContinue
    }
    $env:MELONDS_NSML_CALL_TRACE_DUMP_LEN = "$CallTraceDumpLen"
} else {
    Remove-Item Env:\MELONDS_NSML_CALL_TRACE -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_CALL_TRACE_LOG -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_CALL_TRACE_ADDRS -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_CALL_TRACE_START_FRAME -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_CALL_TRACE_END_FRAME -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_CALL_TRACE_DUMP_LEN -ErrorAction SilentlyContinue
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
