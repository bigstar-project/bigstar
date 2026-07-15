param(
    [ValidateSet("DirectUdp", "WebRtc")]
    [string]$Mode = "DirectUdp",
    [string]$SignalUrl = "wss://nsmb-mvl-signaling-signaling-prod.uniunitaro.workers.dev/session",
    [string]$RoomCode = "",
    [int]$HostPort = 8165,
    [int]$ClientPort = 8265,
    [int]$Frames = 999999,
    [int]$InputDelayFrames = 4,
    [int]$InputMaxFrameLead = 4,
    [int]$InputBundleHistory = 8,
    [int]$StartupDelayMs = 1500,
    [string]$Exe = "",
    [string]$BridgeExe = "",
    [string]$SourceRom = "roms\nsmb-us.nds",
    [string]$CachedHostRom = "roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-netaid.tmp.nds",
    [string]$CachedClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-netaid.tmp.nds",
    [string]$ExistingHostRom = "",
    [string]$ExistingClientRom = "",
    [string]$InputScript = "tests\nsmb_us_direct_mvl_minimal_bootstrap.inputs",
    [string]$HostInputScript = "",
    [string]$ClientInputScript = "",
    [string]$LogDir = "",
    [int]$MvlStage = -1,
    [ValidateSet(1, 2, 3)] [int]$MvlWins = 2,
    [ValidateSet(3, 5, 10)] [int]$MvlBigStars = 5,
    [ValidateSet("3", "5", "endless", "Endless")] [string]$MvlLives = "endless",
    [ValidateSet("random", "select")] [string]$MvlCourseMode = "random",
    [string]$MvlMatchSeed = "",
    [string]$MvlStageSequence = "",
    [string]$MvlMatchSeedSequence = "",
    [int]$BridgeDelayMs = 0,
    [int]$BridgeJitterMs = 0,
    [int]$BridgeDropModulo = 0,
    [int]$BridgeDropBurstModulo = 0,
    [int]$BridgeDropBurstLen = 0,
    [int]$HostBridgeDelayMs = -1,
    [int]$HostBridgeJitterMs = -1,
    [int]$HostBridgeDropModulo = -1,
    [int]$HostBridgeDropBurstModulo = -1,
    [int]$HostBridgeDropBurstLen = -1,
    [int]$ClientBridgeDelayMs = -1,
    [int]$ClientBridgeJitterMs = -1,
    [int]$ClientBridgeDropModulo = -1,
    [int]$ClientBridgeDropBurstModulo = -1,
    [int]$ClientBridgeDropBurstLen = -1,
    [int]$HostInputSendDelayFrames = 0,
    [int]$HostInputSendJitterFrames = 0,
    [int]$HostInputSendDelayStartFrame = 0,
    [int]$HostInputSendDelayEndFrame = 0,
    [int]$HostInputDropModulo = 0,
    [int]$HostInputDropOffset = 0,
    [int]$HostInputDropStartFrame = 0,
    [int]$HostInputDropEndFrame = 0,
    [int]$ClientInputSendDelayFrames = 0,
    [int]$ClientInputSendJitterFrames = 0,
    [int]$ClientInputSendDelayStartFrame = 0,
    [int]$ClientInputSendDelayEndFrame = 0,
    [int]$ClientInputDropModulo = 0,
    [int]$ClientInputDropOffset = 0,
    [int]$ClientInputDropStartFrame = 0,
    [int]$ClientInputDropEndFrame = 0,
    [ValidateSet("HostFirst", "ClientFirst")]
    [string]$MelonLaunchOrder = "HostFirst",
    [int]$MelonLaunchGapMs = 500,
    [switch]$SkipRomEnsure,
    [switch]$NoJit,
    [switch]$SoftwareRenderer
)

$ErrorActionPreference = "Stop"

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
    if ($MustExist -and -not (Test-Path $resolved)) {
        throw "Path not found: $resolved"
    }
    return $resolved
}

function Resolve-FirstExisting {
    param([string[]]$Candidates)
    foreach ($candidate in $Candidates) {
        $path = Resolve-RepoPath $candidate
        if (Test-Path $path) {
            return (Resolve-Path $path).Path
        }
    }
    throw "None of these paths exist: $($Candidates -join ', ')"
}

