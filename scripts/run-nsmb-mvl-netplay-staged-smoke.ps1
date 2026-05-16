param(
    [int]$Frames = 5100,
    [int]$NetplayStartFrame = 4500,
    [int]$Port = 8071,
    [int]$WaitTimeoutMs = 180000,
    [string]$Seed = "0x00000100",
    [switch]$WaitForPeerAtNetplayStart,
    [switch]$NoLocalWait,
    [string]$Exe = "build\debug-windows-x86_64\melonDS.exe",
    [string]$Rom = "roms\nsmb.nds",
    [string]$InputScript = "tests\nsmb_mario_vs_luigi.inputs",
    [string]$LogDir = "logs\nsmb-mvl-netplay-staged"
)

$ErrorActionPreference = "Stop"

$exePath = (Resolve-Path $Exe).Path
$romPath = (Resolve-Path $Rom).Path
$scriptPath = (Resolve-Path $InputScript).Path
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$logRoot = (Resolve-Path $LogDir).Path

$hostRoot = Join-Path $logRoot "host-rom"
$clientRoot = Join-Path $logRoot "client-rom"
New-Item -ItemType Directory -Force -Path $hostRoot, $clientRoot | Out-Null
$hostRom = Join-Path $hostRoot "nsmb.nds"
$clientRom = Join-Path $clientRoot "nsmb.nds"
Copy-Item -Force $romPath $hostRom
Copy-Item -Force $romPath $clientRom

$romBase = [System.IO.Path]::Combine(
    [System.IO.Path]::GetDirectoryName($romPath),
    [System.IO.Path]::GetFileNameWithoutExtension($romPath))
foreach ($suffix in @(".sav", ".sav.2")) {
    $source = "$romBase$suffix"
    if (Test-Path $source) {
        Copy-Item -Force $source (Join-Path $hostRoot "nsmb$suffix")
        Copy-Item -Force $source (Join-Path $clientRoot "nsmb$suffix")
    }
}

$hostOut = Join-Path $logRoot "host.stdout.txt"
$clientOut = Join-Path $logRoot "client.stdout.txt"
$hostHash = Join-Path $logRoot "host.hash.csv"
$clientHash = Join-Path $logRoot "client.hash.csv"
$hostScreens = Join-Path $logRoot "screens-host"
$clientScreens = Join-Path $logRoot "screens-client"
Remove-Item -Force $hostOut, $clientOut, $hostHash, $clientHash, "$hostOut.err", "$clientOut.err" -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $hostScreens, $clientScreens -ErrorAction SilentlyContinue

function Start-MelonStagedProcess {
    param(
        [string]$Role,
        [int]$LocalInstance,
        [string]$RoleRom,
        [string]$Stdout,
        [string]$HashLog,
        [string]$ScreenshotDir
    )

    $env:MELONDS_NSML_TEST = "1"
    $env:MELONDS_NSML_TEST_INSTANCES = "2"
    $env:MELONDS_NSML_TEST_FRAMES = "$Frames"
    $env:MELONDS_NSML_INPUT_SCRIPT = $scriptPath
    $env:MELONDS_NSML_HASH_LOG = $HashLog
    $env:MELONDS_NSML_HASH_INTERVAL = "300"
    $env:MELONDS_NSML_SCREENSHOT_DIR = $ScreenshotDir
    $env:MELONDS_NSML_SCREENSHOT_INTERVAL = "600"
    $env:MELONDS_NSML_WAIT_TIMEOUT_MS = "$WaitTimeoutMs"
    $env:MELONDS_NSML_SEED_WAIT_TIMEOUT_MS = "$WaitTimeoutMs"
    $env:MELONDS_NSML_QUIT_GRACE_MS = "3000"
    $env:MELONDS_NSML_POC = "1"
    $env:MELONDS_NSML_ROLE = $Role
    $env:MELONDS_NSML_PORT = "$Port"
    $env:MELONDS_NSML_LOCAL_INSTANCE = "$LocalInstance"
    $env:MELONDS_NSML_DELAY = "6"
    $env:MELONDS_NSML_NETPLAY_START_FRAME = "$NetplayStartFrame"
    if ($NoLocalWait) {
        $env:MELONDS_NSML_NO_LOCAL_WAIT = "1"
    } else {
        Remove-Item Env:\MELONDS_NSML_NO_LOCAL_WAIT -ErrorAction SilentlyContinue
    }
    $env:MELONDS_NSML_NET_RANDOM_AUTO = "1"
    $env:MELONDS_NSML_NET_RANDOM_VALUE = $Seed
    $env:MELONDS_NSML_DEFER_NETWORK_UNTIL_START = "1"
    $env:MELONDS_NSML_NETPLAY_FRAME_BARRIER = "1"
    $env:MELONDS_NSML_FIXED_RTC = "2020-01-01T00:00:00"
    $env:MELONDS_NSML_DISABLE_JIT = "1"
    if ($Role -eq "host") {
        if ($WaitForPeerAtNetplayStart) {
            $env:MELONDS_NSML_WAIT_FOR_PEER_AT_NETPLAY_START = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_WAIT_FOR_PEER_AT_NETPLAY_START -ErrorAction SilentlyContinue
        }
        Remove-Item Env:\MELONDS_NSML_PEER -ErrorAction SilentlyContinue
    } else {
        Remove-Item Env:\MELONDS_NSML_WAIT_FOR_PEER_AT_NETPLAY_START -ErrorAction SilentlyContinue
        $env:MELONDS_NSML_PEER = "127.0.0.1"
    }

    $err = "$Stdout.err"
    $process = Start-Process -FilePath $exePath `
        -ArgumentList "`"$RoleRom`"" `
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

function Wait-LogPattern {
    param(
        [string]$Path,
        [string]$Pattern,
        [int]$TimeoutMs
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ((Test-Path $Path) -and (Select-String -Path $Path -Pattern $Pattern -Quiet)) {
            return
        }
        Start-Sleep -Milliseconds 100
    }
    throw "timed out waiting for '$Pattern' in $Path"
}

function Wait-HashFrame {
    param(
        [string]$Path,
        [int]$Frame,
        [int]$TimeoutMs
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path $Path) {
            $rows = Import-Csv $Path
            $match = $rows | Where-Object { [int]$_.frame -ge $Frame } | Select-Object -First 1
            if ($match) {
                return
            }
        }
        Start-Sleep -Milliseconds 100
    }
    throw "timed out waiting for frame >= $Frame in $Path"
}

