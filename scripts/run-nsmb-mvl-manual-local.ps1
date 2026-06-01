param(
    [int]$Frames = 999999,
    [int]$WaitTimeoutMs = 86400000,
    [int]$StallTimeoutMs = 0,
    [int]$StallStartFrame = 900,
    [int]$InputDelayFrames = 16,
    [int]$InputMaxFrameLead = 2,
    [int]$InternalWaitTimeoutMs = 0,
    [int]$InputSendDelayFrames = 0,
    [int]$InputSendJitterFrames = 0,
    [switch]$InputUnreliable,
    [int]$InputBundleHistory = 0,
    [switch]$NetworkPumpThread,
    [int]$NetworkPumpSleepUs = 250,
    [switch]$LowDelayWan,
    [switch]$LowLatencyRollback,
    [switch]$Rollback,
    [string]$RollbackBackend = "",
    [string]$RollbackTinyCoreFlags = "",
    [int]$RollbackWindow = 120,
    [int]$RollbackCheckpointInterval = 30,
    [int]$RollbackResimulateDelayFrames = 0,
    [int]$RollbackInputWaitUs = 0,
    [int]$RollbackMaxResimFrames = 0,
    [switch]$RollbackResimulate,
    [int]$HostStartupDelayMs = 1200,
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-netaid.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-netaid.tmp.nds",
    [string]$InputScript = "tests\nsmb_us_direct_mvl_minimal_bootstrap.inputs",
    [string]$LogDir = "",
    [int]$ScreenshotInterval = 0,
    [switch]$GameStateTrace,
    [int]$GameStateTraceInterval = 60,
    [int]$GameStateTraceStartFrame = 0,
    [int]$GameStateTraceEndFrame = 0,
    [switch]$GameStateTraceExtended,
    [switch]$InputNetplayTrace,
    [switch]$TracePlayerLifeChanges,
    [switch]$TracePlayerDefeated,
    [switch]$PerfBreakdown,
    [int]$PacketBridgeStartFrame = 840,
    [int]$MvlStage = -1,
    [string]$MvlSceneSettings = "",
    [ValidateSet(1, 2, 3)] [int]$MvlWins = 2,
    [ValidateSet(3, 5, 10)] [int]$MvlBigStars = 5,
    [ValidateSet("3", "5", "endless", "Endless")] [string]$MvlLives = "endless",
    [ValidateSet("fixed", "random", "select")]
    [string]$MvlCourseMode = "fixed",
    [switch]$GenerateMvlConfiguredRoms,
    [string]$MvlMatchSeed = "",
    [switch]$AllowJit,
    [switch]$NoFrameLimit,
    [switch]$SoftwareRenderer
)

$ErrorActionPreference = "Stop"

function Set-MelonTomlValue {
    param(
        [string]$Text,
        [string]$KeyPath,
        [string]$Value
    )

    $idx = $KeyPath.LastIndexOf('.')
    if ($idx -lt 0) {
        if ($Text -match "(?m)^$([regex]::Escape($KeyPath))\s*=") {
            return ($Text -replace "(?m)^$([regex]::Escape($KeyPath))\s*=.*$", "$KeyPath = $Value")
        }
        return "$Text`n$KeyPath = $Value"
    }

    $section = $KeyPath.Substring(0, $idx)
    $key = $KeyPath.Substring($idx + 1)
    $sectionPattern = "(?ms)^\[$([regex]::Escape($section))\]\r?\n.*?(?=^\[|\z)"
    $sectionMatch = [regex]::Match($Text, $sectionPattern)
    if (-not $sectionMatch.Success) {
        return "$Text`n[$section]`n$key = $Value`n"
    }

    $sectionText = $sectionMatch.Value
    if ($sectionText -match "(?m)^$([regex]::Escape($key))\s*=") {
        $newSectionText = $sectionText -replace "(?m)^$([regex]::Escape($key))\s*=.*$", "$key = $Value"
    } else {
        $newSectionText = "$sectionText$key = $Value`n"
    }
    return $Text.Remove($sectionMatch.Index, $sectionMatch.Length).Insert($sectionMatch.Index, $newSectionText)
}