function Convert-ToUInt32Setting {
    param(
        [string]$Value,
        [string]$Name
    )
    if ($Value -match '^0x[0-9a-fA-F]+$') {
        return [uint32]::Parse($Value.Substring(2), [System.Globalization.NumberStyles]::HexNumber)
    }
    if ($Value -match '^[0-9]+$') {
        return [uint32]$Value
    }
    throw "$Name must be decimal or 0x-prefixed hex: $Value"
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

function Update-MelonConfigForManualRun {
    param([string]$MelonExe)
    $cfgPath = Join-Path (Split-Path -Parent $MelonExe) "melonDS.toml"
    if (-not (Test-Path $cfgPath)) {
        return
    }
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
        $cfg = Set-MelonTomlValue -Text $cfg -KeyPath $key -Value $replacements[$key]
    }
    Set-Content -Path $cfgPath -Value $cfg -Encoding UTF8
}

function Set-EnvValue {
    param(
        [string]$Name,
        [string]$Value
    )
    if ($Value -eq "") {
        Remove-Item "Env:\$Name" -ErrorAction SilentlyContinue
    } else {
        Set-Item "Env:\$Name" $Value
    }
}

function Invoke-WithMelonEnv {
    param(
        [hashtable]$Env,
        [scriptblock]$Body
    )
    $old = @{}
    foreach ($key in $Env.Keys) {
        $old[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
    }
    try {
        foreach ($key in $Env.Keys) {
            Set-EnvValue -Name $key -Value ([string]$Env[$key])
        }
        & $Body
    } finally {
        foreach ($key in $Env.Keys) {
            if ($null -eq $old[$key]) {
                Remove-Item "Env:\$key" -ErrorAction SilentlyContinue
            } else {
                Set-Item "Env:\$key" $old[$key]
            }
        }
    }
}

function New-MelonEnv {
    param(
        [ValidateSet("host", "client")] [string]$Role,
        [int]$Port,
        [int]$Stage,
        [string]$Seed,
        [string]$RunLogDir
    )
    $localInstance = if ($Role -eq "host") { "0" } else { "1" }
    $roleInputScript = if ($Role -eq "host" -and $HostInputScript -ne "") {
        $HostInputScript
    } elseif ($Role -eq "client" -and $ClientInputScript -ne "") {
        $ClientInputScript
    } else {
        $InputScript
    }
    $env = @{
        MELONDS_NSML_TEST = "1"
        MELONDS_NSML_TEST_INSTANCES = "1"
        MELONDS_NSML_TEST_FRAMES = "$Frames"
        MELONDS_NSML_NETPLAY = "1"
        MELONDS_NSML_ROLE = $Role
        MELONDS_NSML_PORT = "$Port"
        MELONDS_NSML_LOCAL_INSTANCE = $localInstance
        MELONDS_NSML_INPUT_SCRIPT = (Resolve-RepoPath $roleInputScript -MustExist)
        MELONDS_NSML_DISABLE_HASH = "1"
        MELONDS_NSML_SCREENSHOT_DIR = (Join-Path $RunLogDir "screens")
        MELONDS_NSML_SCREENSHOT_INTERVAL = "300"
        MELONDS_NSML_GAME_STATE_TRACE = (Join-Path $RunLogDir "game-state.csv")
        MELONDS_NSML_GAME_STATE_TRACE_INTERVAL = "60"
        MELONDS_NSML_GAME_STATE_TRACE_START_FRAME = "780"
        MELONDS_NSML_GAME_STATE_TRACE_END_FRAME = "3600"
        MELONDS_NSML_GAME_STATE_TRACE_EXTENDED = "1"
        MELONDS_NSML_FIXED_RTC = "2020-01-01T00:00:00"
        MELONDS_NSML_ALLOW_JIT = if ($NoJit) { "" } else { "1" }
        MELONDS_NSML_INPUT_NETPLAY_ONLY = "1"
        MELONDS_NSML_REMOTE_INPUT_TIMEOUT_FATAL = "1"
        MELONDS_NSML_DELAY = "$InputDelayFrames"
        MELONDS_NSML_INPUT_MAX_FRAME_LEAD = "$InputMaxFrameLead"
        MELONDS_NSML_INPUT_UNRELIABLE = "1"
        MELONDS_NSML_INPUT_BUNDLE_HISTORY = "$InputBundleHistory"
        MELONDS_NSML_INPUT_NETPLAY_TRACE = "1"
        MELONDS_NSML_INPUT_HEALTH_TRACE = "1"
        MELONDS_NSML_INPUT_HEALTH_TRACE_INTERVAL = "120"
        MELONDS_NSML_INPUT_HEALTH_TRACE_WAIT_THRESHOLD_MS = "16"
        MELONDS_NSML_STATE_SYNC = "1"
        MELONDS_NSML_STATE_SYNC_INTERVAL = "60"
        MELONDS_NSML_STATE_SYNC_EXTENDED = "1"
        MELONDS_NSML_DIAGNOSTICS_FILE = (Join-Path $RunLogDir "melonds-diagnostics.json")
        MELONDS_NSML_WAIT_FOR_PEER = "1"
        MELONDS_NSML_WAIT_FOR_PEER_AT_NETPLAY_START = "1"
        MELONDS_NSML_DEFER_NETWORK_UNTIL_START = "1"
        MELONDS_NSML_NETPLAY_START_FRAME = "840"
        MELONDS_NSML_PACKET_BRIDGE_JIT_HELPER_PATCH = "1"
        MELONDS_NSML_PACKET_BRIDGE_JIT_HELPER_PATCH_FRAME = "840"
        MELONDS_NSML_CLEAR_MVL_CAMERA_INIT_HOLD = "1"
        MELONDS_NSML_CLEAR_MVL_CAMERA_INIT_HOLD_START_FRAME = "840"
        MELONDS_NSML_MVL_STAGE = "$Stage"
        MELONDS_NSML_MVL_STAGE_SEQUENCE = $MvlStageSequence
        MELONDS_NSML_MVL_COURSE_MODE = $MvlCourseMode
        MELONDS_NSML_MVL_WINS = "$MvlWins"
        MELONDS_NSML_MVL_BIG_STARS = "$MvlBigStars"
        MELONDS_NSML_MVL_LIVES = $MvlLives.ToLowerInvariant()
        MELONDS_NSML_MATCH_SEED = $Seed
        MELONDS_NSML_MATCH_SEED_SEQUENCE = $MvlMatchSeedSequence
    }
    if ($Role -eq "client") {
        $env.MELONDS_NSML_PEER = "127.0.0.1"
    }
    if ($Role -eq "host") {
        $env.MELONDS_NSML_INPUT_SEND_DELAY_FRAMES = "$HostInputSendDelayFrames"
        $env.MELONDS_NSML_INPUT_SEND_JITTER_FRAMES = "$HostInputSendJitterFrames"
        $env.MELONDS_NSML_INPUT_SEND_DELAY_START_FRAME = "$HostInputSendDelayStartFrame"
        $env.MELONDS_NSML_INPUT_SEND_DELAY_END_FRAME = "$HostInputSendDelayEndFrame"
        $env.MELONDS_NSML_INPUT_DROP_MODULO = "$HostInputDropModulo"
        $env.MELONDS_NSML_INPUT_DROP_OFFSET = "$HostInputDropOffset"
        $env.MELONDS_NSML_INPUT_DROP_START_FRAME = "$HostInputDropStartFrame"
        $env.MELONDS_NSML_INPUT_DROP_END_FRAME = "$HostInputDropEndFrame"
    } else {
        $env.MELONDS_NSML_INPUT_SEND_DELAY_FRAMES = "$ClientInputSendDelayFrames"
        $env.MELONDS_NSML_INPUT_SEND_JITTER_FRAMES = "$ClientInputSendJitterFrames"
        $env.MELONDS_NSML_INPUT_SEND_DELAY_START_FRAME = "$ClientInputSendDelayStartFrame"
        $env.MELONDS_NSML_INPUT_SEND_DELAY_END_FRAME = "$ClientInputSendDelayEndFrame"
        $env.MELONDS_NSML_INPUT_DROP_MODULO = "$ClientInputDropModulo"
        $env.MELONDS_NSML_INPUT_DROP_OFFSET = "$ClientInputDropOffset"
        $env.MELONDS_NSML_INPUT_DROP_START_FRAME = "$ClientInputDropStartFrame"
        $env.MELONDS_NSML_INPUT_DROP_END_FRAME = "$ClientInputDropEndFrame"
    }
    if ($MvlWins -gt 1) {
        $env.MELONDS_NSML_MVL_AUTO_RESTART_AFTER_RESULT = "1"
        $env.MELONDS_NSML_MVL_AUTO_RESTART_DELAY_FRAMES = "120"
    }
    return $env
}

if ($LogDir -eq "") {
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $LogDir = "logs\nsmb-mvl-local-triage-$($Mode.ToLowerInvariant())-$timestamp"
}
$logRoot = Resolve-RepoPath $LogDir
New-Item -ItemType Directory -Force $logRoot | Out-Null

if ($MvlMatchSeed -eq "") {
    $firstSeed = @($MvlMatchSeedSequence.Split(",") | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -First 1)
    if ($firstSeed.Count -gt 0) {
        $MvlMatchSeed = $firstSeed[0].Trim()
    } else {
        $MvlMatchSeed = "0x$('{0:x8}' -f (Get-Random -Minimum 0 -Maximum ([int]::MaxValue)))"
    }
}
$seedValue = Convert-ToUInt32Setting -Value $MvlMatchSeed -Name "MvlMatchSeed"
$firstStage = @($MvlStageSequence.Split(",") | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -First 1)
$effectiveStage = if ($MvlStage -ge 0) {
    $MvlStage
} elseif ($firstStage.Count -gt 0) {
    [int]$firstStage[0].Trim()
} else {
    [int]($seedValue % 5)
}
if ($effectiveStage -lt 0 -or $effectiveStage -gt 4) {
    throw "MvlStage must be between 0 and 4: $effectiveStage"
}

if ($Exe -eq "") {
    $Exe = Resolve-FirstExisting @(
        "build\release-windows-x86_64\melonDS.exe",
        "tools\nsmb-mvl-gui\src-tauri\target\release\melonDS.exe"
    )
} else {
    $Exe = Resolve-RepoPath $Exe -MustExist
}

Update-MelonConfigForManualRun -MelonExe $Exe

Write-Host "NSMB MvL local triage"
Write-Host "mode=$Mode log=$logRoot"
Write-Host "melonDS=$Exe"
Write-Host "seed=$MvlMatchSeed stage=$effectiveStage wins=$MvlWins bigStars=$MvlBigStars lives=$MvlLives course=$MvlCourseMode jit=$(-not $NoJit)"

if ($Mode -eq "DirectUdp") {
    $manualLocal = Join-Path $PSScriptRoot "run-nsmb-mvl-manual-local.ps1"
    $manualParams = @{
        Frames = $Frames
        InputDelayFrames = $InputDelayFrames
        InputMaxFrameLead = $InputMaxFrameLead
        InputUnreliable = $true
        InputBundleHistory = $InputBundleHistory
        HostStartupDelayMs = $StartupDelayMs
        Exe = $Exe
        InputScript = (Resolve-RepoPath $InputScript -MustExist)
        LogDir = $LogDir
        ScreenshotInterval = 300
        GameStateTrace = $true
        GameStateTraceInterval = 60
        GameStateTraceStartFrame = 780
        GameStateTraceEndFrame = 3600
        GameStateTraceExtended = $true
        InputNetplayTrace = $true
        MvlStage = $effectiveStage
        MvlWins = $MvlWins
        MvlBigStars = $MvlBigStars
        MvlLives = $MvlLives
        MvlCourseMode = $MvlCourseMode
        GenerateMvlSourceRom = $SourceRom
        MvlMatchSeed = $MvlMatchSeed
    }
    if (-not $NoJit) {
        $manualParams.AllowJit = $true
    }
    if ($SoftwareRenderer) {
        $manualParams.SoftwareRenderer = $true
    }
    & $manualLocal @manualParams
    Write-Host ""
    Write-Host "DirectUdp: WebRTC is not involved. If this reproduces the green/frozen client, focus on Rust ROM generation or melonDS input-netplay/runtime env."
    exit $LASTEXITCODE
}

if ($BridgeExe -eq "") {
    $BridgeExe = Resolve-FirstExisting @(
        "tools\nsmb-net-bridge\target\release\nsmb-net-bridge.exe",
        "tools\nsmb-mvl-gui\src-tauri\binaries\nsmb-net-bridge-x86_64-pc-windows-msvc.exe",
        "tools\nsmb-net-bridge\target\debug\nsmb-net-bridge.exe"
    )
} else {
    $BridgeExe = Resolve-RepoPath $BridgeExe -MustExist
}

if ($RoomCode -eq "") {
    $RoomCode = "triage-$('{0:yyyyMMddHHmmss}' -f (Get-Date))"
}

$hostLog = Join-Path $logRoot "host"
$clientLog = Join-Path $logRoot "client"
New-Item -ItemType Directory -Force $hostLog, $clientLog | Out-Null

$hostRom = Join-Path $logRoot "generated-host.nds"
$clientRom = Join-Path $logRoot "generated-client.nds"
$cachedHostRomPath = Resolve-RepoPath $CachedHostRom
$cachedClientRomPath = Resolve-RepoPath $CachedClientRom
if (!$SkipRomEnsure) {
    & (Join-Path $PSScriptRoot "generate-nsmb-mvl-stable-roms.ps1") `
        -SourceRom (Resolve-RepoPath $SourceRom -MustExist) `
        -HostRom $cachedHostRomPath `
        -ClientRom $cachedClientRomPath `
        -MvlStage $effectiveStage `
        -MvlWins $MvlWins `
        -MvlBigStars $MvlBigStars `
        -MvlLives $MvlLives `
        -MvlCourseMode $MvlCourseMode
} else {
    Resolve-RepoPath $CachedHostRom -MustExist | Out-Null
    Resolve-RepoPath $CachedClientRom -MustExist | Out-Null
}
Copy-Item -LiteralPath $cachedHostRomPath -Destination $hostRom -Force
Copy-Item -LiteralPath $cachedClientRomPath -Destination $clientRom -Force
if ($ExistingHostRom -ne "" -or $ExistingClientRom -ne "") {
    $existingRomPairs = @()
    if ($ExistingHostRom -ne "") {
        $existingHostRomPath = Resolve-RepoPath $ExistingHostRom -MustExist
        Copy-Item -LiteralPath $existingHostRomPath -Destination $hostRom -Force
        $existingRomPairs += @{ Source = $existingHostRomPath; Destination = $hostRom }
    }
    if ($ExistingClientRom -ne "") {
        $existingClientRomPath = Resolve-RepoPath $ExistingClientRom -MustExist
        Copy-Item -LiteralPath $existingClientRomPath -Destination $clientRom -Force
        $existingRomPairs += @{ Source = $existingClientRomPath; Destination = $clientRom }
    }
    foreach ($pair in $existingRomPairs) {
        $sourceSave = [System.IO.Path]::ChangeExtension($pair.Source, ".sav")
        if (Test-Path -LiteralPath $sourceSave) {
            $destinationSave = [System.IO.Path]::ChangeExtension($pair.Destination, ".sav")
            Copy-Item -LiteralPath $sourceSave -Destination $destinationSave -Force
        }
    }
} else {
    & (Join-Path $PSScriptRoot "generate-nsmb-mvl-stable-roms.ps1") `
        -SourceRom (Resolve-RepoPath $SourceRom -MustExist) `
        -HostRom $hostRom `
        -ClientRom $clientRom `
        -MvlStage $effectiveStage `
        -MvlWins $MvlWins `
        -MvlBigStars $MvlBigStars `
        -MvlLives $MvlLives `
        -MvlCourseMode $MvlCourseMode
}

@(
    "mode=$Mode"
    "signalUrl=$SignalUrl"
    "roomCode=$RoomCode"
    "seed=$MvlMatchSeed"
    "seedSequence=$MvlMatchSeedSequence"
    "stage=$effectiveStage"
    "stageSequence=$MvlStageSequence"
    "wins=$MvlWins"
    "bigStars=$MvlBigStars"
    "lives=$MvlLives"
    "courseMode=$MvlCourseMode"
    "existingHostRom=$ExistingHostRom"
    "existingClientRom=$ExistingClientRom"
    "inputScript=$InputScript"
    "hostInputScript=$HostInputScript"
    "clientInputScript=$ClientInputScript"
    "inputBundleHistory=$InputBundleHistory"
    "melonDS=$Exe"
    "bridge=$BridgeExe"
    "bridgeDelayMs=$BridgeDelayMs"
    "bridgeJitterMs=$BridgeJitterMs"
    "bridgeDropModulo=$BridgeDropModulo"
    "bridgeDropBurstModulo=$BridgeDropBurstModulo"
    "bridgeDropBurstLen=$BridgeDropBurstLen"
    "hostBridgeDelayMs=$HostBridgeDelayMs"
    "hostBridgeJitterMs=$HostBridgeJitterMs"
    "hostBridgeDropModulo=$HostBridgeDropModulo"
    "hostBridgeDropBurstModulo=$HostBridgeDropBurstModulo"
    "hostBridgeDropBurstLen=$HostBridgeDropBurstLen"
    "clientBridgeDelayMs=$ClientBridgeDelayMs"
    "clientBridgeJitterMs=$ClientBridgeJitterMs"
    "clientBridgeDropModulo=$ClientBridgeDropModulo"
    "clientBridgeDropBurstModulo=$ClientBridgeDropBurstModulo"
    "clientBridgeDropBurstLen=$ClientBridgeDropBurstLen"
    "hostInputSendDelayFrames=$HostInputSendDelayFrames"
    "hostInputSendJitterFrames=$HostInputSendJitterFrames"
    "hostInputSendDelayStartFrame=$HostInputSendDelayStartFrame"
    "hostInputSendDelayEndFrame=$HostInputSendDelayEndFrame"
    "hostInputDropModulo=$HostInputDropModulo"
    "hostInputDropOffset=$HostInputDropOffset"
    "hostInputDropStartFrame=$HostInputDropStartFrame"
    "hostInputDropEndFrame=$HostInputDropEndFrame"
    "clientInputSendDelayFrames=$ClientInputSendDelayFrames"
    "clientInputSendJitterFrames=$ClientInputSendJitterFrames"
    "clientInputSendDelayStartFrame=$ClientInputSendDelayStartFrame"
    "clientInputSendDelayEndFrame=$ClientInputSendDelayEndFrame"
    "clientInputDropModulo=$ClientInputDropModulo"
    "clientInputDropOffset=$ClientInputDropOffset"
    "clientInputDropStartFrame=$ClientInputDropStartFrame"
    "clientInputDropEndFrame=$ClientInputDropEndFrame"
    "melonLaunchOrder=$MelonLaunchOrder"
    "melonLaunchGapMs=$MelonLaunchGapMs"
) | Set-Content -Encoding UTF8 (Join-Path $logRoot "triage-settings.txt")

$offerArgs = @(
    "webrtc-offer",
    "--local-bind", "127.0.0.1:0",
    "--local-target", "127.0.0.1:$HostPort",
    "--signal", $SignalUrl,
    "--session", $RoomCode
)
$answerArgs = @(
    "webrtc-answer",
    "--local-bind", "127.0.0.1:$ClientPort",
    "--signal", $SignalUrl,
    "--session", $RoomCode
)

function New-BridgeEnv {
    param(
        [int]$RoleDelayMs,
        [int]$RoleJitterMs,
        [int]$RoleDropModulo,
        [int]$RoleDropBurstModulo,
        [int]$RoleDropBurstLen
    )
    $effectiveDelayMs = if ($RoleDelayMs -ge 0) { $RoleDelayMs } else { $BridgeDelayMs }
    $effectiveJitterMs = if ($RoleJitterMs -ge 0) { $RoleJitterMs } else { $BridgeJitterMs }
    $effectiveDropModulo = if ($RoleDropModulo -ge 0) { $RoleDropModulo } else { $BridgeDropModulo }
    $effectiveDropBurstModulo = if ($RoleDropBurstModulo -ge 0) { $RoleDropBurstModulo } else { $BridgeDropBurstModulo }
    $effectiveDropBurstLen = if ($RoleDropBurstLen -ge 0) { $RoleDropBurstLen } else { $BridgeDropBurstLen }
    return @{
        NSMB_NET_BRIDGE_DELAY_MS = "$effectiveDelayMs"
        NSMB_NET_BRIDGE_JITTER_MS = "$effectiveJitterMs"
        NSMB_NET_BRIDGE_DROP_MODULO = "$effectiveDropModulo"
        NSMB_NET_BRIDGE_DROP_BURST_MODULO = "$effectiveDropBurstModulo"
        NSMB_NET_BRIDGE_DROP_BURST_LEN = "$effectiveDropBurstLen"
    }
}

$hostBridgeEnv = New-BridgeEnv -RoleDelayMs $HostBridgeDelayMs -RoleJitterMs $HostBridgeJitterMs -RoleDropModulo $HostBridgeDropModulo -RoleDropBurstModulo $HostBridgeDropBurstModulo -RoleDropBurstLen $HostBridgeDropBurstLen
$clientBridgeEnv = New-BridgeEnv -RoleDelayMs $ClientBridgeDelayMs -RoleJitterMs $ClientBridgeJitterMs -RoleDropModulo $ClientBridgeDropModulo -RoleDropBurstModulo $ClientBridgeDropBurstModulo -RoleDropBurstLen $ClientBridgeDropBurstLen

Invoke-WithMelonEnv -Env $hostBridgeEnv -Body {
    $script:hostBridge = Start-Process -FilePath $BridgeExe -ArgumentList $offerArgs -WorkingDirectory $hostLog `
        -RedirectStandardOutput (Join-Path $hostLog "bridge.stdout.txt") `
        -RedirectStandardError (Join-Path $hostLog "bridge.stderr.txt") `
        -WindowStyle Hidden -PassThru
}
Start-Sleep -Milliseconds 500
Invoke-WithMelonEnv -Env $clientBridgeEnv -Body {
    $script:clientBridge = Start-Process -FilePath $BridgeExe -ArgumentList $answerArgs -WorkingDirectory $clientLog `
        -RedirectStandardOutput (Join-Path $clientLog "bridge.stdout.txt") `
        -RedirectStandardError (Join-Path $clientLog "bridge.stderr.txt") `
        -WindowStyle Hidden -PassThru
}

Start-Sleep -Milliseconds $StartupDelayMs

$hostEnv = New-MelonEnv -Role host -Port $HostPort -Stage $effectiveStage -Seed $MvlMatchSeed -RunLogDir $hostLog
$clientEnv = New-MelonEnv -Role client -Port $ClientPort -Stage $effectiveStage -Seed $MvlMatchSeed -RunLogDir $clientLog

function Start-HostMelon {
    Invoke-WithMelonEnv -Env $hostEnv -Body {
        $script:hostMelon = Start-Process -FilePath $Exe -ArgumentList @($hostRom) -WorkingDirectory $hostLog `
            -RedirectStandardOutput (Join-Path $hostLog "melonds.stdout.txt") `
            -RedirectStandardError (Join-Path $hostLog "melonds.stderr.txt") `
            -PassThru
    }
}

function Start-ClientMelon {
    Invoke-WithMelonEnv -Env $clientEnv -Body {
        $script:clientMelon = Start-Process -FilePath $Exe -ArgumentList @($clientRom) -WorkingDirectory $clientLog `
            -RedirectStandardOutput (Join-Path $clientLog "melonds.stdout.txt") `
            -RedirectStandardError (Join-Path $clientLog "melonds.stderr.txt") `
            -PassThru
    }
}

if ($MelonLaunchOrder -eq "ClientFirst") {
    Start-ClientMelon
    Start-Sleep -Milliseconds $MelonLaunchGapMs
    Start-HostMelon
} else {
    Start-HostMelon
    Start-Sleep -Milliseconds $MelonLaunchGapMs
    Start-ClientMelon
}

Write-Host "WebRtc: GUI is not involved, but nsmb-net-bridge WebRTC is involved."
Write-Host "room=$RoomCode signal=$SignalUrl"
Write-Host "host bridge pid=$($hostBridge.Id) melon pid=$($hostMelon.Id) log=$hostLog"
Write-Host "client bridge pid=$($clientBridge.Id) melon pid=$($clientMelon.Id) log=$clientLog"
Write-Host "If DirectUdp works but WebRtc reproduces the issue, focus on bridge/WebRTC timing or UDP target learning."
