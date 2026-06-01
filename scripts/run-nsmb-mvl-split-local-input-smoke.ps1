param(
    [int]$Frames = 2600,
    [int]$WaitTimeoutMs = 300000,
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-netaid.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-netaid.tmp.nds",
    [string]$HostInputScript = "tests\nsmb_us_direct_mvl_manual_host_mario_move.inputs",
    [string]$ClientInputScript = "tests\nsmb_us_direct_mvl_manual_client_luigi_move.inputs",
    [string]$MvlMatchSeed = "",
    [int]$InputDelayFrames = 16,
    [int]$InputMaxFrameLead = 2,
    [switch]$InputNetplayTrace,
    [int]$InputSendDelayFrames = 0,
    [int]$InputSendJitterFrames = 0,
    [switch]$InputUnreliable,
    [int]$InputBundleHistory = 0,
    [switch]$LowDelayWan,
    [int]$InputDropModulo = 0,
    [int]$InputDropOffset = 0,
    [switch]$Rollback,
    [string]$RollbackBackend = "",
    [int]$RollbackWindow = 20,
    [int]$RollbackCheckpointInterval = 1,
    [int]$RollbackResimulateDelayFrames = 0,
    [switch]$RollbackResimulate,
    [switch]$RollbackRestoreProbe,
    [int]$RollbackSettleFrames = 0,
    [switch]$IgnoreSpeculativeInputFields,
    [int]$GameStateTraceInterval = 30,
    [switch]$NoGameStateTrace,
    [switch]$SkipGameStateComparison,
    [switch]$SkipMovementProbe,
    [switch]$NoFrameLimit,
    [switch]$FixedFrameTime,
    [double]$TargetFps = 0.0,
    [switch]$NoDrawScreen,
    [switch]$NoAudioSync,
    [double]$MaxActiveFrameMs = 0.0,
    [int]$MaxActiveFrameOver25ms = -1,
    [int]$MaxActiveFrameOver33ms = -1,
    [int]$StallTimeoutMs = 0,
    [int]$StallStartFrame = 900,
    [switch]$UseLanMP,
    [switch]$ForceStageActorFreezeFlag,
    [switch]$ForceStageActorFreezeFlagHostOnly,
    [switch]$ForceStageActorFreezeFlagClientOnly,
    [int]$ForceStageActorFreezeFlagStartFrame = 0,
    [int]$ForceStageActorFreezeFlagEndFrame = 0,
    [string]$ForceStageActorFreezeFlagValue = "0",
    [int]$HostStartupDelayMs = 1200,
    [string]$LogDir = "logs\nsmb-mvl-split-local-input-smoke",
    [switch]$AllowJit
)

$ErrorActionPreference = "Stop"

if ($LowDelayWan) {
    $InputDelayFrames = 4
    $InputMaxFrameLead = 4
    $InputSendDelayFrames = 0
    $InputSendJitterFrames = 0
    $InputUnreliable = $true
    $InputBundleHistory = 8
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$smokeScript = Join-Path $PSScriptRoot "run-nsmb-mvl-lan-route-smoke.ps1"
$logRoot = Join-Path $repoRoot $LogDir
$hostLog = Join-Path $logRoot "host"
$clientLog = Join-Path $logRoot "client"
$wrapperLog = Join-Path $logRoot "wrapper"
New-Item -ItemType Directory -Force $wrapperLog | Out-Null
Remove-Item -Recurse -Force $hostLog, $clientLog -ErrorAction SilentlyContinue

$common = @(
    "-WaitTimeoutMs", "$WaitTimeoutMs",
    "-StallTimeoutMs", "$StallTimeoutMs",
    "-StallStartFrame", "$StallStartFrame",
    "-Frames", "$Frames",
    "-Exe", $Exe,
    "-ScreenshotInterval", "0",
    "-NoHashLog",
    "-SkipDisconnectScreenshotCheck",
    "-SkipBlankScreenshotCheck",
    "-SkipMvlStateCheck",
    "-SkipGameplayActorCheck",
    "-InputNetplay",
    "-InputDelayFrames", "$InputDelayFrames",
    "-InputMaxFrameLead", "$InputMaxFrameLead",
    "-InputSendDelayFrames", "$InputSendDelayFrames",
    "-InputSendJitterFrames", "$InputSendJitterFrames",
    "-PacketBridgeJitHelperPatch",
    "-PacketBridgeJitHelperPatchFrame", "870",
    "-PacketBridgeStartFrame", "870",
    "-RequireNetLocalAidStartFrame", "870"
)
if (-not $UseLanMP) {
    $common += "-NoLanMP"
}
if (-not $NoGameStateTrace) {
    $common += @(
        "-GameStateTrace",
        "-GameStateTraceExtended",
        "-GameStateTraceInterval", "$GameStateTraceInterval"
    )
}
if ($AllowJit) {
    $common += "-AllowJit"
}
if ($NoFrameLimit) {
    $common += "-NoFrameLimit"
}
if ($FixedFrameTime) {
    $common += "-FixedFrameTime"
}
if ($TargetFps -gt 0.0) {
    $common += @("-TargetFps", "$TargetFps")
}
if ($NoDrawScreen) {
    $common += "-NoDrawScreen"
}
if ($NoAudioSync) {
    $common += "-NoAudioSync"
}
if ($ForceStageActorFreezeFlag) {
    $common += @(
        "-ForceStageActorFreezeFlag",
        "-ForceStageActorFreezeFlagStartFrame", "$ForceStageActorFreezeFlagStartFrame",
        "-ForceStageActorFreezeFlagEndFrame", "$ForceStageActorFreezeFlagEndFrame",
        "-ForceStageActorFreezeFlagValue", "$ForceStageActorFreezeFlagValue"
    )
    if ($ForceStageActorFreezeFlagHostOnly) {
        $common += "-ForceStageActorFreezeFlagHostOnly"
    }
    if ($ForceStageActorFreezeFlagClientOnly) {
        $common += "-ForceStageActorFreezeFlagClientOnly"
    }
}
if ($InputNetplayTrace) {
    $common += "-InputNetplayTrace"
}
if ($MvlMatchSeed -ne "") {
    $common += @("-MvlMatchSeed", $MvlMatchSeed)
}
if ($InputUnreliable) {
    $common += @("-InputUnreliable", "-InputBundleHistory", "$InputBundleHistory")
}
if ($InputDropModulo -gt 0) {
    $common += @("-InputDropModulo", "$InputDropModulo", "-InputDropOffset", "$InputDropOffset")
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
    if ($RollbackRestoreProbe) {
        $common += "-RollbackRestoreProbe"
    }
}

$hostArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $smokeScript
) + $common + @(
    "-RunRole", "host",
    "-HostRom", $HostRom,
    "-InputScript", $HostInputScript,
    "-LogDir", $hostLog
)
if (-not $NoGameStateTrace) {
    $hostArgs += @("-RequireHostLocalPlayerID", "0", "-RequireHostNetLocalAid", "0")
}

$clientArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $smokeScript
) + $common + @(
    "-RunRole", "client",
    "-Peer", "127.0.0.1",
    "-ClientRom", $ClientRom,
    "-InputScript", $ClientInputScript,
    "-LogDir", $clientLog
)
if (-not $NoGameStateTrace) {
    $clientArgs += @("-RequireClientLocalPlayerID", "1", "-RequireClientNetLocalAid", "1")
}

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

$clientProc.WaitForExit()
$hostProc.WaitForExit()

$hostText = if (Test-Path $hostOut) { Get-Content $hostOut -Raw } else { "" }
$clientText = if (Test-Path $clientOut) { Get-Content $clientOut -Raw } else { "" }
$hostText = [string]$hostText
$clientText = [string]$clientText
$hostMelonOut = Join-Path $hostLog "host.stdout.txt"
$clientMelonOut = Join-Path $clientLog "client.stdout.txt"
$hostMelonText = if (Test-Path $hostMelonOut) { [string](Get-Content $hostMelonOut -Raw) } else { "" }
$clientMelonText = if (Test-Path $clientMelonOut) { [string](Get-Content $clientMelonOut -Raw) } else { "" }
$hostExitFailed = $null -ne $hostProc.ExitCode -and $hostProc.ExitCode -ne 0
$clientExitFailed = $null -ne $clientProc.ExitCode -and $clientProc.ExitCode -ne 0
if ($hostExitFailed -or
    $clientExitFailed -or
    $hostText -notmatch "NSMB Mario vs Luigi LAN route smoke passed" -or
    $clientText -notmatch "NSMB Mario vs Luigi LAN route smoke passed") {
    $details = @()
    foreach ($path in @($hostOut, $hostErr, $clientOut, $clientErr)) {
        if (Test-Path $path) { $details += Get-Content $path -Raw }
    }
    throw "split local-input child smoke failed: hostExit=$($hostProc.ExitCode) clientExit=$($clientProc.ExitCode) $($details -join "`n")"
}

function Assert-ActiveFrameTiming {
    param(
        [string]$Role,
        [string]$Text
    )

    $line = ($Text -split "`r?`n") |
        Where-Object { $_ -match "NSMB Test: active frame timing" } |
        Select-Object -Last 1
    if ($null -eq $line) {
        throw "$Role missing active frame timing line"
    }

    if ($line -notmatch "maxFrameMs=([0-9.]+).*over25ms=([0-9]+).*over33ms=([0-9]+)") {
        throw "$Role malformed active frame timing line: $line"
    }

    $maxFrameMs = [double]$Matches[1]
    $over25ms = [int]$Matches[2]
    $over33ms = [int]$Matches[3]
    if ($MaxActiveFrameMs -gt 0.0 -and $maxFrameMs -gt $MaxActiveFrameMs) {
        throw "$Role active frame spike too high: maxFrameMs=$maxFrameMs limit=$MaxActiveFrameMs"
    }
    if ($MaxActiveFrameOver25ms -ge 0 -and $over25ms -gt $MaxActiveFrameOver25ms) {
        throw "$Role active frame over25ms too high: over25ms=$over25ms limit=$MaxActiveFrameOver25ms"
    }
    if ($MaxActiveFrameOver33ms -ge 0 -and $over33ms -gt $MaxActiveFrameOver33ms) {
        throw "$Role active frame over33ms too high: over33ms=$over33ms limit=$MaxActiveFrameOver33ms"
    }
}

if ($MaxActiveFrameMs -gt 0.0 -or $MaxActiveFrameOver25ms -ge 0 -or $MaxActiveFrameOver33ms -ge 0) {
    Assert-ActiveFrameTiming -Role "host" -Text $hostMelonText
    Assert-ActiveFrameTiming -Role "client" -Text $clientMelonText
}

if ($NoGameStateTrace -or $SkipGameStateComparison) {
    Get-Content $hostOut
    Get-Content $clientOut
    Write-Host "NSMB Mario vs Luigi split local-input smoke passed without game-state comparison: frames=$Frames log=$logRoot"
    return
}

$hostCsv = Join-Path $hostLog "host.game-state.csv"
$clientCsv = Join-Path $clientLog "client.game-state.csv"
$hostRows = Import-Csv $hostCsv
$clientRows = Import-Csv $clientCsv
$clientByFrame = @{}
foreach ($row in $clientRows) {
    $clientByFrame[[int]$row.frame] = $row
}

$stableFields = @(
    "stageID", "stageGroup", "vsMode", "sceneCurrentSceneID",
    "vsStarActorFound", "vsStarActorX", "vsStarActorY",
    "playerActor0Found", "playerActor0X", "playerActor0Y", "playerActor0Z",
    "playerActor1Found", "playerActor1X", "playerActor1Y", "playerActor1Z",
    "movingHazardFound", "movingHazardX", "movingHazardY",
    "objectActiveCount", "objectDeadCount",
    "player0Lives", "player1Lives", "player0BattleStars", "player1BattleStars",
    "player0Dead", "player1Dead", "player0InventoryPowerup", "player1InventoryPowerup",
    "playerGlobalHash", "wifiCandidateHash",
    "playerActor0UpdateLocked", "playerActor1UpdateLocked",
    "playerActor0VisibleFlag", "playerActor1VisibleFlag"
)
$inputFields = @("inputPlayer0Held", "inputPlayer1Held", "inputPlayer0Pressed", "inputPlayer1Pressed")
$fields = if ($IgnoreSpeculativeInputFields) {
    $stableFields
} else {
    @($stableFields + $inputFields)
}

function RowAtFrame {
    param([object[]]$Rows, [int]$Frame)
    return $Rows | Where-Object { [int]$_.frame -eq $Frame } | Select-Object -First 1
}

function RowsMatchFields {
    param([object]$HostRow, [object]$ClientRow, [string[]]$Fields)
    foreach ($field in $Fields) {
        if ($HostRow.$field -ne $ClientRow.$field) {
            return $false
        }
    }
    return $true
}

foreach ($hostRow in $hostRows) {
    $frame = [int]$hostRow.frame
    if ($frame -lt 900) { continue }
    if (-not $clientByFrame.ContainsKey($frame)) {
        throw "missing client frame $frame"
    }
    $clientRow = $clientByFrame[$frame]
    foreach ($field in $fields) {
        if ($hostRow.$field -ne $clientRow.$field) {
            if ($RollbackSettleFrames -gt 0) {
                for ($settleFrame = $frame + 1; $settleFrame -le $frame + $RollbackSettleFrames; $settleFrame++) {
                    $hostSettle = RowAtFrame -Rows $hostRows -Frame $settleFrame
                    $clientSettle = if ($clientByFrame.ContainsKey($settleFrame)) { $clientByFrame[$settleFrame] } else { $null }
                    if ($null -ne $hostSettle -and $null -ne $clientSettle -and
                        (RowsMatchFields -HostRow $hostSettle -ClientRow $clientSettle -Fields $fields)) {
                        Write-Host "rollback transient mismatch settled frame=$frame settleFrame=$settleFrame field=$field"
                        break
                    }
                }
                if ($settleFrame -le $frame + $RollbackSettleFrames) {
                    break
                }
            }
            throw "gameplay mismatch frame=$frame field=$field host=$($hostRow.$field) client=$($clientRow.$field)"
        }
    }
}

if (-not $SkipMovementProbe) {
    $before = RowAtFrame -Rows $hostRows -Frame 1770
    $after = RowAtFrame -Rows $hostRows -Frame 2220
    if ($null -eq $before -or $null -eq $after) {
        throw "missing movement probe rows"
    }
    if ($before.playerActor0X -eq $after.playerActor0X) {
        throw "Mario did not move in host local-input probe"
    }
    if ($before.playerActor1X -eq $after.playerActor1X) {
        throw "Luigi did not move in client local-input probe"
    }
}

Get-Content $hostOut
Get-Content $clientOut
Write-Host "NSMB Mario vs Luigi split local-input smoke passed: frames=$Frames log=$logRoot"
