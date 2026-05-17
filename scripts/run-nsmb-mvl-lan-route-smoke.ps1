param(
    [int]$Frames = 4200,
    [int]$WaitTimeoutMs = 240000,
    [string]$Exe = "build\debug-windows-x86_64\melonDS.exe",
    [string]$Rom = "roms\nsmb.nds",
    [string]$InputScript = "tests\nsmb_mario_vs_luigi.inputs",
    [switch]$GameStateTrace,
    [int]$GameStateTraceInterval = 60,
    [switch]$GameStateTraceExtended,
    [string]$RamDumpFrames = "",
    [int]$RamDumpInterval = 0,
    [switch]$LanMPTrace,
    [int]$LanMPTraceDumpLen = 512,
    [string]$HostPacketReplayFile = "",
    [string]$ClientPacketReplayFile = "",
    [string]$LogDir = "logs\nsmb-mvl-lan-route"
)

$ErrorActionPreference = "Stop"

$exePath = (Resolve-Path $Exe).Path
$romPath = (Resolve-Path $Rom).Path
$sourceInputPath = (Resolve-Path $InputScript).Path
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

$hostInput = Join-Path $logRoot "host.inputs"
$clientInput = Join-Path $logRoot "client.inputs"
Remove-Item -Force $hostInput, $clientInput -ErrorAction SilentlyContinue

function Convert-InputScriptForRole {
    param(
        [string]$Source,
        [string]$Destination,
        [string]$RoleInstance
    )

    $out = New-Object System.Collections.Generic.List[string]
    foreach ($line in Get-Content $Source) {
        $trimmed = $line.Trim()
        if (-not $trimmed -or $trimmed.StartsWith("#")) {
            $out.Add($line)
            continue
        }

        if ($trimmed -match "^(inst\d+)\s+(.+)$") {
            if ($matches[1] -eq $RoleInstance) {
                $out.Add($matches[2])
            }
            continue
        }

        $out.Add($line)
    }

    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllLines($Destination, [string[]]$out, $utf8NoBom)
}

Convert-InputScriptForRole -Source $sourceInputPath -Destination $hostInput -RoleInstance "inst0"
Convert-InputScriptForRole -Source $sourceInputPath -Destination $clientInput -RoleInstance "inst1"

$hostOut = Join-Path $logRoot "host.stdout.txt"
$clientOut = Join-Path $logRoot "client.stdout.txt"
$hostHash = Join-Path $logRoot "host.hash.csv"
$clientHash = Join-Path $logRoot "client.hash.csv"
$hostGameStateTrace = Join-Path $logRoot "host.game-state.csv"
$clientGameStateTrace = Join-Path $logRoot "client.game-state.csv"
$hostLanMPTrace = Join-Path $logRoot "host.lanmp.csv"
$clientLanMPTrace = Join-Path $logRoot "client.lanmp.csv"
$hostRamDumps = Join-Path $logRoot "ram-host"
$clientRamDumps = Join-Path $logRoot "ram-client"
$hostScreens = Join-Path $logRoot "screens-host"
$clientScreens = Join-Path $logRoot "screens-client"
Remove-Item -Force $hostOut, $clientOut, $hostHash, $clientHash, "$hostOut.err", "$clientOut.err" -ErrorAction SilentlyContinue
Remove-Item -Force $hostGameStateTrace, $clientGameStateTrace, $hostLanMPTrace, $clientLanMPTrace -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $hostScreens, $clientScreens, $hostRamDumps, $clientRamDumps -ErrorAction SilentlyContinue

