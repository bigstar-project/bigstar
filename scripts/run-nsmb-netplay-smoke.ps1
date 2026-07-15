param(
    [int]$Frames = 180,
    [int]$Port = 8065,
    [string]$Exe = "build\debug-windows-x86_64\melonDS.exe",
    [string]$Rom = "roms\nsmb.nds",
    [string]$InputScript = "tests\nsmb_smoke.inputs",
    [string]$LogDir = "logs"
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$exePath = (Resolve-Path $Exe).Path
$romPath = (Resolve-Path $Rom).Path
$scriptPath = (Resolve-Path $InputScript).Path
$logRoot = (Resolve-Path $LogDir).Path

$hostOut = Join-Path $logRoot "nsmb-netplay-host.stdout.txt"
$clientOut = Join-Path $logRoot "nsmb-netplay-client.stdout.txt"
$hostHash = Join-Path $logRoot "nsmb-netplay-host.hash.csv"
$clientHash = Join-Path $logRoot "nsmb-netplay-client.hash.csv"
Remove-Item -Force $hostOut, $clientOut, $hostHash, $clientHash -ErrorAction SilentlyContinue

function Start-MelonTestProcess {
    param(
        [string]$Role,
        [string]$Stdout,
        [string]$HashLog
    )

    $path = [System.Environment]::GetEnvironmentVariable("Path", "Process")
    [System.Environment]::SetEnvironmentVariable("Path", $null, "Process")
    [System.Environment]::SetEnvironmentVariable("PATH", $path, "Process")

    $env:MELONDS_NSML_TEST = "1"
    $env:MELONDS_NSML_TEST_FRAMES = "$Frames"
    $env:MELONDS_NSML_INPUT_SCRIPT = $scriptPath
    $env:MELONDS_NSML_HASH_LOG = $HashLog
    $env:MELONDS_NSML_HASH_INTERVAL = "30"
    $env:MELONDS_NSML_WAIT_TIMEOUT_MS = "5000"
    $env:MELONDS_NSML_POC = "1"
    $env:MELONDS_NSML_ROLE = $Role
    $env:MELONDS_NSML_PORT = "$Port"
    $env:MELONDS_NSML_LOCAL_INSTANCE = "0"
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

function Complete-MelonTestProcess {
    param($Started)

    $process = $Started.Process
    if (-not $process.WaitForExit(60000)) {
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

$hostProc = Start-MelonTestProcess -Role "host" -Stdout $hostOut -HashLog $hostHash
Start-Sleep -Milliseconds 500
$clientProc = Start-MelonTestProcess -Role "client" -Stdout $clientOut -HashLog $clientHash

Complete-MelonTestProcess $clientProc
Complete-MelonTestProcess $hostProc

if (-not (Select-String -Path $hostOut -Pattern "NSMB MvL Netplay: peer connected" -Quiet)) {
    throw "host did not report peer connection. See $hostOut"
}

if (-not (Test-Path $hostHash) -or -not (Test-Path $clientHash)) {
    throw "one or both hash logs were not created"
}

Write-Host "NSMB netplay smoke passed: frames=$Frames port=$Port"
