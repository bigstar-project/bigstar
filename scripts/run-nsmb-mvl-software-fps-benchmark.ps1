param(
    [int]$Frames = 3600,
    [int]$InputMaxFrameLead = 4,
    [int]$InputSendDelayFrames = 0,
    [int]$InputSendJitterFrames = 0,
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$HostRom = "",
    [string]$ClientRom = "",
    [string]$InputScript = "tests\nsmb_us_direct_mvl_both_different.inputs",
    [string]$LogDir = "logs\nsmb-mvl-software-fps-benchmark",
    [ValidateRange(0, 4)] [int]$MvlStage = 2,
    [string]$MvlMatchSeed = "",
    [ValidateSet("Normal", "AboveNormal", "High")]
    [string]$ProcessPriority = "AboveNormal",
    [UInt64]$HostProcessAffinityMask = 0,
    [UInt64]$ClientProcessAffinityMask = 0,
    [UInt64]$HostEmulationThreadAffinityMask = 0,
    [UInt64]$ClientEmulationThreadAffinityMask = 0,
    [ValidateSet("Auto", "Normal", "AboveNormal", "Highest")]
    [string]$EmulationThreadPriority = "Auto",
    [switch]$Visible,
    [switch]$PerfBreakdown,
    [switch]$PerformanceLog,
    [switch]$SystemTelemetry,
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
            "-Exe", $Exe,
            "-Frames", "$Frames",
            "-InputMaxFrameLead", "$InputMaxFrameLead",
            "-InputSendDelayFrames", "$InputSendDelayFrames",
            "-InputSendJitterFrames", "$InputSendJitterFrames",
            "-InputScript", $InputScript,
            "-MvlStage", "$MvlStage",
            "-MvlCourseMode", "fixed",
            "-ProcessPriority", $ProcessPriority,
            "-ProcessAffinityMask", "$HostProcessAffinityMask",
            "-EmulationThreadAffinityMask", "$HostEmulationThreadAffinityMask",
            "-EmulationThreadPriority", $EmulationThreadPriority,
            "-LogDir", $hostLog
        )
        $clientArgs = @(
            "-NoProfile",
            "-ExecutionPolicy", "Bypass",
            "-File", $peerScript,
            "-Role", "client",
            "-Peer", "127.0.0.1",
            "-SoftwareRenderer",
            "-Exe", $Exe,
            "-Frames", "$Frames",
            "-InputMaxFrameLead", "$InputMaxFrameLead",
            "-InputSendDelayFrames", "$InputSendDelayFrames",
            "-InputSendJitterFrames", "$InputSendJitterFrames",
            "-InputScript", $InputScript,
            "-MvlStage", "$MvlStage",
            "-MvlCourseMode", "fixed",
            "-ProcessPriority", $ProcessPriority,
            "-ProcessAffinityMask", "$ClientProcessAffinityMask",
            "-EmulationThreadAffinityMask", "$ClientEmulationThreadAffinityMask",
            "-EmulationThreadPriority", $EmulationThreadPriority,
            "-LogDir", $clientLog
        )
        if (-not [string]::IsNullOrWhiteSpace($MvlMatchSeed)) {
            $hostArgs += @("-MvlMatchSeed", $MvlMatchSeed)
            $clientArgs += @("-MvlMatchSeed", $MvlMatchSeed)
        }
        if (-not [string]::IsNullOrWhiteSpace($HostRom) -or
            -not [string]::IsNullOrWhiteSpace($ClientRom)) {
            if ([string]::IsNullOrWhiteSpace($HostRom) -or
                [string]::IsNullOrWhiteSpace($ClientRom)) {
                throw "HostRom and ClientRom must be specified together"
            }
            $hostArgs += @("-HostRom", $HostRom, "-ClientRom", $ClientRom, "-SkipRomEnsure")
            $clientArgs += @("-HostRom", $HostRom, "-ClientRom", $ClientRom, "-SkipRomEnsure")
        }
        if ($PerformanceLog) {
            $hostArgs += "-PerformanceLog"
            $clientArgs += "-PerformanceLog"
        }
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
        if ($SystemTelemetry) {
            $counterPaths = @(
                "\Processor(_Total)\% Processor Time",
                "\Processor(_Total)\% DPC Time",
                "\Processor(_Total)\% Interrupt Time",
                "\Processor(_Total)\Interrupts/sec",
                "\Processor(_Total)\DPCs Queued/sec",
                "\Processor Information(_Total)\Actual Frequency",
                "\Processor Information(_Total)\% Processor Performance",
                "\Processor Information(_Total)\% Performance Limit",
                "\Process(melonDS*)\% Processor Time",
                "\Process(melonDS*)\ID Process"
            )
            $telemetry = [System.Collections.Generic.List[object]]::new()
            while (-not $hostProc.HasExited -or -not $clientProc.HasExited) {
                try {
                    $sample = Get-Counter -Counter $counterPaths -MaxSamples 1 -ErrorAction Stop
                } catch {
                    $hostProc.Refresh()
                    $clientProc.Refresh()
                    if ($hostProc.HasExited -and $clientProc.HasExited) {
                        break
                    }
                    Write-Warning "Skipping invalid system telemetry sample: $($_.Exception.Message)"
                    Start-Sleep -Seconds 1
                    continue
                }
                foreach ($counterSample in $sample.CounterSamples) {
                    if ($counterSample.Status -ne 0) {
                        continue
                    }
                    $telemetry.Add([pscustomobject]@{
                        Timestamp = $counterSample.Timestamp.ToString("o")
                        Path = $counterSample.Path
                        Instance = $counterSample.InstanceName
                        CookedValue = $counterSample.CookedValue
                    })
                }
                Start-Sleep -Seconds 1
                $hostProc.Refresh()
                $clientProc.Refresh()
            }
            $telemetry | Export-Csv -LiteralPath "$LogDir-system-telemetry.csv" -NoTypeInformation -Encoding UTF8
        } else {
            Wait-Process -Id $hostProc.Id, $clientProc.Id
        }

        Write-Host "host log: $hostLog\host.stdout.txt"
        Write-Host "client log: $clientLog\client.stdout.txt"
        Select-String -Path "$hostLog\host.stdout.txt","$clientLog\client.stdout.txt" `
            -Pattern "active fps|frame limit reached|input wait stats"
    } else {
        $argsForRun = @{
            LowDelayWan = $true
            SoftwareRenderer = $true
            Exe = $Exe
            Frames = $Frames
            InputMaxFrameLead = $InputMaxFrameLead
            InputSendDelayFrames = $InputSendDelayFrames
            InputSendJitterFrames = $InputSendJitterFrames
            InputScript = $InputScript
            LogDir = $LogDir
            MvlStage = $MvlStage
            MvlCourseMode = "fixed"
            ProcessPriority = $ProcessPriority
        }
        if (-not [string]::IsNullOrWhiteSpace($MvlMatchSeed)) {
            $argsForRun.MvlMatchSeed = $MvlMatchSeed
        }
        if (-not [string]::IsNullOrWhiteSpace($HostRom) -or
            -not [string]::IsNullOrWhiteSpace($ClientRom)) {
            if ([string]::IsNullOrWhiteSpace($HostRom) -or
                [string]::IsNullOrWhiteSpace($ClientRom)) {
                throw "HostRom and ClientRom must be specified together"
            }
            $argsForRun.HostRom = $HostRom
            $argsForRun.ClientRom = $ClientRom
            $argsForRun.SkipRomEnsure = $true
        }
        if ($PerformanceLog) {
            throw "PerformanceLog currently requires -Visible so each peer gets a separate JSONL file"
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
