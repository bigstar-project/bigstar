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
    [int]$GameStateTraceStartFrame = 0,
    [int]$GameStateTraceEndFrame = 0,
    [switch]$GameStateTraceExtended,
    [string]$RamDumpFrames = "",
    [int]$RamDumpInterval = 0,
    [string]$StateSaveDir = "",
    [int]$StateSaveFrame = 0,
    [int]$VsStarSnapFrame = 0,
    [int]$VsStarSnapPlayerSlot = 0,
    [switch]$AllowJit,
    [switch]$NoFrameLimit,
    [switch]$NoScreenshots,
    [int]$ScreenshotInterval = 120,
    [switch]$NoHashLog,
    [int]$HashInterval = 300,
    [switch]$QuietLog,
    [int]$ActiveFpsStartFrame = 0,
    [switch]$Visible,
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
    [int]$CallTraceDumpLen = 32,
    [switch]$WriteTrace,
    [string]$WriteTraceAddrs = "",
    [int]$WriteTraceStartFrame = 0,
    [int]$WriteTraceEndFrame = 0,
    [switch]$BadJumpTrace
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stdout = Join-Path $LogDir "nsmb-mvl-route.stdout.txt"
$stderr = Join-Path $LogDir "nsmb-mvl-route.stderr.txt"
$hashLog = Join-Path $LogDir "nsmb-mvl-route.hash.csv"
$gameStateTracePath = Join-Path $LogDir "nsmb-mvl-route.game-state.csv"
$callTracePath = Join-Path $LogDir "nsmb-mvl-route.call-trace.csv"
$writeTracePath = Join-Path $LogDir "nsmb-mvl-route.write-trace.csv"
$screenDir = Join-Path $LogDir "screens-mvl-route"
$ramDumpDir = Join-Path $LogDir "ram-mvl-route"
$romRoot = Join-Path $LogDir "rom"
Remove-Item -Force $stdout, $stderr, $hashLog, $gameStateTracePath, $callTracePath, $writeTracePath -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $screenDir -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $ramDumpDir -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $romRoot -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $romRoot | Out-Null

$sourceRomPath = (Resolve-Path $Rom).Path
$testRomPath = Join-Path $romRoot "nsmb.nds"
Copy-Item -Force $sourceRomPath $testRomPath

$sourceRomBase = [System.IO.Path]::Combine(
    [System.IO.Path]::GetDirectoryName($sourceRomPath),
    [System.IO.Path]::GetFileNameWithoutExtension($sourceRomPath))
foreach ($suffix in @(".sav", ".sav.2")) {
    $sourceSave = "$sourceRomBase$suffix"
    if (Test-Path $sourceSave) {
        Copy-Item -Force $sourceSave (Join-Path $romRoot "nsmb$suffix")
    }
}

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
    "MELONDS_NSML_DISABLE_FRAME_LIMIT",
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
    "MELONDS_NSML_CALL_TRACE_DUMP_LEN",
    "MELONDS_NSML_WRITE_TRACE",
    "MELONDS_NSML_WRITE_TRACE_LOG",
    "MELONDS_NSML_WRITE_TRACE_ADDRS",
    "MELONDS_NSML_WRITE_TRACE_START_FRAME",
    "MELONDS_NSML_WRITE_TRACE_END_FRAME",
    "MELONDS_NSML_BAD_JUMP_TRACE",
    "MELONDS_NSML_DIRECT_MVL_BOOT",
    "MELONDS_NSML_DIRECT_MVL_BOOT_FRAME",
    "MELONDS_NSML_DIRECT_MVL_BOOT_SCENE",
    "MELONDS_NSML_DIRECT_MVL_BOOT_STAGE",
    "MELONDS_NSML_DIRECT_MVL_BOOT_PLAYER_ID",
    "MELONDS_NSML_DIRECT_MVL_BOOT_LOAD_SM",
    "MELONDS_NSML_DIRECT_MVL_BOOT_PATCH_LOAD_SM_ONLY",
    "MELONDS_NSML_DIRECT_MVL_BOOT_CALL_UPDATE_SM",
    "MELONDS_NSML_DIRECT_MVL_BOOT_CALL_START_LOAD",
    "MELONDS_NSML_DIRECT_MVL_BOOT_CALL_COURSE_SELECT",
    "MELONDS_NSML_DIRECT_MVL_BOOT_CALL_OBJECT_COURSE_SELECT",
    "MELONDS_NSML_DISABLE_JIT",
    "MELONDS_NSML_ALLOW_JIT",
    "MELONDS_NSML_DISABLE_HASH",
    "MELONDS_NSML_HASH_LOG",
    "MELONDS_NSML_HASH_INTERVAL",
    "MELONDS_NSML_GAME_STATE_TRACE_START_FRAME",
    "MELONDS_NSML_GAME_STATE_TRACE_END_FRAME",
    "MELONDS_NSML_SCREENSHOT_DIR",
    "MELONDS_NSML_SCREENSHOT_INTERVAL",
    "MELONDS_NSML_QUIET_LOG",
    "MELONDS_NSML_ACTIVE_FPS_START_FRAME"
)) {
    Remove-Item "Env:\$name" -ErrorAction SilentlyContinue
}

