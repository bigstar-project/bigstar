param(
    [int]$Frames = 999999,
    [int]$WaitTimeoutMs = 86400000,
    [int]$StallTimeoutMs = 0,
    [int]$StallStartFrame = 900,
    [int]$GameplayHeartbeatInterval = 120,
    [int]$InputDelayFrames = 2,
    [int]$InputMaxFrameLead = 4,
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
    [switch]$WorldStateTraceObjectLifecycles,
    [switch]$WorldStateTraceActorInternals,
    [switch]$WorldStateTraceEffects,
    [int]$WorldStateTraceObjectLifecyclesInterval = 60,
    [int]$WorldStateTraceObjectLifecyclesStartFrame = 0,
    [int]$WorldStateTraceObjectLifecyclesEndFrame = 0,
    [int]$HostStartupDelayMs = 1200,
    [int]$HostReadyTimeoutMs = 30000,
    [switch]$ClientOnly,
    [switch]$WaitForPeerAtNetplayStart,
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$GenerateMvlSourceRom = "roms\nsmb-us.nds",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-netaid.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-netaid.tmp.nds",
    [switch]$CopyRomToLog,
    [string]$InputScript = "tests\nsmb_us_direct_mvl_minimal_bootstrap.inputs",
    [switch]$RecordInput,
    [string]$InputRecordDir = "",
    [int]$InputRecordStartFrame = 0,
    [int]$InputRecordEndFrame = 0,
    [string]$LogDir = "",
    [int]$ScreenshotInterval = 0,
    [switch]$GameStateTrace,
    [int]$GameStateTraceInterval = 60,
    [int]$GameStateTraceStartFrame = 0,
    [int]$GameStateTraceEndFrame = 0,
    [switch]$GameStateTraceExtended,
    [string]$HostAIPlayLog = "",
    [string]$ClientAIPlayLog = "",
    [string]$HostAIObservationV3Log = "",
    [string]$ClientAIObservationV3Log = "",
    [int]$AIPlayLogInterval = 1,
    [int]$AIPlayLogFlushInterval = 60,
    [int]$AIPlayLogMaxObjects = 128,
    [switch]$NeutralizeHostInput,
    [switch]$NeutralizeClientInput,
    [switch]$InputNetplayTrace,
    [switch]$PacketCapture,
    [switch]$PacketCaptureAllowPreGame,
    [switch]$TracePlayerLifeChanges,
    [switch]$TracePlayerDefeated,
    [switch]$PerfBreakdown,
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
    [int]$PacketBridgeStartFrame = 840,
    [int]$MvlStage = -1,
    [string]$MvlSceneSettings = "",
    [ValidateSet(1, 2, 3)] [int]$MvlWins = 2,
    [ValidateSet(3, 5, 10)] [int]$MvlBigStars = 5,
    [ValidateSet("3", "5", "endless", "Endless")] [string]$MvlLives = "endless",
    [ValidateSet("fixed", "random", "select")]
    [string]$MvlCourseMode = "fixed",
    [switch]$GenerateMvlConfiguredRoms,
    [switch]$SkipRomEnsure,
    [string]$MvlMatchSeed = "",
    [switch]$AllowJit,
    [switch]$NoJit,
    [switch]$NoFrameLimit,
    [switch]$SkipFrameLimitCheck,
    [switch]$SoftwareRenderer,
    [switch]$Wait
)

$ErrorActionPreference = "Stop"

if ($AllowJit -and $NoJit) {
    throw "AllowJit and NoJit cannot be used together"
}

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
    if (-not $PSBoundParameters.ContainsKey('StallTimeoutMs')) { $StallTimeoutMs = 10000 }
    $RollbackResimulate = $true
}

$isTinyCorePreimageRollback = $RollbackBackend -eq "tinycorepreimage" -or $RollbackBackend -eq "tiny-core-preimage"

