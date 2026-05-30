param(
    [ValidateSet("host", "client")]
    [string]$Role,
    [string]$Peer = "127.0.0.1",
    [int]$Port = 8165,
    [int]$Frames = 999999,
    [int]$WaitTimeoutMs = 86400000,
    [int]$InputDelayFrames = 4,
    [int]$InputSendDelayFrames = 0,
    [int]$InputSendJitterFrames = 0,
    [int]$InputMaxFrameLead = 4,
    [int]$InternalWaitTimeoutMs = 0,
    [switch]$InputUnreliable,
    [int]$InputBundleHistory = 8,
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-netaid.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-netaid.tmp.nds",
    [string]$InputScript = "tests\nsmb_us_direct_mvl_minimal_bootstrap.inputs",
    [string]$LogDir = "",
    [int]$SwapBuffersInterval = 1,
    [switch]$UseFrameLimit,
    [switch]$NoFrameLimit,
    [switch]$NoJit,
    [switch]$NoStartBarrier,
    [switch]$NoDynamicCameraLead,
    [switch]$RuntimeDynamicCameraLead,
    [switch]$SoftwareRenderer
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$smokeScript = Join-Path $PSScriptRoot "run-nsmb-mvl-lan-route-smoke.ps1"

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

if ($LogDir -eq "") {
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $LogDir = "logs\nsmb-mvl-manual-peer-$Role-$timestamp"
}

if (-not $InputUnreliable) {
    $InputUnreliable = $true
}

$params = @{
    RunRole = $Role
    Peer = $Peer
    PacketBridgePort = $Port
    Frames = $Frames
    WaitTimeoutMs = $WaitTimeoutMs
    InternalWaitTimeoutMs = $InternalWaitTimeoutMs
    Exe = $Exe
    Rom = "roms\nsmb-us.nds"
    InputScript = $InputScript
    ScreenshotInterval = 0
    NoHashLog = $true
    SkipMvlStateCheck = $true
    SkipGameplayActorCheck = $true
    NoLanMP = $true
    InputNetplay = $true
    InputDelayFrames = $InputDelayFrames
    InputSendDelayFrames = $InputSendDelayFrames
    InputSendJitterFrames = $InputSendJitterFrames
    InputMaxFrameLead = $InputMaxFrameLead
    PacketBridgeJitHelperPatch = $true
    PacketBridgeJitHelperPatchFrame = 840
    PacketBridgeStartFrame = 840
    WaitForPeerAtNetplayStart = (-not $NoStartBarrier)
    ClearMvlCameraInitHold = $true
    ClearMvlCameraInitHoldStartFrame = 840
    DynamicCameraLead = ($RuntimeDynamicCameraLead -and -not $NoDynamicCameraLead)
    LogDir = $LogDir
}

if ($UseFrameLimit -and $NoFrameLimit) {
    throw "UseFrameLimit and NoFrameLimit cannot be used together"
}

if ($NoFrameLimit) {
    $params.NoFrameLimit = $true
}

if ($Role -eq "host") {
    $params.HostRom = $HostRom
} else {
    $params.ClientRom = $ClientRom
}

if (-not $NoJit) {
    $params.AllowJit = $true
}

if ($InputUnreliable) {
    $params.InputUnreliable = $true
    $params.InputBundleHistory = $InputBundleHistory
}

Write-Host "Starting NSMB MvL peer session: role=$Role peer=$Peer port=$Port"
Write-Host "input delay=$InputDelayFrames sendDelay=$InputSendDelayFrames sendJitter=$InputSendJitterFrames max frame lead=$InputMaxFrameLead internalWaitTimeoutMs=$InternalWaitTimeoutMs unreliable=$($InputUnreliable.IsPresent) bundleHistory=$InputBundleHistory jit=$(-not $NoJit)"
Write-Host "frameLimit=$(-not $NoFrameLimit.IsPresent) swapBuffersInterval=$SwapBuffersInterval startBarrier=$(-not $NoStartBarrier) clearMvlCameraInitHold=true runtimeDynamicCameraLead=$($RuntimeDynamicCameraLead -and -not $NoDynamicCameraLead) renderer=$(if ($SoftwareRenderer) { 'software' } else { 'opengl-compute' })"
Write-Host "log=$LogDir"
Write-Host "Host controls Mario. Client controls Luigi."

Push-Location $repoRoot
try {
    $oldSwapBuffersInterval = $env:MELONDS_NSML_SWAPBUFFERS_INTERVAL
    if ($SwapBuffersInterval -gt 1) {
        $env:MELONDS_NSML_SWAPBUFFERS_INTERVAL = "$SwapBuffersInterval"
    } else {
        Remove-Item Env:\MELONDS_NSML_SWAPBUFFERS_INTERVAL -ErrorAction SilentlyContinue
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

    & $smokeScript @params
} finally {
    if ($null -ne $oldSwapBuffersInterval) {
        $env:MELONDS_NSML_SWAPBUFFERS_INTERVAL = $oldSwapBuffersInterval
    } else {
        Remove-Item Env:\MELONDS_NSML_SWAPBUFFERS_INTERVAL -ErrorAction SilentlyContinue
    }
    Pop-Location
}
