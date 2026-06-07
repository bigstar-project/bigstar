param(
    [int]$Frames = 1600,
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-netaid.tmp.nds",
    [string]$InputScript = "tests\nsmb_us_direct_mvl_manual_host_mario_move.inputs",
    [string]$LogDir = "logs\nsmb-mvl-rule-ai-smoke",
    [ValidateSet("remote", "local", "0", "1", "mario", "luigi")]
    [string]$RuleAIPlayer = "remote",
    [switch]$Trace,
    [int]$TraceInterval = 30,
    [int]$InputDelayFrames = 4,
    [int]$InputMaxFrameLead = 4,
    [int]$JitHelperPatchFrame = 870,
    [switch]$NoDrawScreen,
    [switch]$NoAudioSync,
    [switch]$NoHashLog,
    [switch]$AllowJit
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$smokeScript = Join-Path $repoRoot "scripts\run-nsmb-mvl-lan-route-smoke.ps1"

$oldEnv = @{}
foreach ($name in @(
    "MELONDS_NSML_RULE_AI",
    "MELONDS_NSML_RULE_AI_PLAYER",
    "MELONDS_NSML_RULE_AI_TRACE",
    "MELONDS_NSML_RULE_AI_TRACE_INTERVAL",
    "MELONDS_NSML_RULE_AI_HOST_ONLY",
    "MELONDS_NSML_RULE_AI_CLIENT_ONLY"
)) {
    $oldEnv[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
}

try {
    $env:MELONDS_NSML_RULE_AI = "1"
    $env:MELONDS_NSML_RULE_AI_PLAYER = $RuleAIPlayer
    if ($Trace) {
        $env:MELONDS_NSML_RULE_AI_TRACE = "1"
        $env:MELONDS_NSML_RULE_AI_TRACE_INTERVAL = "$TraceInterval"
    } else {
        Remove-Item Env:\MELONDS_NSML_RULE_AI_TRACE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_RULE_AI_TRACE_INTERVAL -ErrorAction SilentlyContinue
    }
    Remove-Item Env:\MELONDS_NSML_RULE_AI_HOST_ONLY -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_RULE_AI_CLIENT_ONLY -ErrorAction SilentlyContinue

    $smokeArgs = @(
        "-RunRole", "host",
        "-Frames", "$Frames",
        "-Exe", $Exe,
        "-HostRom", $HostRom,
        "-InputScript", $InputScript,
        "-InputNetplay",
        "-PacketBridgeJitHelperPatch",
        "-PacketBridgeJitHelperPatchFrame", "$JitHelperPatchFrame",
        "-InputDelayFrames", "$InputDelayFrames",
        "-InputMaxFrameLead", "$InputMaxFrameLead",
        "-AllowRemoteInputTimeoutFallback",
        "-NoGameStateTrace",
        "-LogDir", $LogDir
    )
    if ($NoDrawScreen) { $smokeArgs += "-NoDrawScreen" }
    if ($NoAudioSync) { $smokeArgs += "-NoAudioSync" }
    if ($NoHashLog) { $smokeArgs += "-NoHashLog" }
    if ($AllowJit) { $smokeArgs += "-AllowJit" }

    $processArgs = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $smokeScript
    ) + $smokeArgs
    & powershell.exe @processArgs
} finally {
    foreach ($entry in $oldEnv.GetEnumerator()) {
        if ($null -eq $entry.Value) {
            [Environment]::SetEnvironmentVariable($entry.Key, $null, "Process")
        } else {
            [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, "Process")
        }
    }
}