if ($LowLatencyRollback -and $isTinyCorePreimageRollback) {
    if (-not $PSBoundParameters.ContainsKey('InputMaxFrameLead')) { $InputMaxFrameLead = 2 }
    if (-not $PSBoundParameters.ContainsKey('RollbackWindow')) { $RollbackWindow = 32 }
    if (-not $PSBoundParameters.ContainsKey('RollbackCheckpointInterval')) { $RollbackCheckpointInterval = 1 }
    if (-not $PSBoundParameters.ContainsKey('RollbackInputWaitUs')) { $RollbackInputWaitUs = 1500 }
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
if ($InputRecordDir -eq "") {
    $InputRecordDir = Join-Path $logRoot "recorded-inputs"
} elseif (-not [System.IO.Path]::IsPathRooted($InputRecordDir)) {
    $InputRecordDir = Join-Path $repoRoot $InputRecordDir
}
if ($RecordInput) {
    New-Item -ItemType Directory -Force $InputRecordDir | Out-Null
}

if (!$SkipRomEnsure -and !$GenerateMvlConfiguredRoms) {
    $generatorCourseMode = if ($MvlCourseMode -eq "fixed") { "random" } else { $MvlCourseMode }
    $ensureParams = @{
        SourceRom = $GenerateMvlSourceRom
        HostRom = $HostRom
        ClientRom = $ClientRom
        MvlWins = $MvlWins
        MvlBigStars = $MvlBigStars
        MvlLives = $MvlLives
        MvlCourseMode = $generatorCourseMode
    }
    if ($MvlStage -ge 0) {
        $ensureParams.MvlStage = $MvlStage
    }
    if ($MvlSceneSettings -ne "") {
        $ensureParams.MvlSceneSettings = $MvlSceneSettings
    }
    & (Join-Path $PSScriptRoot "generate-nsmb-mvl-stable-roms.ps1") @ensureParams
}

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
    "-GameplayHeartbeatInterval", "$GameplayHeartbeatInterval",
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
    "-ClearMvlCameraInitHoldStartFrame", "840"
)
if ($ClientOnly) {
    $common += @(
        "-NoLocalWait",
        "-NoImplicitInputNetplayPeerWait"
    )
} elseif ($WaitForPeerAtNetplayStart) {
    $common += "-WaitForPeerAtNetplayStart"
}
if ($CopyRomToLog) {
    $common += "-CopyRomToLog"
}
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
if ($PacketCapture) {
    $common += "-PacketCapture"
    if ($PacketCaptureAllowPreGame) {
        $common += "-PacketCaptureAllowPreGame"
    }
}
if ($TracePlayerLifeChanges) {
    $common += "-TracePlayerLifeChanges"
}
if ($TracePlayerDefeated) {
    $common += "-TracePlayerDefeated"
}
if ($ForcePlayerPowerups) {
    $common += @(
        "-ForcePlayerPowerups",
        "-ForcePlayerPowerupsStartFrame", "$ForcePlayerPowerupsStartFrame",
        "-ForcePlayerPowerupsEndFrame", "$ForcePlayerPowerupsEndFrame",
        "-ForcePlayerPowerup0", "$ForcePlayerPowerup0",
        "-ForcePlayerPowerup1", "$ForcePlayerPowerup1"
    )
}
if ($ForcePlayerStarCounters) {
    $common += @(
        "-ForcePlayerStarCounters",
        "-ForcePlayerStarCountersStartFrame", "$ForcePlayerStarCountersStartFrame",
        "-ForcePlayerStarCountersEndFrame", "$ForcePlayerStarCountersEndFrame",
        "-ForcePlayerBattleStars0", "$ForcePlayerBattleStars0",
        "-ForcePlayerBattleStars1", "$ForcePlayerBattleStars1",
        "-ForcePlayerDisplayedStars0", "$ForcePlayerDisplayedStars0",
        "-ForcePlayerDisplayedStars1", "$ForcePlayerDisplayedStars1",
        "-ForcePlayerCollectedStars0", "$ForcePlayerCollectedStars0",
        "-ForcePlayerCollectedStars1", "$ForcePlayerCollectedStars1"
    )
}
if ($WorldStateTraceObjectLifecycles) {
    $common += @(
        "-WorldStateTraceObjectLifecycles",
        "-WorldStateTraceObjectLifecyclesInterval", "$WorldStateTraceObjectLifecyclesInterval",
        "-WorldStateTraceObjectLifecyclesStartFrame", "$WorldStateTraceObjectLifecyclesStartFrame",
        "-WorldStateTraceObjectLifecyclesEndFrame", "$WorldStateTraceObjectLifecyclesEndFrame"
    )
}
if ($WorldStateTraceActorInternals) {
    $common += "-WorldStateTraceActorInternals"
}
if ($WorldStateTraceEffects) {
    $common += "-WorldStateTraceEffects"
}
if ($NoFrameLimit) {
    $common += "-NoFrameLimit"
}
if ($SkipFrameLimitCheck) {
    $common += "-SkipFrameLimitCheck"
}
if (-not $NoJit) {
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
    $common += "-InputUnreliable"
}
if ($InputBundleHistory -gt 0) {
    $common += @("-InputBundleHistory", "$InputBundleHistory")
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
if ($RecordInput) {
    $common += @(
        "-RecordInput",
        "-InputRecordStartFrame", "$InputRecordStartFrame",
        "-InputRecordEndFrame", "$InputRecordEndFrame"
    )
}

if ($LowLatencyRollback) {
    $env:MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL = "30"
    $env:MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE = "256"
    $env:MELONDS_NSML_FIXED_FRAME_SLEEP = "1"
    if ($PerfBreakdown) {
        $env:MELONDS_NSML_FPS_SPIKE_THRESHOLD_MS = "25"
        $env:MELONDS_NSML_FPS_SPIKE_TRACE = "1"
        $env:MELONDS_NSML_PERF_SPIKE_PHASE_TRACE = "1"
    } else {
        Remove-Item Env:\MELONDS_NSML_FPS_SPIKE_THRESHOLD_MS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FPS_SPIKE_TRACE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PERF_SPIKE_PHASE_TRACE -ErrorAction SilentlyContinue
    }
    if ($isTinyCorePreimageRollback) {
        $env:MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET = "1"
        $env:MELONDS_NSML_ROLLBACK_RESIM_SKIP_RENDER = "1"
        $env:MELONDS_NSML_SUPPRESS_PU_DEBUG = "1"
        if ($RollbackTinyCoreFlags -eq "") { $RollbackTinyCoreFlags = "0x241" }
        $env:MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS = "$RollbackTinyCoreFlags"
    } else {
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
else {
    Remove-Item Env:\MELONDS_NSML_PERF_BREAKDOWN -ErrorAction SilentlyContinue
}

$hostArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $smokeScript
) + $common + @(
    "-RunRole", "host",
    "-Rom", $HostRom,
    "-HostRom", $HostRom,
    "-LogDir", $hostLog
)
if ($RecordInput) {
    $hostArgs += @("-InputRecordFile", (Join-Path $InputRecordDir "host.inputs"))
}

$clientArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $smokeScript
) + $common + @(
    "-RunRole", "client",
    "-Peer", "127.0.0.1",
    "-Rom", $ClientRom,
    "-ClientRom", $ClientRom,
    "-LogDir", $clientLog
)
if ($RecordInput) {
    $clientArgs += @("-InputRecordFile", (Join-Path $InputRecordDir "client.inputs"))
}

$hostOut = Join-Path $wrapperLog "host-wrapper.out.txt"
$hostErr = Join-Path $wrapperLog "host-wrapper.err.txt"
$clientOut = Join-Path $wrapperLog "client-wrapper.out.txt"
$clientErr = Join-Path $wrapperLog "client-wrapper.err.txt"

$oldAIEnv = @{}
foreach ($name in @(
    "MELONDS_NSML_AI_PLAY_LOG",
    "MELONDS_NSML_AI_OBSERVATION_V3_LOG",
    "MELONDS_NSML_AI_PLAY_LOG_INTERVAL",
    "MELONDS_NSML_AI_PLAY_LOG_FLUSH_INTERVAL",
    "MELONDS_NSML_AI_PLAY_LOG_MAX_OBJECTS",
    "MELONDS_NSML_NEUTRALIZE_POLLED_INPUT",
    "MELONDS_NSML_NEUTRALIZE_POLLED_INPUT_PRESERVE_TOUCH"
)) {
    $oldAIEnv[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
}

function Set-AIPlayLogEnv {
    param(
        [string]$Path,
        [string]$ObservationV3Path
    )
    if ($Path -eq "" -and $ObservationV3Path -eq "") {
        Remove-Item Env:\MELONDS_NSML_AI_PLAY_LOG -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_AI_OBSERVATION_V3_LOG -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_AI_PLAY_LOG_INTERVAL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_AI_PLAY_LOG_FLUSH_INTERVAL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_AI_PLAY_LOG_MAX_OBJECTS -ErrorAction SilentlyContinue
        return
    }

    if ($Path -eq "") {
        Remove-Item Env:\MELONDS_NSML_AI_PLAY_LOG -ErrorAction SilentlyContinue
    } else {
        $resolved = if ([System.IO.Path]::IsPathRooted($Path)) {
            $Path
        } else {
            Join-Path $repoRoot $Path
        }
        $parent = Split-Path -Parent $resolved
        if ($parent) {
            New-Item -ItemType Directory -Force -Path $parent | Out-Null
        }
        $env:MELONDS_NSML_AI_PLAY_LOG = $resolved
    }
    if ($ObservationV3Path -eq "") {
        Remove-Item Env:\MELONDS_NSML_AI_OBSERVATION_V3_LOG -ErrorAction SilentlyContinue
    } else {
        $resolvedV3 = if ([System.IO.Path]::IsPathRooted($ObservationV3Path)) {
            $ObservationV3Path
        } else {
            Join-Path $repoRoot $ObservationV3Path
        }
        $parentV3 = Split-Path -Parent $resolvedV3
        if ($parentV3) {
            New-Item -ItemType Directory -Force -Path $parentV3 | Out-Null
        }
        $env:MELONDS_NSML_AI_OBSERVATION_V3_LOG = $resolvedV3
    }
    $env:MELONDS_NSML_AI_PLAY_LOG_INTERVAL = "$AIPlayLogInterval"
    $env:MELONDS_NSML_AI_PLAY_LOG_FLUSH_INTERVAL = "$AIPlayLogFlushInterval"
    $env:MELONDS_NSML_AI_PLAY_LOG_MAX_OBJECTS = "$AIPlayLogMaxObjects"
}

function Set-PolledInputNeutralizeEnv {
    param([bool]$Enabled)
    if ($Enabled) {
        $env:MELONDS_NSML_NEUTRALIZE_POLLED_INPUT = "1"
    } else {
        Remove-Item Env:\MELONDS_NSML_NEUTRALIZE_POLLED_INPUT -ErrorAction SilentlyContinue
    }
    Remove-Item Env:\MELONDS_NSML_NEUTRALIZE_POLLED_INPUT_PRESERVE_TOUCH -ErrorAction SilentlyContinue
}

$hostProc = $null
if (-not $ClientOnly) {
    Set-AIPlayLogEnv -Path $HostAIPlayLog -ObservationV3Path $HostAIObservationV3Log
    Set-PolledInputNeutralizeEnv -Enabled ([bool]$NeutralizeHostInput)
    $hostProc = Start-Process -FilePath "powershell.exe" `
        -ArgumentList $hostArgs `
        -WorkingDirectory $repoRoot `
        -RedirectStandardOutput $hostOut `
        -RedirectStandardError $hostErr `
        -PassThru `
        -WindowStyle Hidden

    if ($HostReadyTimeoutMs -gt 0) {
        $hostReadyLog = Join-Path $hostLog "host.stdout.txt"
        $hostReadyDeadline = [DateTime]::UtcNow.AddMilliseconds($HostReadyTimeoutMs)
        $hostReady = $false
        Write-Host "Waiting for host netplay init. log=$hostReadyLog timeoutMs=$HostReadyTimeoutMs"
        while ([DateTime]::UtcNow -lt $hostReadyDeadline) {
            if ((Get-Process -Id $hostProc.Id -ErrorAction SilentlyContinue) -eq $null) {
                throw "host wrapper exited before netplay init. See $hostOut / $hostErr"
            }
            if (Test-Path $hostReadyLog) {
                if (Select-String -Path $hostReadyLog -Pattern "NSMB PoC: enabled role=host" -Quiet) {
                    $hostReady = $true
                    break
                }
            }
            Start-Sleep -Milliseconds 100
        }
        if (-not $hostReady) {
            throw "host netplay init did not become ready within ${HostReadyTimeoutMs}ms. See $hostReadyLog"
        }
    } elseif ($HostStartupDelayMs -gt 0) {
        Start-Sleep -Milliseconds $HostStartupDelayMs
    }
}

Set-AIPlayLogEnv -Path $ClientAIPlayLog -ObservationV3Path $ClientAIObservationV3Log
Set-PolledInputNeutralizeEnv -Enabled ([bool]$NeutralizeClientInput)
$clientProc = Start-Process -FilePath "powershell.exe" `
    -ArgumentList $clientArgs `
    -WorkingDirectory $repoRoot `
    -RedirectStandardOutput $clientOut `
    -RedirectStandardError $clientErr `
    -PassThru `
    -WindowStyle Hidden

foreach ($entry in $oldAIEnv.GetEnumerator()) {
    if ($null -eq $entry.Value) {
        [Environment]::SetEnvironmentVariable($entry.Key, $null, "Process")
    } else {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, "Process")
    }
}

Write-Host "Started NSMB MvL manual local session."
if ($ClientOnly) {
    Write-Host "host wrapper disabled; client window is the authoritative human recording instance."
} else {
    Write-Host "host wrapper pid=$($hostProc.Id) log=$hostLog"
}
Write-Host "client wrapper pid=$($clientProc.Id) log=$clientLog"
if ($ClientOnly) {
    Write-Host "Use the client melonDS window for Luigi. Mario is observed from the same client-side game state log."
} else {
    Write-Host "Use the host melonDS window for Mario and the client melonDS window for Luigi."
}
Write-Host "physical input neutralized host=$([bool]$NeutralizeHostInput) client=$([bool]$NeutralizeClientInput)"
Write-Host "input delay=$InputDelayFrames max frame lead=$InputMaxFrameLead internal wait timeout ms=$InternalWaitTimeoutMs stallTimeoutMs=$StallTimeoutMs send delay=$InputSendDelayFrames jitter=$InputSendJitterFrames networkPump=$([bool]$NetworkPumpThread) networkPumpSleepUs=$NetworkPumpSleepUs packetBridgeStart=$PacketBridgeStartFrame startBarrier=$([bool]$WaitForPeerAtNetplayStart) renderer=$(if ($SoftwareRenderer) { 'software' } else { 'opengl-compute' }) frameLimit=$(-not $NoFrameLimit) perfBreakdown=$([bool]$PerfBreakdown)"
Write-Host "gameplay heartbeat interval=$GameplayHeartbeatInterval"
if ($HostAIPlayLog -or $ClientAIPlayLog) {
    Write-Host "AI play log host=$(if ($HostAIPlayLog) { $HostAIPlayLog } else { 'off' }) client=$(if ($ClientAIPlayLog) { $ClientAIPlayLog } else { 'off' }) interval=$AIPlayLogInterval flushInterval=$AIPlayLogFlushInterval maxObjects=$AIPlayLogMaxObjects"
}
if ($HostAIObservationV3Log -or $ClientAIObservationV3Log) {
    Write-Host "AI observation v3 host=$(if ($HostAIObservationV3Log) { $HostAIObservationV3Log } else { 'off' }) client=$(if ($ClientAIObservationV3Log) { $ClientAIObservationV3Log } else { 'off' }) interval=$AIPlayLogInterval flushInterval=$AIPlayLogFlushInterval maxObjects=$AIPlayLogMaxObjects"
}
Write-Host "trace gameState=$([bool]$GameStateTrace) interval=$GameStateTraceInterval extended=$([bool]$GameStateTraceExtended) lifeChanges=$([bool]$TracePlayerLifeChanges) defeated=$([bool]$TracePlayerDefeated)"
Write-Host "recordInput=$([bool]$RecordInput) recordDir=$(if ($RecordInput) { $InputRecordDir } else { 'disabled' }) recordStart=$InputRecordStartFrame recordEnd=$InputRecordEndFrame"
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
Write-Host "jit=$(-not $NoJit)$(if ($NoJit) { ' (disabled by -NoJit)' } else { ' (default)' })"

function Get-ManualWrapperExitCodeOrNull {
    param([System.Diagnostics.Process]$Process)

    try {
        $Process.Refresh()
    } catch {
    }

    try {
        if ($Process.HasExited) {
            return [Nullable[int]]$Process.ExitCode
        }
    } catch {
    }

    return $null
}

function Test-ManualWrapperSuccessMarker {
    param(
        [string]$OutPath,
        [string]$ErrPath
    )

    $hasSuccessMarker = $false
    if (Test-Path -LiteralPath $OutPath) {
        $hasSuccessMarker = Select-String -LiteralPath $OutPath -Pattern "NSMB Mario vs Luigi LAN route smoke passed" -Quiet
    }

    $hasErrorOutput = $false
    if (Test-Path -LiteralPath $ErrPath) {
        $errItem = Get-Item -LiteralPath $ErrPath -ErrorAction SilentlyContinue
        $hasErrorOutput = ($null -ne $errItem -and $errItem.Length -gt 0)
    }

    return ($hasSuccessMarker -and -not $hasErrorOutput)
}

if ($Wait) {
    $waitProcesses = @(
        [pscustomobject]@{
            Role = "client"
            Process = $clientProc
            OutPath = $clientOut
            ErrPath = $clientErr
        }
    )
    if ($null -ne $hostProc) {
        $waitProcesses += [pscustomobject]@{
            Role = "host"
            Process = $hostProc
            OutPath = $hostOut
            ErrPath = $hostErr
        }
    }
    $deadline = [DateTime]::UtcNow.AddMilliseconds($WaitTimeoutMs)
    foreach ($entry in $waitProcesses) {
        $proc = $entry.Process
        $remainingMs = [int][Math]::Max(0, ($deadline - [DateTime]::UtcNow).TotalMilliseconds)
        if (-not $proc.WaitForExit($remainingMs)) {
            throw "manual local wrapper did not exit within WaitTimeoutMs=$WaitTimeoutMs. role=$($entry.Role) pid=$($proc.Id)"
        }
        try {
            $proc.WaitForExit()
        } catch {
        }

        $exitCode = Get-ManualWrapperExitCodeOrNull -Process $proc
        if ($null -eq $exitCode) {
            if (Test-ManualWrapperSuccessMarker -OutPath $entry.OutPath -ErrPath $entry.ErrPath) {
                Write-Warning "manual local wrapper exitCode was empty, but $($entry.Role) wrapper success marker was present and stderr was empty. pid=$($proc.Id)"
                continue
            }
            throw "manual local wrapper failed. role=$($entry.Role) pid=$($proc.Id) exitCode=<empty>. See $($entry.OutPath) / $($entry.ErrPath)"
        }
        if ($exitCode -ne 0) {
            throw "manual local wrapper failed. role=$($entry.Role) pid=$($proc.Id) exitCode=$exitCode"
        }
    }
    Write-Host "NSMB MvL manual local session exited."
}