function Start-MelonLANProcess {
    param(
        [string]$Role,
        [string]$RoleRom,
        [string]$RoleInput,
        [string]$Stdout,
        [string]$HashLog,
        [string]$ScreenshotDir,
        [string]$GameStateTracePath,
        [string]$LanMPTracePath,
        [string]$PacketReplayFile,
        [string]$RamDumpDir
    )

    $env:MELONDS_NSML_TEST = "1"
    $env:MELONDS_NSML_TEST_INSTANCES = "1"
    $env:MELONDS_NSML_TEST_FRAMES = "$Frames"
    $env:MELONDS_NSML_INPUT_SCRIPT = $RoleInput
    $env:MELONDS_NSML_HASH_LOG = $HashLog
    $env:MELONDS_NSML_HASH_INTERVAL = "300"
    $env:MELONDS_NSML_SCREENSHOT_DIR = $ScreenshotDir
    $env:MELONDS_NSML_SCREENSHOT_INTERVAL = "600"
    if ($GameStateTrace) {
        $env:MELONDS_NSML_GAME_STATE_TRACE = $GameStateTracePath
        $env:MELONDS_NSML_GAME_STATE_TRACE_INTERVAL = "$GameStateTraceInterval"
        if ($GameStateTraceExtended) {
            $env:MELONDS_NSML_GAME_STATE_TRACE_EXTENDED = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE_EXTENDED -ErrorAction SilentlyContinue
        }
    } else {
        Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE_INTERVAL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE_EXTENDED -ErrorAction SilentlyContinue
    }
    if ($RamDumpFrames -or $RamDumpInterval -gt 0) {
        $env:MELONDS_NSML_RAM_DUMP_DIR = $RamDumpDir
        $env:MELONDS_NSML_RAM_DUMP_FRAMES = $RamDumpFrames
        $env:MELONDS_NSML_RAM_DUMP_INTERVAL = "$RamDumpInterval"
    } else {
        Remove-Item Env:\MELONDS_NSML_RAM_DUMP_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_RAM_DUMP_FRAMES -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_RAM_DUMP_INTERVAL -ErrorAction SilentlyContinue
    }
    if ($LanMPTrace) {
        $env:MELONDS_NSML_LANMP_TRACE = $LanMPTracePath
        $env:MELONDS_NSML_LANMP_TRACE_DUMP_LEN = "$LanMPTraceDumpLen"
    } else {
        Remove-Item Env:\MELONDS_NSML_LANMP_TRACE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LANMP_TRACE_DUMP_LEN -ErrorAction SilentlyContinue
    }
    if ($PacketReplayFile) {
        $env:MELONDS_NSML_PACKET_REPLAY_FILE = (Resolve-Path $PacketReplayFile).Path
        $env:MELONDS_NSML_PACKET_REPLAY_LOG = "$Stdout.packet-replay.csv"
    } else {
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_FILE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_LOG -ErrorAction SilentlyContinue
    }
    $env:MELONDS_NSML_FIXED_RTC = "2020-01-01T00:00:00"
    $env:MELONDS_NSML_DISABLE_JIT = "1"
    $env:MELONDS_NSML_MP_INTERFACE = "lan"
    $env:MELONDS_NSML_LAN_ROLE = $Role
    $env:MELONDS_NSML_LAN_PLAYERS = "2"
    $env:MELONDS_NSML_LAN_HOST = "127.0.0.1"
    $env:MELONDS_NSML_LAN_PLAYER = "codex-$Role"
    if ($Role -eq "host") {
        $env:MELONDS_NSML_FIRMWARE_MAC = "00:09:BF:11:22:33"
    } else {
        $env:MELONDS_NSML_FIRMWARE_MAC = "00:09:BF:11:22:43"
    }
    Remove-Item Env:\MELONDS_NSML_POC -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_ROLE -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_PEER -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_PORT -ErrorAction SilentlyContinue
    Remove-Item Env:\MELONDS_NSML_LOCAL_INSTANCE -ErrorAction SilentlyContinue

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

function Complete-MelonLANProcess {
    param($Started)

    $process = $Started.Process
    if (-not $process.WaitForExit($WaitTimeoutMs)) {
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
    $hostProc = Start-MelonLANProcess -Role "host" -RoleRom $hostRom -RoleInput $hostInput -Stdout $hostOut -HashLog $hostHash -ScreenshotDir $hostScreens -GameStateTracePath $hostGameStateTrace -LanMPTracePath $hostLanMPTrace -PacketReplayFile $HostPacketReplayFile -RamDumpDir $hostRamDumps
    Wait-LogPattern -Path $hostOut -Pattern "LAN host start .* ok=1" -TimeoutMs 10000
    $clientProc = Start-MelonLANProcess -Role "client" -RoleRom $clientRom -RoleInput $clientInput -Stdout $clientOut -HashLog $clientHash -ScreenshotDir $clientScreens -GameStateTracePath $clientGameStateTrace -LanMPTracePath $clientLanMPTrace -PacketReplayFile $ClientPacketReplayFile -RamDumpDir $clientRamDumps

    Complete-MelonLANProcess $clientProc
    Complete-MelonLANProcess $hostProc
} catch {
    foreach ($started in @($hostProc, $clientProc)) {
        if ($null -ne $started -and $null -ne $started.Process -and -not $started.Process.HasExited) {
            $started.Process.Kill()
        }
    }
    throw
}

foreach ($item in @(
    @{ Path = $hostOut; Pattern = "LAN host start .* ok=1"; Name = "host LAN start" },
    @{ Path = $clientOut; Pattern = "LAN client start .* ok=1"; Name = "client LAN start" },
    @{ Path = $hostOut; Pattern = "frame limit reached"; Name = "host frame limit" },
    @{ Path = $clientOut; Pattern = "frame limit reached"; Name = "client frame limit" }
)) {
    if (-not (Select-String -Path $item.Path -Pattern $item.Pattern -Quiet)) {
        throw "missing $($item.Name). See $($item.Path)"
    }
}

foreach ($hashLog in @($hostHash, $clientHash)) {
    if (-not (Test-Path $hashLog)) {
        throw "hash log was not created: $hashLog"
    }
    $rows = Import-Csv $hashLog
    if (-not ($rows | Where-Object { $_.instance -eq "0" })) {
        throw "hash log did not contain instance 0 rows: $hashLog"
    }
}

foreach ($screenDir in @($hostScreens, $clientScreens)) {
    $screens = Get-ChildItem $screenDir -Filter "inst0_*.png" -ErrorAction SilentlyContinue
    if (-not $screens) {
        throw "expected screenshots in $screenDir"
    }
}

if ($GameStateTrace) {
    foreach ($item in @(
        @{ Path = $hostGameStateTrace; Role = "host"; LocalPlayerID = "0x0" },
        @{ Path = $clientGameStateTrace; Role = "client"; LocalPlayerID = "0x1" }
    )) {
        if (-not (Test-Path $item.Path)) {
            throw "game state trace was not created for $($item.Role): $($item.Path)"
        }

        $last = Import-Csv $item.Path | Select-Object -Last 1
        if (-not $last) {
            throw "game state trace is empty for $($item.Role): $($item.Path)"
        }

        if ($last.stageGroup -ne "0x9" -or $last.vsMode -ne "0x1" -or $last.localPlayerID -ne $item.LocalPlayerID) {
            throw "Mario vs Luigi state check failed for $($item.Role): stageGroup=$($last.stageGroup) vsMode=$($last.vsMode) localPlayerID=$($last.localPlayerID). See $($item.Path)"
        }
    }
}

Write-Host "NSMB Mario vs Luigi LAN route smoke passed: frames=$Frames"
