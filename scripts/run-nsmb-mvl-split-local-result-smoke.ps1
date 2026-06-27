param(
    [int]$Frames = 6000,
    [int]$WaitTimeoutMs = 420000,
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$GenerateMvlSourceRom = "roms\nsmb-us.nds",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-netaid.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-netaid.tmp.nds",
    [string]$InputScript = "tests\nsmb_us_direct_mvl_star_collect_left.inputs",
    [int]$InputDelayFrames = 16,
    [int]$InputMaxFrameLead = 2,
    [int]$InputSendDelayFrames = 8,
    [int]$InputSendJitterFrames = 4,
    [switch]$InputUnreliable,
    [int]$InputBundleHistory = 0,
    [switch]$LowDelayWan,
    [int]$ScreenshotInterval = 6000,
    [int]$GameStateTraceInterval = 120,
    [int]$HostStartupDelayMs = 1200,
    [string]$LogDir = "logs\nsmb-mvl-split-local-result-smoke",
    [switch]$SkipRomEnsure
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

if (!$SkipRomEnsure) {
    & (Join-Path $PSScriptRoot "generate-nsmb-mvl-stable-roms.ps1") `
        -SourceRom $GenerateMvlSourceRom `
        -HostRom $HostRom `
        -ClientRom $ClientRom
}

$common = @(
    "-WaitTimeoutMs", "$WaitTimeoutMs",
    "-Frames", "$Frames",
    "-AllowJit",
    "-Exe", $Exe,
    "-InputScript", $InputScript,
    "-GameStateTrace",
    "-GameStateTraceExtended",
    "-GameStateTraceInterval", "$GameStateTraceInterval",
    "-ScreenshotInterval", "$ScreenshotInterval",
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
    "-RequireResultScene",
    "-RequireNetLocalAidStartFrame", "900"
)
if ($InputUnreliable) {
    $common += @("-InputUnreliable", "-InputBundleHistory", "$InputBundleHistory")
}

$hostArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $smokeScript
) + $common + @(
    "-RunRole", "host",
    "-HostRom", $HostRom,
    "-RequireHostResultWinScreenshot",
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
    "-RequireClientResultLoseScreenshot",
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
$clientProc.Refresh()
$hostProc.Refresh()

function Assert-ChildSmokeSucceeded {
    param(
        [string]$Role,
        $Process,
        [string]$Stdout,
        [string]$Stderr
    )

    $stdoutText = if (Test-Path $Stdout) { Get-Content $Stdout -Raw } else { "" }
    $stderrText = if (Test-Path $Stderr) { Get-Content $Stderr -Raw } else { "" }
    $passed = $stdoutText -match "NSMB Mario vs Luigi LAN route smoke passed"
    if ($null -ne $Process.ExitCode -and $Process.ExitCode -ne 0) {
        throw "split $Role failed exit=$($Process.ExitCode): $stderrText $stdoutText"
    }
    if ($null -eq $Process.ExitCode -and -not $passed) {
        throw "split $Role did not report an exit code or pass marker: $stderrText $stdoutText"
    }
}

Assert-ChildSmokeSucceeded -Role "host" -Process $hostProc -Stdout $hostOut -Stderr $hostErr
Assert-ChildSmokeSucceeded -Role "client" -Process $clientProc -Stdout $clientOut -Stderr $clientErr

Get-Content $hostOut
Get-Content $clientOut
Write-Host "NSMB Mario vs Luigi split local result smoke passed: frames=$Frames log=$logRoot"