$env:MELONDS_NSML_TEST = "1"
$env:MELONDS_NSML_TEST_INSTANCES = "2"
$env:MELONDS_NSML_TEST_FRAMES = "$Frames"
$env:MELONDS_NSML_INPUT_SCRIPT = (Resolve-Path $InputScript).Path
$env:MELONDS_NSML_FIXED_RTC = "2020-01-01T00:00:00"
if ($NoHashLog) {
    $env:MELONDS_NSML_DISABLE_HASH = "1"
    Remove-Item Env:\MELONDS_NSML_HASH_LOG -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_HASH_INTERVAL -ErrorAction SilentlyContinue
} else {
    Remove-Item Env:\MELONDS_NSML_DISABLE_HASH -ErrorAction SilentlyContinue
    $env:MELONDS_NSML_HASH_LOG = (Join-Path (Resolve-Path $LogDir).Path "nsmb-mvl-route.hash.csv")
    $env:MELONDS_NSML_HASH_INTERVAL = "$HashInterval"
}
if ($NoScreenshots) {
    Remove-Item Env:\MELONDS_NSML_SCREENSHOT_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_SCREENSHOT_INTERVAL -ErrorAction SilentlyContinue
} else {
    $env:MELONDS_NSML_SCREENSHOT_DIR = (Join-Path (Resolve-Path $LogDir).Path "screens-mvl-route")
    $env:MELONDS_NSML_SCREENSHOT_INTERVAL = "$ScreenshotInterval"
}
if ($AllowJit) {
    Remove-Item Env:\MELONDS_NSML_DISABLE_JIT -ErrorAction SilentlyContinue
    $env:MELONDS_NSML_ALLOW_JIT = "1"
} else {
    Remove-Item Env:\MELONDS_NSML_ALLOW_JIT -ErrorAction SilentlyContinue
    $env:MELONDS_NSML_DISABLE_JIT = "1"
}
if ($NoFrameLimit) {
    $env:MELONDS_NSML_DISABLE_FRAME_LIMIT = "1"
} else {
    Remove-Item Env:\MELONDS_NSML_DISABLE_FRAME_LIMIT -ErrorAction SilentlyContinue
}
if ($QuietLog) {
    $env:MELONDS_NSML_QUIET_LOG = "1"
} else {
    Remove-Item Env:\MELONDS_NSML_QUIET_LOG -ErrorAction SilentlyContinue
}
if ($ActiveFpsStartFrame -gt 0) {
    $env:MELONDS_NSML_ACTIVE_FPS_START_FRAME = "$ActiveFpsStartFrame"
} else {
    Remove-Item Env:\MELONDS_NSML_ACTIVE_FPS_START_FRAME -ErrorAction SilentlyContinue
}
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
    if ($GameStateTraceStartFrame -gt 0) {
        $env:MELONDS_NSML_GAME_STATE_TRACE_START_FRAME = "$GameStateTraceStartFrame"
    } else {
        Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE_START_FRAME -ErrorAction SilentlyContinue
    }
    if ($GameStateTraceEndFrame -gt 0) {
        $env:MELONDS_NSML_GAME_STATE_TRACE_END_FRAME = "$GameStateTraceEndFrame"
    } else {
        Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE_END_FRAME -ErrorAction SilentlyContinue
    }
    if ($GameStateTraceExtended) {
        $env:MELONDS_NSML_GAME_STATE_TRACE_EXTENDED = "1"
    } else {
        Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE_EXTENDED -ErrorAction SilentlyContinue
    }
} else {
    Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE_INTERVAL -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE_START_FRAME -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE_END_FRAME -ErrorAction SilentlyContinue
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
if ($WriteTrace) {
    $env:MELONDS_NSML_WRITE_TRACE = "1"
    $env:MELONDS_NSML_WRITE_TRACE_LOG = (Join-Path (Resolve-Path $LogDir).Path "nsmb-mvl-route.write-trace.csv")
    if ($WriteTraceAddrs) {
        $env:MELONDS_NSML_WRITE_TRACE_ADDRS = $WriteTraceAddrs
    } else {
        Remove-Item Env:\MELONDS_NSML_WRITE_TRACE_ADDRS -ErrorAction SilentlyContinue
    }
    if ($WriteTraceStartFrame -gt 0) {
        $env:MELONDS_NSML_WRITE_TRACE_START_FRAME = "$WriteTraceStartFrame"
    } else {
        Remove-Item Env:\MELONDS_NSML_WRITE_TRACE_START_FRAME -ErrorAction SilentlyContinue
    }
    if ($WriteTraceEndFrame -gt 0) {
        $env:MELONDS_NSML_WRITE_TRACE_END_FRAME = "$WriteTraceEndFrame"
    } else {
        Remove-Item Env:\MELONDS_NSML_WRITE_TRACE_END_FRAME -ErrorAction SilentlyContinue
    }
} else {
    Remove-Item Env:\MELONDS_NSML_WRITE_TRACE -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_WRITE_TRACE_LOG -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_WRITE_TRACE_ADDRS -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_WRITE_TRACE_START_FRAME -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_WRITE_TRACE_END_FRAME -ErrorAction SilentlyContinue
}
if ($BadJumpTrace) {
    $env:MELONDS_NSML_BAD_JUMP_TRACE = "1"
} else {
    Remove-Item Env:\MELONDS_NSML_BAD_JUMP_TRACE -ErrorAction SilentlyContinue
}

$startInfo = @{
    FilePath = (Resolve-Path $Exe).Path
    ArgumentList = @((Resolve-Path $testRomPath).Path)
    Wait = $true
    PassThru = $true
    RedirectStandardOutput = $stdout
    RedirectStandardError = $stderr
}
if (-not $Visible) {
    $startInfo.WindowStyle = "Hidden"
}
$proc = Start-Process @startInfo
$exitCode = $proc.ExitCode
if (Test-Path $stderr) {
    Add-Content -Path $stdout -Value (Get-Content -Path $stderr -Raw)
}

if ($exitCode -ne 0) {
    throw "melonDS exited with code $exitCode. See $stdout"
}

if (-not $NoHashLog -and -not (Test-Path $hashLog)) {
    throw "hash log was not created: $hashLog"
}

$hashRows = @()
if (-not $NoHashLog) {
    $hashRows = Import-Csv $hashLog
    if (-not ($hashRows | Where-Object { $_.instance -eq "0" })) {
        throw "hash log did not contain instance 0 rows: $hashLog"
    }

    if (-not ($hashRows | Where-Object { $_.instance -eq "1" })) {
        throw "hash log did not contain instance 1 rows: $hashLog"
    }
}

if (-not (Select-String -Path $stdout -Pattern "NSMB Test: frame limit reached" -Quiet)) {
    throw "frame-limit completion marker was not found in $stdout"
}

$screenshotCount = 0
if (-not $NoScreenshots) {
    $inst0Screens = Get-ChildItem $screenDir -Filter "inst0_*.png" -ErrorAction SilentlyContinue
    $inst1Screens = Get-ChildItem $screenDir -Filter "inst1_*.png" -ErrorAction SilentlyContinue
    if (-not $inst0Screens -or -not $inst1Screens) {
        throw "expected screenshots for both instances in $screenDir"
    }
    $screenshotCount = $inst0Screens.Count + $inst1Screens.Count
}

Write-Host "NSMB Mario vs Luigi route smoke passed: frames=$Frames rows=$($hashRows.Count) screenshots=$screenshotCount"