if ($LowDelayWan) {
    if (-not $PSBoundParameters.ContainsKey('InputDelayFrames')) { $InputDelayFrames = 4 }
    if (-not $PSBoundParameters.ContainsKey('InputMaxFrameLead')) { $InputMaxFrameLead = 4 }
    if (-not $PSBoundParameters.ContainsKey('InputSendDelayFrames')) { $InputSendDelayFrames = 0 }
    if (-not $PSBoundParameters.ContainsKey('InputSendJitterFrames')) { $InputSendJitterFrames = 0 }
    $InputUnreliable = $true
    if (-not $PSBoundParameters.ContainsKey('InputBundleHistory')) { $InputBundleHistory = 8 }
}

if ($LowLatencyRollback) {
    $InputDelayFrames = 0
    $InputMaxFrameLead = 8
    $Rollback = $true
    if (-not $PSBoundParameters.ContainsKey('RollbackBackend')) { $RollbackBackend = "coredelta" }
    if (-not $PSBoundParameters.ContainsKey('RollbackWindow')) { $RollbackWindow = 64 }
    if (-not $PSBoundParameters.ContainsKey('RollbackCheckpointInterval')) { $RollbackCheckpointInterval = 8 }
    if (-not $PSBoundParameters.ContainsKey('PacketBridgeStartFrame')) { $PacketBridgeStartFrame = 870 }
    if (-not $PSBoundParameters.ContainsKey('StallTimeoutMs')) { $StallTimeoutMs = 5000 }
    if (-not $PSBoundParameters.ContainsKey('GameStateTrace')) { $GameStateTrace = $true }
    if (-not $PSBoundParameters.ContainsKey('GameStateTraceInterval')) { $GameStateTraceInterval = 15 }
    $GameStateTraceExtended = $true
    $TracePlayerLifeChanges = $true
    $TracePlayerDefeated = $true
    $RollbackResimulate = $true
    $PerfBreakdown = $true
}