function Complete-MelonStagedProcess {
    param($Started)

    $process = $Started.Process
    if (-not $process.WaitForExit($WaitTimeoutMs + 120000)) {
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

$hostProc = $null
$clientProc = $null
try {
    $hostProc = Start-MelonStagedProcess -Role "host" -LocalInstance 0 -RoleRom $hostRom -Stdout $hostOut -HashLog $hostHash -ScreenshotDir $hostScreens
    if ($WaitForPeerAtNetplayStart) {
        Wait-LogPattern -Path $hostOut -Pattern "waiting for peer at netplay start" -TimeoutMs $WaitTimeoutMs
    } else {
        Wait-HashFrame -Path $hostHash -Frame $NetplayStartFrame -TimeoutMs $WaitTimeoutMs
    }
    $clientProc = Start-MelonStagedProcess -Role "client" -LocalInstance 1 -RoleRom $clientRom -Stdout $clientOut -HashLog $clientHash -ScreenshotDir $clientScreens

    Complete-MelonStagedProcess $clientProc
    Complete-MelonStagedProcess $hostProc
} catch {
    foreach ($started in @($hostProc, $clientProc)) {
        if ($null -ne $started -and $null -ne $started.Process -and -not $started.Process.HasExited) {
            $started.Process.Kill()
        }
    }
    throw
}

foreach ($item in @(
    @{ Path = $hostOut; Pattern = "peer connected"; Name = "host peer connection" },
    @{ Path = $hostOut; Pattern = "patched Net::random.value inst=0"; Name = "host inst0 RNG patch" },
    @{ Path = $hostOut; Pattern = "patched Net::random.value inst=1"; Name = "host inst1 RNG patch" },
    @{ Path = $clientOut; Pattern = "patched Net::random.value inst=0"; Name = "client inst0 RNG patch" },
    @{ Path = $clientOut; Pattern = "patched Net::random.value inst=1"; Name = "client inst1 RNG patch" },
    @{ Path = $hostOut; Pattern = "frame limit reached"; Name = "host frame limit" },
    @{ Path = $clientOut; Pattern = "frame limit reached"; Name = "client frame limit" }
)) {
    if (-not (Select-String -Path $item.Path -Pattern $item.Pattern -Quiet)) {
        throw "missing $($item.Name). See $($item.Path)"
    }
}

if (Select-String -Path $hostOut, $clientOut -Pattern "remote input timeout" -Quiet) {
    throw "remote input timeout was reported. See $logRoot"
}

Write-Host "NSMB Mario vs Luigi staged netplay smoke passed: frames=$Frames start=$NetplayStartFrame seed=$Seed"
