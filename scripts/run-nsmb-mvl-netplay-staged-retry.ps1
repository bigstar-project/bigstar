param(
    [int]$Attempts = 3,
    [int]$Frames = 6200,
    [int]$NetplayStartFrame = 4200,
    [int]$Port = 8130,
    [int]$WaitTimeoutMs = 480000,
    [string]$Seed = "0x00000100",
    [string]$LogDir = "logs\nsmb-mvl-netplay-staged-retry",
    [string]$InputScript = "tests\nsmb_mario_vs_luigi_star_probe.inputs",
    [int]$StateApplyCompareStartFrame = 5100,
    [int]$StateSyncInterval = 5,
    [string]$RamDumpFrames = "",
    [int]$RamDumpInterval = 0,
    [int]$PlayerStickToStarStartFrame = 4380,
    [int]$PlayerStickToStarEndFrame = 4440,
    [int]$PlayerStickToStarSlot = 0
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$failures = @()

for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
    $attemptLogDir = Join-Path $LogDir ("attempt-{0:D2}" -f $attempt)
    $attemptPort = $Port + $attempt - 1
    Write-Host "NSMB staged retry attempt $attempt/$Attempts port=$attemptPort log=$attemptLogDir"

    try {
        & "$PSScriptRoot\run-nsmb-mvl-netplay-staged-smoke.ps1" `
            -Frames $Frames `
            -NetplayStartFrame $NetplayStartFrame `
            -Port $attemptPort `
            -WaitTimeoutMs $WaitTimeoutMs `
            -LogDir $attemptLogDir `
            -InputScript $InputScript `
            -Seed $Seed `
            -GameStateTrace `
            -GameStateTraceInterval 10 `
            -GameStateTraceExtended `
            -StateSync `
            -StateApply `
            -StateApplyCompareStartFrame $StateApplyCompareStartFrame `
            -StateSyncInterval $StateSyncInterval `
            -RamDumpFrames $RamDumpFrames `
            -RamDumpInterval $RamDumpInterval `
            -AllowStateMismatch `
            -PlayerStickToStarStartFrame $PlayerStickToStarStartFrame `
            -PlayerStickToStarEndFrame $PlayerStickToStarEndFrame `
            -PlayerStickToStarSlot $PlayerStickToStarSlot

        Write-Host "NSMB staged retry passed on attempt ${attempt}/${Attempts}: $attemptLogDir"
        exit 0
    } catch {
        $failures += "attempt ${attempt}: $($_.Exception.Message)"
        Write-Warning $failures[-1]
    }
}

throw "all NSMB staged retry attempts failed. $($failures -join ' | ')"
