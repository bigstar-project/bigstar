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
    [int]$StartupDelayMs = 1500,
    [string]$Exe = "",
    [string]$BridgeExe = "",
    [string]$SourceRom = "roms\nsmb-us.nds",
    [string]$InputScript = "tests\nsmb_us_direct_mvl_minimal_bootstrap.inputs",
    [string]$LogDir = "",
    [int]$MvlStage = -1,
    [ValidateSet(1, 2, 3)] [int]$MvlWins = 2,
    [ValidateSet(3, 5, 10)] [int]$MvlBigStars = 5,
    [ValidateSet("3", "5", "endless", "Endless")] [string]$MvlLives = "endless",
    [ValidateSet("random", "select")] [string]$MvlCourseMode = "random",
    [string]$MvlMatchSeed = "",
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
    $env = @{
        MELONDS_NSML_TEST = "1"
        MELONDS_NSML_TEST_INSTANCES = "1"
        MELONDS_NSML_TEST_FRAMES = "$Frames"
        MELONDS_NSML_POC = "1"
        MELONDS_NSML_ROLE = $Role
        MELONDS_NSML_PORT = "$Port"
        MELONDS_NSML_LOCAL_INSTANCE = $localInstance
        MELONDS_NSML_INPUT_SCRIPT = (Resolve-RepoPath $InputScript -MustExist)
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
        MELONDS_NSML_INPUT_BUNDLE_HISTORY = "8"
        MELONDS_NSML_INPUT_NETPLAY_TRACE = "1"
        MELONDS_NSML_WAIT_FOR_PEER = "1"
        MELONDS_NSML_WAIT_FOR_PEER_AT_NETPLAY_START = "1"
        MELONDS_NSML_DEFER_NETWORK_UNTIL_START = "1"
        MELONDS_NSML_NETPLAY_START_FRAME = "840"
        MELONDS_NSML_PACKET_BRIDGE_JIT_HELPER_PATCH = "1"
        MELONDS_NSML_PACKET_BRIDGE_JIT_HELPER_PATCH_FRAME = "840"
        MELONDS_NSML_CLEAR_MVL_CAMERA_INIT_HOLD = "1"
        MELONDS_NSML_CLEAR_MVL_CAMERA_INIT_HOLD_START_FRAME = "840"
        MELONDS_NSML_MVL_STAGE = "$Stage"
        MELONDS_NSML_DIRECT_MVL_BOOT_STAGE = "$Stage"
        MELONDS_NSML_MVL_COURSE_MODE = $MvlCourseMode
        MELONDS_NSML_MVL_WINS = "$MvlWins"
        MELONDS_NSML_MVL_BIG_STARS = "$MvlBigStars"
        MELONDS_NSML_MVL_LIVES = $MvlLives.ToLowerInvariant()
        MELONDS_NSML_MATCH_SEED = $Seed
    }
    if ($Role -eq "client") {
        $env.MELONDS_NSML_PEER = "127.0.0.1"
    }
    if ($MvlWins -gt 1) {
        $env.MELONDS_NSML_MVL_AUTO_RESTART_AFTER_RESULT = "1"
        $env.MELONDS_NSML_MVL_AUTO_RESTART_DELAY_FRAMES = "120"
    }
    return $env
}

if ($MvlCourseMode -eq "select") {
    throw "MvlCourseMode=select is not supported for direct-route local triage yet. Use -MvlCourseMode random."
}

if ($LogDir -eq "") {
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $LogDir = "logs\nsmb-mvl-local-triage-$($Mode.ToLowerInvariant())-$timestamp"
}
$logRoot = Resolve-RepoPath $LogDir
New-Item -ItemType Directory -Force $logRoot | Out-Null

if ($MvlMatchSeed -eq "") {
    $MvlMatchSeed = "0x$('{0:x8}' -f (Get-Random -Minimum 0 -Maximum ([int]::MaxValue)))"
}
$seedValue = Convert-ToUInt32Setting -Value $MvlMatchSeed -Name "MvlMatchSeed"
$effectiveStage = if ($MvlStage -ge 0) { $MvlStage } else { [int]($seedValue % 5) }
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
        InputBundleHistory = 8
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
        GenerateMvlConfiguredRoms = $true
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
& (Join-Path $PSScriptRoot "generate-nsmb-mvl-stable-roms.ps1") `
    -SourceRom (Resolve-RepoPath $SourceRom -MustExist) `
    -HostRom $hostRom `
    -ClientRom $clientRom `
    -MvlStage $effectiveStage `
    -MvlWins $MvlWins `
    -MvlBigStars $MvlBigStars `
    -MvlLives $MvlLives `
    -MvlCourseMode $MvlCourseMode

@(
    "mode=$Mode"
    "signalUrl=$SignalUrl"
    "roomCode=$RoomCode"
    "seed=$MvlMatchSeed"
    "stage=$effectiveStage"
    "wins=$MvlWins"
    "bigStars=$MvlBigStars"
    "lives=$MvlLives"
    "courseMode=$MvlCourseMode"
    "melonDS=$Exe"
    "bridge=$BridgeExe"
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

$hostBridge = Start-Process -FilePath $BridgeExe -ArgumentList $offerArgs -WorkingDirectory $hostLog `
    -RedirectStandardOutput (Join-Path $hostLog "bridge.stdout.txt") `
    -RedirectStandardError (Join-Path $hostLog "bridge.stderr.txt") `
    -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds 500
$clientBridge = Start-Process -FilePath $BridgeExe -ArgumentList $answerArgs -WorkingDirectory $clientLog `
    -RedirectStandardOutput (Join-Path $clientLog "bridge.stdout.txt") `
    -RedirectStandardError (Join-Path $clientLog "bridge.stderr.txt") `
    -WindowStyle Hidden -PassThru

Start-Sleep -Milliseconds $StartupDelayMs

$hostEnv = New-MelonEnv -Role host -Port $HostPort -Stage $effectiveStage -Seed $MvlMatchSeed -RunLogDir $hostLog
$clientEnv = New-MelonEnv -Role client -Port $ClientPort -Stage $effectiveStage -Seed $MvlMatchSeed -RunLogDir $clientLog

Invoke-WithMelonEnv -Env $hostEnv -Body {
    $script:hostMelon = Start-Process -FilePath $Exe -ArgumentList @($hostRom) -WorkingDirectory $hostLog `
        -RedirectStandardOutput (Join-Path $hostLog "melonds.stdout.txt") `
        -RedirectStandardError (Join-Path $hostLog "melonds.stderr.txt") `
        -PassThru
}
Start-Sleep -Milliseconds 500
Invoke-WithMelonEnv -Env $clientEnv -Body {
    $script:clientMelon = Start-Process -FilePath $Exe -ArgumentList @($clientRom) -WorkingDirectory $clientLog `
        -RedirectStandardOutput (Join-Path $clientLog "melonds.stdout.txt") `
        -RedirectStandardError (Join-Path $clientLog "melonds.stderr.txt") `
        -PassThru
}

Write-Host "WebRtc: GUI is not involved, but nsmb-net-bridge WebRTC is involved."
Write-Host "room=$RoomCode signal=$SignalUrl"
Write-Host "host bridge pid=$($hostBridge.Id) melon pid=$($hostMelon.Id) log=$hostLog"
Write-Host "client bridge pid=$($clientBridge.Id) melon pid=$($clientMelon.Id) log=$clientLog"
Write-Host "If DirectUdp works but WebRtc reproduces the issue, focus on bridge/WebRTC timing or UDP target learning."
