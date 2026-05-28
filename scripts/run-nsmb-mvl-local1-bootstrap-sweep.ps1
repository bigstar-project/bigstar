param(
    [string]$SwitchFrames = "1500,1680,1800,2100",
    [int]$Frames = 2400,
    [int]$PortBase = 8300,
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-host.tmp.nds",
    [string]$InputScript = "tests\nsmb_us_direct_mvl_safe_short.inputs",
    [string]$LogRoot = "logs",
    [string]$LogPrefix = "smvl-local1-bootstrap",
    [int]$LookupTickDelay = 60,
    [int]$MaxFrameLead = 20,
    [int]$ScreenshotInterval = 300,
    [int]$GameStateTraceInterval = 60,
    [int]$WaitTimeoutMs = 900000,
    [int]$JobTimeoutSeconds = 960,
    [switch]$PacketBridgeTrace,
    [switch]$NoScreenshots
)

$ErrorActionPreference = "Stop"

$framesToRun = @()
foreach ($part in $SwitchFrames.Split(",")) {
    $trimmed = $part.Trim()
    if ($trimmed -eq "") {
        continue
    }
    $framesToRun += [int]$trimmed
}
if ($framesToRun.Count -eq 0) {
    throw "SwitchFrames is empty"
}

$summary = @()
$index = 0
foreach ($switchFrame in $framesToRun) {
    $port = $PortBase + $index
    $hostLog = Join-Path $LogRoot "$LogPrefix-switch$switchFrame-host-$Frames"
    $clientLog = Join-Path $LogRoot "$LogPrefix-switch$switchFrame-client-$Frames"
    Write-Host "=== local1 bootstrap switchFrame=$switchFrame port=$port ==="

    $childArgs = @{
        Frames = $Frames
        HostRom = $HostRom
        ClientRom = $ClientRom
        InputScript = $InputScript
        HostLogDir = $hostLog
        ClientLogDir = $clientLog
        HostGameLocalPlayerID = "0"
        ClientGameLocalPlayerID = "1"
        GameLocalPlayerIDStartFrame = $switchFrame
        LookupTickDelay = $LookupTickDelay
        MaxFrameLead = $MaxFrameLead
        ScreenshotInterval = $ScreenshotInterval
        GameStateTraceInterval = $GameStateTraceInterval
        WaitTimeoutMs = $WaitTimeoutMs
        JobTimeoutSeconds = $JobTimeoutSeconds
        Port = $port
        NoHashLog = $true
    }
    if ($PacketBridgeTrace) {
        $childArgs.PacketBridgeTrace = $true
    }
    if ($NoScreenshots) {
        $childArgs.NoScreenshots = $true
    }

    $status = "passed"
    $message = ""
    try {
        & .\scripts\run-nsmb-mvl-standard-split.ps1 @childArgs
    } catch {
        $status = "failed"
        $message = $_.Exception.Message
        Write-Warning $message
    }

    $verifyStatus = "skipped"
    try {
        & .\scripts\verify-nsmb-mvl-stable-split.ps1 `
            -HostLogDir $hostLog `
            -ClientLogDir $clientLog `
            -FromFrame 1500 `
            -ToFrame $Frames `
            -RequireNoLifeLossUntilFrame $Frames `
            -RequireStageVisibleScreenshots:(!$NoScreenshots)
        $verifyStatus = "passed"
    } catch {
        $verifyStatus = "failed"
        if ($message -eq "") {
            $message = $_.Exception.Message
        } else {
            $message += " | verify: " + $_.Exception.Message
        }
        Write-Warning ("verify failed: " + $_.Exception.Message)
    }

    $summary += [pscustomobject]@{
        SwitchFrame = $switchFrame
        Status = $status
        Verify = $verifyStatus
        HostLog = $hostLog
        ClientLog = $clientLog
        Message = $message
    }
    $index++
}

$summary | Format-Table -AutoSize
