param(
    [int]$Frames = 2600,
    [int]$WaitTimeoutMs = 300000,
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-rngconst-netaid.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-rngconst-netaid.tmp.nds",
    [string]$HostInputScript = "tests\nsmb_us_direct_mvl_manual_host_mario_move.inputs",
    [string]$ClientInputScript = "tests\nsmb_us_direct_mvl_manual_client_luigi_move.inputs",
    [int]$InputDelayFrames = 16,
    [int]$InputMaxFrameLead = 2,
    [switch]$InputNetplayTrace,
    [int]$InputSendDelayFrames = 0,
    [int]$InputSendJitterFrames = 0,
    [switch]$Rollback,
    [int]$RollbackWindow = 20,
    [int]$RollbackCheckpointInterval = 1,
    [switch]$RollbackResimulate,
    [switch]$RollbackRestoreProbe,
    [int]$RollbackSettleFrames = 0,
    [int]$GameStateTraceInterval = 30,
    [int]$HostStartupDelayMs = 1200,
    [string]$LogDir = "logs\nsmb-mvl-split-local-input-smoke",
    [switch]$AllowJit
)

$ErrorActionPreference = "Stop"

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
    "-Frames", "$Frames",
    "-Exe", $Exe,
    "-GameStateTrace",
    "-GameStateTraceExtended",
    "-GameStateTraceInterval", "$GameStateTraceInterval",
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
    "-PacketBridgeJitHelperPatchFrame", "900",
    "-PacketBridgeStartFrame", "900",
    "-RequireNetLocalAidStartFrame", "900"
)
if ($AllowJit) {
    $common += "-AllowJit"
}
if ($InputNetplayTrace) {
    $common += "-InputNetplayTrace"
}
if ($Rollback) {
    $common += @("-Rollback", "-RollbackWindow", "$RollbackWindow", "-RollbackCheckpointInterval", "$RollbackCheckpointInterval")
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
    "-RequireHostLocalPlayerID", "0",
    "-RequireHostNetLocalAid", "0",
    "-LogDir", $hostLog
)

$clientArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $smokeScript
) + $common + @(
    "-RunRole", "client",
    "-Peer", "127.0.0.1",
    "-ClientRom", $ClientRom,
    "-InputScript", $ClientInputScript,
    "-RequireClientLocalPlayerID", "1",
    "-RequireClientNetLocalAid", "1",
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

$clientProc.WaitForExit()
$hostProc.WaitForExit()

$hostText = if (Test-Path $hostOut) { Get-Content $hostOut -Raw } else { "" }
$clientText = if (Test-Path $clientOut) { Get-Content $clientOut -Raw } else { "" }
if ($hostText -notmatch "NSMB Mario vs Luigi LAN route smoke passed" -or
    $clientText -notmatch "NSMB Mario vs Luigi LAN route smoke passed") {
    $details = @()
    foreach ($path in @($hostOut, $hostErr, $clientOut, $clientErr)) {
        if (Test-Path $path) { $details += Get-Content $path -Raw }
    }
    throw "split local-input child smoke failed: $($details -join "`n")"
}

$hostCsv = Join-Path $hostLog "host.game-state.csv"
$clientCsv = Join-Path $clientLog "client.game-state.csv"
$hostRows = Import-Csv $hostCsv
$clientRows = Import-Csv $clientCsv
$clientByFrame = @{}
foreach ($row in $clientRows) {
    $clientByFrame[[int]$row.frame] = $row
}

$fields = @(
    "stageID", "stageGroup", "vsMode", "sceneCurrentSceneID",
    "inputPlayer0Held", "inputPlayer1Held", "inputPlayer0Pressed", "inputPlayer1Pressed",
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
                $settleFrame = $frame + $RollbackSettleFrames
                $hostSettle = RowAtFrame -Rows $hostRows -Frame $settleFrame
                $clientSettle = if ($clientByFrame.ContainsKey($settleFrame)) { $clientByFrame[$settleFrame] } else { $null }
                if ($null -ne $hostSettle -and $null -ne $clientSettle -and
                    (RowsMatchFields -HostRow $hostSettle -ClientRow $clientSettle -Fields $fields)) {
                    Write-Host "rollback transient mismatch settled frame=$frame settleFrame=$settleFrame field=$field"
                    break
                }
            }
            throw "gameplay mismatch frame=$frame field=$field host=$($hostRow.$field) client=$($clientRow.$field)"
        }
    }
}

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

Get-Content $hostOut
Get-Content $clientOut
Write-Host "NSMB Mario vs Luigi split local-input smoke passed: frames=$Frames log=$logRoot"
