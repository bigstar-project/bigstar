param(
    [int]$Frames = 3600,
    [int]$InputMaxFrameLead = 4,
    [int]$InputSendDelayFrames = 0,
    [int]$InputSendJitterFrames = 0,
    [string]$InputScript = "tests\nsmb_us_direct_mvl_both_different.inputs",
    [string]$LogDir = "logs\nsmb-mvl-software-fps-benchmark",
    [switch]$Visible,
    [switch]$PerfBreakdown,
    [switch]$NoJit
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$localScript = Join-Path $PSScriptRoot "run-nsmb-mvl-manual-local.ps1"
$peerScript = Join-Path $PSScriptRoot "run-nsmb-mvl-manual-peer.ps1"

function Wait-BenchmarkLog {
    param(
        [string[]]$Paths,
        [int]$TimeoutSeconds
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $allDone = $true
        foreach ($path in $Paths) {
            if (-not (Test-Path $path)) {
                $allDone = $false
                break
            }
            $text = Get-Content $path -Raw -ErrorAction SilentlyContinue
            if ($text -notmatch "NSMB Test: frame limit reached") {
                $allDone = $false
                break
            }
        }
        if ($allDone) {
            return
        }
        Start-Sleep -Milliseconds 500
    }

    throw "Timed out waiting for FPS benchmark logs"
}

Push-Location $repoRoot
try {
    $oldPerf = $env:MELONDS_NSML_PERF_BREAKDOWN
    if ($PerfBreakdown) {
        $env:MELONDS_NSML_PERF_BREAKDOWN = "1"
    } else {
        Remove-Item Env:\MELONDS_NSML_PERF_BREAKDOWN -ErrorAction SilentlyContinue
    }

    if ($Visible) {
        $hostLog = "$LogDir-host"
        $clientLog = "$LogDir-client"
        $hostArgs = @(
            "-NoProfile",
            "-ExecutionPolicy", "Bypass",
            "-File", $peerScript,
            "-Role", "host",
            "-SoftwareRenderer",
            "-Frames", "$Frames",
            "-InputMaxFrameLead", "$InputMaxFrameLead",
            "-InputSendDelayFrames", "$InputSendDelayFrames",
            "-InputSendJitterFrames", "$InputSendJitterFrames",
            "-InputScript", $InputScript,
            "-LogDir", $hostLog
        )
        $clientArgs = @(
            "-NoProfile",
            "-ExecutionPolicy", "Bypass",
            "-File", $peerScript,
            "-Role", "client",
            "-Peer", "127.0.0.1",
            "-SoftwareRenderer",
            "-Frames", "$Frames",
            "-InputMaxFrameLead", "$InputMaxFrameLead",
            "-InputSendDelayFrames", "$InputSendDelayFrames",
            "-InputSendJitterFrames", "$InputSendJitterFrames",
            "-InputScript", $InputScript,
            "-LogDir", $clientLog
        )
        if ($NoJit) {
            $hostArgs += "-NoJit"
            $clientArgs += "-NoJit"
        }

        $hostProc = Start-Process -FilePath "powershell.exe" `
            -ArgumentList $hostArgs `
            -WorkingDirectory $repoRoot `
            -PassThru
        Start-Sleep -Milliseconds 1200
        $clientProc = Start-Process -FilePath "powershell.exe" `
            -ArgumentList $clientArgs `
            -WorkingDirectory $repoRoot `
            -PassThru
        Wait-Process -Id $hostProc.Id, $clientProc.Id

        Write-Host "host log: $hostLog\host.stdout.txt"
        Write-Host "client log: $clientLog\client.stdout.txt"
        Select-String -Path "$hostLog\host.stdout.txt","$clientLog\client.stdout.txt" `
            -Pattern "active fps|frame limit reached|input wait stats"
    } else {
        $argsForRun = @{
            LowDelayWan = $true
            SoftwareRenderer = $true
            Frames = $Frames
            InputMaxFrameLead = $InputMaxFrameLead
            InputSendDelayFrames = $InputSendDelayFrames
            InputSendJitterFrames = $InputSendJitterFrames
            InputScript = $InputScript
            LogDir = $LogDir
        }
        if (-not $NoJit) {
            $argsForRun.AllowJit = $true
        }
        & $localScript @argsForRun

        $hostLog = Join-Path $LogDir "host\host.stdout.txt"
        $clientLog = Join-Path $LogDir "client\client.stdout.txt"
        Wait-BenchmarkLog -Paths @($hostLog, $clientLog) -TimeoutSeconds ([Math]::Max(60, [int]($Frames / 30)))
        Write-Host "host log: $hostLog"
        Write-Host "client log: $clientLog"
        Select-String -Path $hostLog,$clientLog `
            -Pattern "active fps|frame limit reached|input wait stats"
    }
} finally {
    if ($null -ne $oldPerf) {
        $env:MELONDS_NSML_PERF_BREAKDOWN = $oldPerf
    } else {
        Remove-Item Env:\MELONDS_NSML_PERF_BREAKDOWN -ErrorAction SilentlyContinue
    }
    Pop-Location
}
