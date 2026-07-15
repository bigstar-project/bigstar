param(
    [int]$Frames = 5100,
    [int]$NetplayStartFrame = 4500,
    [int]$Port = 8066,
    [string]$Exe = "build\debug-windows-x86_64\melonDS.exe",
    [string]$Rom = "roms\nsmb.nds",
    [string]$InputScript = "tests\nsmb_mario_vs_luigi.inputs",
    [string]$LogDir = "logs"
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$exePath = (Resolve-Path $Exe).Path
$romPath = (Resolve-Path $Rom).Path
$scriptPath = (Resolve-Path $InputScript).Path
$logRoot = (Resolve-Path $LogDir).Path

$hostOut = Join-Path $logRoot "nsmb-mvl-netplay-host.stdout.txt"
$clientOut = Join-Path $logRoot "nsmb-mvl-netplay-client.stdout.txt"
$hostHash = Join-Path $logRoot "nsmb-mvl-netplay-host.hash.csv"
$clientHash = Join-Path $logRoot "nsmb-mvl-netplay-client.hash.csv"
$hostScreens = Join-Path $logRoot "screens-mvl-netplay-host"
$clientScreens = Join-Path $logRoot "screens-mvl-netplay-client"
Remove-Item -Force $hostOut, $clientOut, $hostHash, $clientHash -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $hostScreens, $clientScreens -ErrorAction SilentlyContinue

function Start-MelonRouteNetplayProcess {
    param(
        [string]$Role,
        [int]$LocalInstance,
        [string]$Stdout,
        [string]$HashLog,
        [string]$ScreenshotDir
    )

    $path = [System.Environment]::GetEnvironmentVariable("Path", "Process")
    [System.Environment]::SetEnvironmentVariable("Path", $null, "Process")
    [System.Environment]::SetEnvironmentVariable("PATH", $path, "Process")

    $env:MELONDS_NSML_TEST = "1"
    $env:MELONDS_NSML_TEST_INSTANCES = "2"
    $env:MELONDS_NSML_TEST_FRAMES = "$Frames"
    $env:MELONDS_NSML_INPUT_SCRIPT = $scriptPath
    $env:MELONDS_NSML_HASH_LOG = $HashLog
    $env:MELONDS_NSML_HASH_INTERVAL = "300"
    $env:MELONDS_NSML_SCREENSHOT_DIR = $ScreenshotDir
    $env:MELONDS_NSML_SCREENSHOT_INTERVAL = "600"
    $env:MELONDS_NSML_WAIT_TIMEOUT_MS = "60000"
    $env:MELONDS_NSML_NETPLAY = "1"
    Remove-Item Env:\MELONDS_NSML_POC -ErrorAction SilentlyContinue
    $env:MELONDS_NSML_ROLE = $Role
    $env:MELONDS_NSML_PORT = "$Port"
    $env:MELONDS_NSML_LOCAL_INSTANCE = "$LocalInstance"
    $env:MELONDS_NSML_DELAY = "6"
    $env:MELONDS_NSML_NETPLAY_START_FRAME = "$NetplayStartFrame"
    $env:MELONDS_NSML_NO_LOCAL_WAIT = "1"
    if ($Role -eq "client") {
        $env:MELONDS_NSML_PEER = "127.0.0.1"
    } else {
        Remove-Item Env:\MELONDS_NSML_PEER -ErrorAction SilentlyContinue
    }

    $err = "$Stdout.err"
    Remove-Item -Force $Stdout, $err -ErrorAction SilentlyContinue
    $process = Start-Process -FilePath $exePath `
        -ArgumentList "`"$romPath`"" `
        -WorkingDirectory $logRoot `
        -RedirectStandardOutput $Stdout `
        -RedirectStandardError $err `
        -PassThru
    return [pscustomobject]@{
        Process = $process
        Stdout = $Stdout
        Stderr = $err
    }
}

function Complete-MelonRouteNetplayProcess {
    param($Started)

    $process = $Started.Process
    if (-not $process.WaitForExit(360000)) {
        $process.Kill()
        throw "melonDS process timed out. pid=$($process.Id)"
    }
    $process.Refresh()

    if (Test-Path $Started.Stderr) {
        Add-Content -Path $Started.Stdout -Value (Get-Content $Started.Stderr -Raw)
    }

    if ($null -ne $process.ExitCode -and $process.ExitCode -ne 0) {
        throw "melonDS exited with code $($process.ExitCode). See $($Started.Stdout)"
    }
}

$hostProc = Start-MelonRouteNetplayProcess -Role "host" -LocalInstance 0 -Stdout $hostOut -HashLog $hostHash -ScreenshotDir $hostScreens
Start-Sleep -Milliseconds 500
$clientProc = Start-MelonRouteNetplayProcess -Role "client" -LocalInstance 1 -Stdout $clientOut -HashLog $clientHash -ScreenshotDir $clientScreens

Complete-MelonRouteNetplayProcess $clientProc
Complete-MelonRouteNetplayProcess $hostProc

if (-not (Select-String -Path $hostOut -Pattern "NSMB MvL Netplay: peer connected" -Quiet)) {
    throw "host did not report peer connection. See $hostOut"
}

if (-not (Test-Path $hostHash) -or -not (Test-Path $clientHash)) {
    throw "one or both hash logs were not created"
}

$hostRows = Import-Csv $hostHash
$clientRows = Import-Csv $clientHash
$compareFrame = [Math]::Floor($Frames / 300) * 300
foreach ($rows in @($hostRows, $clientRows)) {
    foreach ($instance in @("0", "1")) {
        $row = $rows | Where-Object { $_.instance -eq $instance -and [int]$_.frame -ge $NetplayStartFrame } | Select-Object -First 1
        if (-not $row) {
            throw "missing post-netplay-start hash row for instance=$instance"
        }
    }
}

foreach ($screenDir in @($hostScreens, $clientScreens)) {
    foreach ($instance in @("inst0", "inst1")) {
        $screens = Get-ChildItem $screenDir -Filter "$($instance)_*.png" -ErrorAction SilentlyContinue
        if (-not ($screens | Where-Object { $_.Name -match "frame00(4[5-9]|5[0-9])" })) {
            throw "missing post-netplay-start screenshots for $instance in $screenDir"
        }
    }
}

Write-Host "NSMB Mario vs Luigi netplay route smoke passed: frames=$Frames start=$NetplayStartFrame compareFrame=$compareFrame"
