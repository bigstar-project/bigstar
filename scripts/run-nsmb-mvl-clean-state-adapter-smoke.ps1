param(
    [int]$StateFrame = 3200,
    [int]$Frames = 5000,
    [string]$RouteInputScript = "tests\nsmb_mario_vs_luigi.inputs",
    [string]$AdapterInputScript = "tests\nsmb_after_state_escape.inputs",
    [string]$LogDir = "logs\nsmb-mvl-clean-state-adapter-smoke",
    [int]$WaitTimeoutMs = 420000,
    [int]$VerifyFromFrame = 200,
    [int]$Attempts = 3
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path .
$logRoot = Join-Path $root $LogDir

Remove-Item -Recurse -Force $logRoot -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $logRoot | Out-Null

$sourceFrames = $StateFrame + 10
$lastError = $null

for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
    $attemptRoot = Join-Path $logRoot "attempt$attempt"
    $stateSource = Join-Path $attemptRoot "state-source"
    $stateSplit = Join-Path $attemptRoot "state-split"
    $routeLog = Join-Path $attemptRoot "route"
    $adapterLog = Join-Path $attemptRoot "adapter"

    try {
        Write-Host "Attempt $attempt/$Attempts`: generating clean LocalMP state at frame $StateFrame..."
        & "$root\scripts\run-nsmb-mvl-route-smoke.ps1" `
            -Frames $sourceFrames `
            -LogDir $routeLog `
            -InputScript $RouteInputScript `
            -GameStateTrace `
            -GameStateTraceInterval 100 `
            -GameStateTraceExtended `
            -StateSaveDir $stateSource `
            -StateSaveFrame $StateFrame

        New-Item -ItemType Directory -Force -Path "$stateSplit\host", "$stateSplit\client" | Out-Null
        Copy-Item -Force "$stateSource\inst0.mln" "$stateSplit\host\inst0.mln"
        Copy-Item -Force "$stateSource\inst1.mln" "$stateSplit\client\inst0.mln"

        Write-Host "Attempt $attempt/$Attempts`: running adapter smoke from split state..."
        & "$root\scripts\run-nsmb-mvl-lan-route-smoke.ps1" `
            -WaitTimeoutMs $WaitTimeoutMs `
            -LanStartAttempts 1 `
            -Frames $Frames `
            -HostStartupDelayMs 0 `
            -ScreenshotInterval 500 `
            -InputScript $AdapterInputScript `
            -LogDir $adapterLog `
            -GameStateTrace `
            -GameStateTraceInterval 100 `
            -GameStateTraceExtended `
            -StateLoadDir $stateSplit `
            -StateLoadFrame 1 `
            -PacketBridge `
            -PacketBridgeTrace `
            -PacketBridgeReplayOps keys,byte,tick,action `
            -PacketBridgeReplayReturnLookupTick `
            -PacketBridgeLiveFallbackWindow 16 `
            -PacketBridgeSuppressDisconnect `
            -PacketBridgeBypassNetDisconnect `
            -PacketBridgeForceTransferResult `
            -DropMPAfterFrame 1

        Write-Host "Attempt $attempt/$Attempts`: verifying adapter result..."
        & "$root\scripts\verify-nsmb-mvl-lan-result.ps1" `
            -LogDir $adapterLog `
            -FromFrame $VerifyFromFrame `
            -RequireRemoteInputHits

        Write-Host "Clean-state adapter smoke completed: $adapterLog"
        exit 0
    } catch {
        $lastError = $_
        Write-Warning "Attempt $attempt failed: $($_.Exception.Message)"
    }
}

throw "clean-state adapter smoke failed after $Attempts attempts. Last error: $($lastError.Exception.Message)"
