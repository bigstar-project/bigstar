param(
    [string]$HostLogDir = "",
    [string]$ClientLogDir = "",
    [int]$FromFrame = 1500,
    [int]$ToFrame = 0,
    [int]$RequireNoLifeLossUntilFrame = 0,
    [switch]$NoRequireInput,
    [switch]$RequireStageVisibleScreenshots,
    [switch]$RequirePlayerVisibleScreenshots
)

$ErrorActionPreference = "Stop"

if (!$HostLogDir -or !$ClientLogDir) {
    throw "HostLogDir and ClientLogDir are required"
}

$argsForVerifier = @{
    HostLogDir = $HostLogDir
    ClientLogDir = $ClientLogDir
    FromFrame = $FromFrame
}

if ($ToFrame -gt 0) {
    $argsForVerifier.ToFrame = $ToFrame
}

if (-not $NoRequireInput) {
    $argsForVerifier.RequirePlayer0Input = $true
    $argsForVerifier.RequirePlayer1Input = $true
}

if ($RequireNoLifeLossUntilFrame -gt 0) {
    $argsForVerifier.RequireNoLifeLossUntilFrame = $RequireNoLifeLossUntilFrame
}

if ($RequireStageVisibleScreenshots) {
    $argsForVerifier.RequireStageVisibleScreenshots = $true
}

if ($RequirePlayerVisibleScreenshots) {
    $argsForVerifier.RequirePlayerVisibleScreenshots = $true
}

& .\scripts\verify-nsmb-mvl-lan-result.ps1 @argsForVerifier