if ($LowLatencyRollback -and ($RollbackBackend -eq "nsmbtinycore" -or $RollbackBackend -eq "nsmb-tiny-core")) {
    if (-not $PSBoundParameters.ContainsKey('InputMaxFrameLead')) { $InputMaxFrameLead = 1 }
    if (-not $PSBoundParameters.ContainsKey('RollbackCheckpointInterval')) { $RollbackCheckpointInterval = 1 }
    if (-not $PSBoundParameters.ContainsKey('RollbackInputWaitUs')) { $RollbackInputWaitUs = 2500 }
    if (-not $PSBoundParameters.ContainsKey('RollbackMaxResimFrames')) { $RollbackMaxResimFrames = 1 }
    if (-not $PSBoundParameters.ContainsKey('NetworkPumpThread')) { $NetworkPumpThread = $true }
    if (-not $PSBoundParameters.ContainsKey('NetworkPumpSleepUs')) { $NetworkPumpSleepUs = 50 }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
if ($LogDir -eq "") {
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $LogDir = "logs\nsmb-mvl-manual-local-$timestamp"
}
$smokeScript = Join-Path $PSScriptRoot "run-nsmb-mvl-lan-route-smoke.ps1"
$logRoot = Join-Path $repoRoot $LogDir
$hostLog = Join-Path $logRoot "host"
$clientLog = Join-Path $logRoot "client"
$wrapperLog = Join-Path $logRoot "wrapper"
New-Item -ItemType Directory -Force $wrapperLog | Out-Null

$cfgPath = Join-Path $repoRoot "build\release-windows-x86_64\melonDS.toml"
if (Test-Path $cfgPath) {
    $cfg = Get-Content $cfgPath -Raw
    $useGL = if ($SoftwareRenderer) { 'false' } else { 'true' }
    $renderer = if ($SoftwareRenderer) { '0' } else { '2' }
    $replacements = [ordered]@{
        'LimitFPS' = 'true'
        'AudioSync' = 'false'
        'Screen.UseGL' = $useGL
        'Screen.VSync' = 'false'
        'Screen.VSyncInterval' = '1'
        '3D.Renderer' = $renderer
        '3D.GL.ScaleFactor' = '1'
        '3D.GL.HiresCoordinates' = 'false'
        '3D.Soft.Threaded' = 'true'
        'Instance0.Window0.ScreenSizing' = '0'
        'Instance0.Window0.ShowOSD' = 'false'
    }
    foreach ($key in $replacements.Keys) {
        $value = $replacements[$key]
        $cfg = Set-MelonTomlValue -Text $cfg -KeyPath $key -Value $value
    }
    Set-Content -Path $cfgPath -Value $cfg -Encoding UTF8
}

$common = @(
    "-Frames", "$Frames",
    "-WaitTimeoutMs", "$WaitTimeoutMs",
    "-InternalWaitTimeoutMs", "$InternalWaitTimeoutMs",
    "-StallTimeoutMs", "$StallTimeoutMs",
    "-StallStartFrame", "$StallStartFrame",
    "-Exe", $Exe,
    "-InputScript", $InputScript,
    "-ScreenshotInterval", "$ScreenshotInterval",
    "-NoHashLog",
    "-SkipMvlStateCheck",
    "-SkipGameplayActorCheck",
    "-NoLanMP",
    "-InputNetplay",
    "-InputDelayFrames", "$InputDelayFrames",
    "-InputMaxFrameLead", "$InputMaxFrameLead",
    "-InputSendDelayFrames", "$InputSendDelayFrames",
    "-InputSendJitterFrames", "$InputSendJitterFrames",
    "-PacketBridgeJitHelperPatch",
    "-PacketBridgeJitHelperPatchFrame", "$PacketBridgeStartFrame",
    "-PacketBridgeStartFrame", "$PacketBridgeStartFrame",
    "-ClearMvlCameraInitHold",
    "-ClearMvlCameraInitHoldStartFrame", "840",
    "-WaitForPeerAtNetplayStart"
)
if ($GameStateTrace) {
    $common += @(
        "-GameStateTrace",
        "-GameStateTraceInterval", "$GameStateTraceInterval",
        "-GameStateTraceStartFrame", "$GameStateTraceStartFrame",
        "-GameStateTraceEndFrame", "$GameStateTraceEndFrame"
    )
    if ($GameStateTraceExtended) {
        $common += "-GameStateTraceExtended"
    }
}
if ($InputNetplayTrace) {
    $common += "-InputNetplayTrace"
}
if ($TracePlayerLifeChanges) {
    $common += "-TracePlayerLifeChanges"
}
if ($TracePlayerDefeated) {
    $common += "-TracePlayerDefeated"
}
if ($NoFrameLimit) {
    $common += "-NoFrameLimit"
}
if ($AllowJit) {
    $common += "-AllowJit"
}
if ($Rollback) {
    $common += @(
        "-Rollback",
        "-RollbackWindow", "$RollbackWindow",
        "-RollbackCheckpointInterval", "$RollbackCheckpointInterval",
        "-RollbackResimulateDelayFrames", "$RollbackResimulateDelayFrames"
    )
    if ($RollbackBackend -ne "") {
        $common += @("-RollbackBackend", "$RollbackBackend")
    }
    if ($RollbackResimulate) {
        $common += "-RollbackResimulate"
    }
}
if ($InputUnreliable) {
    $common += @("-InputUnreliable", "-InputBundleHistory", "$InputBundleHistory")
}
if ($MvlStage -ge 0) {
    $common += @("-MvlStage", "$MvlStage")
}
if ($MvlSceneSettings -ne "") {
    $common += @("-MvlSceneSettings", "$MvlSceneSettings")
}
$common += @("-MvlWins", "$MvlWins", "-MvlBigStars", "$MvlBigStars", "-MvlLives", "$MvlLives")
if ($MvlCourseMode -ne "fixed") {
    $common += @("-MvlCourseMode", "$MvlCourseMode")
}
if ($GenerateMvlConfiguredRoms) {
    $common += @("-GenerateMvlConfiguredRoms")
}
if ($MvlMatchSeed -ne "") {
    $common += @("-MvlMatchSeed", "$MvlMatchSeed")
}

if ($LowLatencyRollback) {
    $env:MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "30"
    $env:MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
    $env:MELONDS_NSML_FIXED_FRAME_SLEEP = "1"
    $env:MELONDS_NSML_FPS_SPIKE_THRESHOLD_MS = "25"
    $env:MELONDS_NSML_FPS_SPIKE_TRACE = "1"
    if ($RollbackBackend -eq "nsmbtinycore" -or $RollbackBackend -eq "nsmb-tiny-core") {
        $env:MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES = "1"
        $env:MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES = "1"
        $env:MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE = "1"
        $env:MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES = "1"
        $env:MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES = "0"
        $env:MELONDS_NSML_ROLLBACK_NSMB_SCAN_INTERVAL = "30"
        $env:MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
        $env:MELONDS_NSML_ROLLBACK_RESIM_SKIP_RENDER = "1"
        $env:MELONDS_NSML_SUPPRESS_PU_DEBUG = "1"
        if ($RollbackTinyCoreFlags -eq "") { $RollbackTinyCoreFlags = "0x241" }
        $env:MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS = "$RollbackTinyCoreFlags"
    } else {
        Remove-Item Env:\MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_ROLLBACK_NSMB_SCAN_INTERVAL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_ROLLBACK_RESIM_SKIP_RENDER -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SUPPRESS_PU_DEBUG -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS -ErrorAction SilentlyContinue
    }
    Remove-Item Env:\MELONDS_NSML_ROLLBACK_CORE_SKIP_MASK -ErrorAction SilentlyContinue
}
if ($RollbackInputWaitUs -gt 0) {
    $env:MELONDS_NSML_ROLLBACK_INPUT_WAIT_US = "$RollbackInputWaitUs"
} else {
    Remove-Item Env:\MELONDS_NSML_ROLLBACK_INPUT_WAIT_US -ErrorAction SilentlyContinue
}
if ($RollbackMaxResimFrames -gt 0) {
    $env:MELONDS_NSML_ROLLBACK_MAX_RESIM_FRAMES = "$RollbackMaxResimFrames"
} else {
    Remove-Item Env:\MELONDS_NSML_ROLLBACK_MAX_RESIM_FRAMES -ErrorAction SilentlyContinue
}
if ($NetworkPumpThread) {
    $env:MELONDS_NSML_NET_PUMP_THREAD = "1"
    $env:MELONDS_NSML_NET_PUMP_SLEEP_US = "$NetworkPumpSleepUs"
} else {
    Remove-Item Env:\MELONDS_NSML_NET_PUMP_THREAD -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_NET_PUMP_SLEEP_US -ErrorAction SilentlyContinue
}
if ($PerfBreakdown) {
    $env:MELONDS_NSML_PERF_BREAKDOWN = "1"
}

$hostArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $smokeScript
) + $common + @(
    "-RunRole", "host",
    "-Rom", "roms\nsmb-us.nds",
    "-HostRom", $HostRom,
    "-LogDir", $hostLog
)

$clientArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $smokeScript
) + $common + @(
    "-RunRole", "client",
    "-Peer", "127.0.0.1",
    "-Rom", "roms\nsmb-us.nds",
    "-ClientRom", $ClientRom,
    "-LogDir", $clientLog
)

$hostOut = Join-Path $wrapperLog "host-wrapper.out.txt"
$hostErr = Join-Path $wrapperLog "host-wrapper.err.txt"
$clientOut = Join-Path $wrapperLog "client-wrapper.out.txt"
$clientErr = Join-Path $wrapperLog "client-wrapper.err.txt"

$hostProc = Start-Process -FilePath "powershell.exe" `
    -ArgumentList $hostArgs `
    -WorkingDirectory $repoRoot `
    -RedirectStandardOutput $hostOut `
    -RedirectStandardError $hostErr `
    -PassThru `
    -WindowStyle Hidden

Start-Sleep -Milliseconds $HostStartupDelayMs

$clientProc = Start-Process -FilePath "powershell.exe" `
    -ArgumentList $clientArgs `
    -WorkingDirectory $repoRoot `
    -RedirectStandardOutput $clientOut `
    -RedirectStandardError $clientErr `
    -PassThru `
    -WindowStyle Hidden

Write-Host "Started NSMB MvL manual local session."
Write-Host "host wrapper pid=$($hostProc.Id) log=$hostLog"
Write-Host "client wrapper pid=$($clientProc.Id) log=$clientLog"
Write-Host "Use the host melonDS window for Mario and the client melonDS window for Luigi."
Write-Host "input delay=$InputDelayFrames max frame lead=$InputMaxFrameLead internal wait timeout ms=$InternalWaitTimeoutMs stallTimeoutMs=$StallTimeoutMs send delay=$InputSendDelayFrames jitter=$InputSendJitterFrames networkPump=$([bool]$NetworkPumpThread) networkPumpSleepUs=$NetworkPumpSleepUs packetBridgeStart=$PacketBridgeStartFrame renderer=$(if ($SoftwareRenderer) { 'software' } else { 'opengl-compute' }) frameLimit=$(-not $NoFrameLimit) perfBreakdown=$([bool]$PerfBreakdown)"
Write-Host "trace gameState=$([bool]$GameStateTrace) interval=$GameStateTraceInterval extended=$([bool]$GameStateTraceExtended) lifeChanges=$([bool]$TracePlayerLifeChanges) defeated=$([bool]$TracePlayerDefeated)"
Write-Host "mvlWins=$MvlWins mvlBigStars=$MvlBigStars mvlLives=$MvlLives mvlStage=$(if ($MvlStage -ge 0) { $MvlStage } else { 'auto/default' }) mvlSceneSettings=$(if ($MvlSceneSettings) { $MvlSceneSettings } else { 'derived' }) mvlCourseMode=$MvlCourseMode generateConfiguredRoms=$($GenerateMvlConfiguredRoms.IsPresent) mvlMatchSeed=$(if ($MvlMatchSeed) { $MvlMatchSeed } else { 'auto' })"
if ($Rollback) {
    $backendLabel = if ($RollbackBackend -ne "") { $RollbackBackend } else { "savestate" }
    $tinyLabel = if ($RollbackTinyCoreFlags -ne "") { " tinyCoreFlags=$RollbackTinyCoreFlags" } else { "" }
    $rollbackWaitLabel = if ($RollbackInputWaitUs -gt 0) { " rollbackInputWaitUs=$RollbackInputWaitUs" } else { "" }
    Write-Host "rollback enabled backend=$backendLabel window=$RollbackWindow checkpointInterval=$RollbackCheckpointInterval resimDelay=$RollbackResimulateDelayFrames resimulate=$RollbackResimulate$tinyLabel$rollbackWaitLabel"
}
if ($InputUnreliable) {
    Write-Host "input unreliable bundleHistory=$InputBundleHistory"
}
if ($AllowJit) {
    Write-Host "JIT is enabled for speed; deterministic sync is not guaranteed yet."
} else {
    Write-Host "JIT is disabled for deterministic sync."
}
