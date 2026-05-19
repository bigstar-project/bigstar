param(
    [int]$Frames = 4200,
    [int]$WaitTimeoutMs = 240000,
    [string]$Exe = "build\debug-windows-x86_64\melonDS.exe",
    [string]$Rom = "roms\nsmb.nds",
    [string]$InputScript = "tests\nsmb_mario_vs_luigi.inputs",
    [switch]$GameStateTrace,
    [int]$GameStateTraceInterval = 60,
    [switch]$GameStateTraceExtended,
    [switch]$StateSync,
    [switch]$StateApply,
    [int]$StateSyncInterval = 60,
    [switch]$StateSyncExtended,
    [string]$StateApplyMode = "",
    [int]$ScreenshotInterval = 600,
    [string]$RamDumpFrames = "",
    [int]$RamDumpInterval = 0,
    [string]$StateSaveDir = "",
    [int]$StateSaveFrame = 0,
    [string]$StateLoadDir = "",
    [int]$StateLoadFrame = -1,
    [switch]$LanMPTrace,
    [int]$LanMPTraceDumpLen = 512,
    [string]$HostPacketReplayFile = "",
    [string]$ClientPacketReplayFile = "",
    [switch]$PacketCapture,
    [switch]$PacketCaptureAllowPreGame,
    [switch]$PacketBridge,
    [switch]$PacketBridgeAllowPreGame,
    [switch]$PacketBridgeTrace,
    [int]$PacketBridgePort = 8165,
    [int]$PacketBridgeStartFrame = 0,
    [int]$PacketBridgeReplayTickOffset = 0,
    [int]$HostPacketBridgeReplayTickOffset = -1,
    [int]$ClientPacketBridgeReplayTickOffset = -1,
    [switch]$PacketBridgeWait,
    [int]$PacketBridgeWaitTimeoutMs = 5,
    [switch]$PacketBridgeStrictRemote,
    [string]$PacketBridgeStrictPlayers = "",
    [int]$PacketBridgeStrictStartFrame = 0,
    [int]$PacketBridgeStrictRequireLead = 0,
    [int]$PacketBridgeLiveFallbackWindow = 0,
    [switch]$PacketBridgeReplayReturnLookupTick,
    [string]$PacketBridgeReplayOps = "",
    [switch]$PacketBridgeDirectCapture,
    [switch]$PacketBridgeForceTick,
    [int]$PacketBridgeForceTickStartFrame = 0,
    [int]$PacketBridgeForceTickBase = -1,
    [switch]$PacketBridgeForceNetReady,
    [int]$PacketBridgeForceNetReadyStartFrame = 0,
    [switch]$PacketBridgeForceLoadGameSM,
    [int]$PacketBridgeForceLoadGameSMStartFrame = 0,
    [int]$PacketBridgeForceLoadGameSMStep = 3,
    [int]$PacketBridgeForceLoadGameSMTimer = -1,
    [switch]$PacketBridgeForceLoadGameSMRunUpdate,
    [switch]$PacketBridgeForceLoadGameSMRunUpdateAll,
    [int]$PacketBridgeLookupTickDelay = 0,
    [int]$PacketBridgeMaxPumpEvents = 64,
    [switch]$PacketBridgeSuppressDisconnect,
    [switch]$PacketBridgeSuppressBlackout,
    [switch]$PacketBridgePreserveNetPointers,
    [switch]$PacketBridgeBypassNetReset,
    [switch]$PacketBridgeBypassNetDisconnect,
    [int]$PacketBridgeBypassNetDisconnectStartFrame = 0,
    [string]$PacketBridgeBypassNetDisconnectMode = "skip",
    [switch]$PacketBridgeForceTransferResult,
    [int]$PacketBridgeForceTransferStartFrame = 0,
    [int]$PacketBridgeForceTransferResultValue = 8,
    [string]$NetRandomValue = "",
    [int]$NetRandomFrame = 0,
    [switch]$NetRandomAuto,
    [int]$PacketBridgeMaxTickLead = -1,
    [int]$PacketBridgeMaxFrameLead = -1,
    [int]$PacketBridgeThrottleTimeoutMs = 5000,
    [int]$DropMPAfterFrame = 0,
    [switch]$LanWanMode,
    [switch]$NoLanMP,
    [int]$LanMPRecvTimeoutMs = -1,
    [int]$LanMPMiscRecvTimeoutMs = -1,
    [int]$LanMPStaleMs = -1,
    [int]$LanMPSendDelayMs = -1,
    [int]$HostLanMPSendDelayMs = -1,
    [int]$ClientLanMPSendDelayMs = -1,
    [switch]$LanMPReliable,
    [switch]$LanMPAcceptAnyChannel,
    [switch]$DirectMvlBoot,
    [int]$DirectMvlBootFrame = 900,
    [int]$DirectMvlBootStage = 0,
    [switch]$DirectMvlBootLoadSM,
    [switch]$DirectMvlBootPatchLoadSMOnly,
    [switch]$DirectMvlBootCallUpdateSM,
    [switch]$DirectMvlBootCallStartLoad,
    [switch]$DirectMvlBootCallCourseSelect,
    [switch]$DirectMvlBootCallObjectCourseSelect,
    [switch]$SafeStartLoadCall,
    [int]$SafeStartLoadCallFrame = 1900,
    [string]$SafeStartLoadCallPC = "0x0200F944",
    [switch]$SafeLoadLevelCall,
    [int]$SafeLoadLevelCallFrame = 1600,
    [string]$SafeLoadLevelCallPC = "0x0200F944",
    [switch]$SafeCourseSelectCall,
    [int]$SafeCourseSelectCallFrame = 1900,
    [string]$SafeCourseSelectCallPC = "0x0200F944",
    [switch]$SafeCourseSelectFactoryCall,
    [int]$SafeCourseSelectFactoryCallFrame = 1900,
    [string]$SafeCourseSelectFactoryCallPC = "0x0200F944",
    [string]$HostSafeCourseSelectFactoryCallPC = "",
    [string]$ClientSafeCourseSelectFactoryCallPC = "",
    [switch]$SafeUpdateLoadGameCall,
    [int]$SafeUpdateLoadGameCallFrame = 1900,
    [string]$SafeUpdateLoadGameCallPC = "0x0200F944",
    [string]$HostSafeUpdateLoadGameCallPC = "",
    [string]$ClientSafeUpdateLoadGameCallPC = "",
    [switch]$ForceCourseSelectFactory,
    [switch]$ForceCourseSelectFactoryClientOnly,
    [int]$ForceCourseSelectFactoryFrame = 2300,
    [int]$HostForceCourseSelectFactoryPlayerArg = 1,
    [int]$ClientForceCourseSelectFactoryPlayerArg = 0,
    [switch]$CallTrace,
    [string]$CallTraceAddrs = "",
    [int]$CallTraceStartFrame = 0,
    [int]$CallTraceEndFrame = 0,
    [switch]$WriteTrace,
    [string]$WriteTraceAddrs = "",
    [int]$WriteTraceStartFrame = 0,
    [int]$WriteTraceEndFrame = 0,
    [int]$HostStartupDelayMs = 1000,
    [int]$LanStartAttempts = 1,
    [switch]$SkipDisconnectScreenshotCheck,
    [switch]$SkipGameplayActorCheck,
    [string]$LogDir = "logs\nsmb-mvl-lan-route"
)

$ErrorActionPreference = "Stop"

if ($LanStartAttempts -gt 1) {
    for ($attempt = 1; $attempt -le $LanStartAttempts; $attempt++) {
        $attemptParams = @{} + $PSBoundParameters
        $attemptParams["LanStartAttempts"] = 1
        $attemptParams["LogDir"] = "$LogDir-attempt$attempt"
        try {
            & $PSCommandPath @attemptParams
            return
        } catch {
            $message = $_.Exception.Message
            $retryable =
                $message -like "*LAN start*" -or
                $message -like "*missing client LAN start*" -or
                $message -like "*missing host LAN start*" -or
                $message -like "*missing client frame limit*" -or
                $message -like "*missing host frame limit*" -or
                $message -like "*connection-dialog screenshot detected*" -or
                $message -like "*stageGroup=0x0 vsMode=0x0*"
            if (-not $retryable -or $attempt -ge $LanStartAttempts) {
                throw
            }
            Write-Host "retrying LAN route smoke after retryable startup failure: attempt $attempt/$LanStartAttempts"
            Start-Sleep -Milliseconds 500
        }
    }
}

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
$hostPacketCapture = Join-Path $logRoot "host.packet-capture.csv"
$clientPacketCapture = Join-Path $logRoot "client.packet-capture.csv"
$hostRamDumps = Join-Path $logRoot "ram-host"
$clientRamDumps = Join-Path $logRoot "ram-client"
$hostScreens = Join-Path $logRoot "screens-host"
$clientScreens = Join-Path $logRoot "screens-client"
Remove-Item -Force $hostOut, $clientOut, $hostHash, $clientHash, "$hostOut.err", "$clientOut.err" -ErrorAction SilentlyContinue
Remove-Item -Force $hostGameStateTrace, $clientGameStateTrace, $hostLanMPTrace, $clientLanMPTrace, $hostPacketCapture, $clientPacketCapture -ErrorAction SilentlyContinue
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
        [string]$PacketCapturePath,
        [string]$RamDumpDir
    )

    $env:MELONDS_NSML_TEST = "1"
    $env:MELONDS_NSML_TEST_INSTANCES = "1"
    $env:MELONDS_NSML_TEST_FRAMES = "$Frames"
    $env:MELONDS_NSML_INPUT_SCRIPT = $RoleInput
    $env:MELONDS_NSML_HASH_LOG = $HashLog
    $env:MELONDS_NSML_HASH_INTERVAL = "300"
    $env:MELONDS_NSML_SCREENSHOT_DIR = $ScreenshotDir
    $env:MELONDS_NSML_SCREENSHOT_INTERVAL = "$ScreenshotInterval"
    if ($DirectMvlBoot) {
        $env:MELONDS_NSML_DIRECT_MVL_BOOT = "1"
        $env:MELONDS_NSML_DIRECT_MVL_BOOT_FRAME = "$DirectMvlBootFrame"
        $env:MELONDS_NSML_DIRECT_MVL_BOOT_STAGE = "$DirectMvlBootStage"
        $env:MELONDS_NSML_DIRECT_MVL_BOOT_PLAYER_ID = $(if ($Role -eq "client") { "1" } else { "0" })
        if ($DirectMvlBootLoadSM) { $env:MELONDS_NSML_DIRECT_MVL_BOOT_LOAD_SM = "1" } else { Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_LOAD_SM -ErrorAction SilentlyContinue }
        if ($DirectMvlBootPatchLoadSMOnly) { $env:MELONDS_NSML_DIRECT_MVL_BOOT_PATCH_LOAD_SM_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_PATCH_LOAD_SM_ONLY -ErrorAction SilentlyContinue }
        if ($DirectMvlBootCallUpdateSM) { $env:MELONDS_NSML_DIRECT_MVL_BOOT_CALL_UPDATE_SM = "1" } else { Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_CALL_UPDATE_SM -ErrorAction SilentlyContinue }
        if ($DirectMvlBootCallStartLoad) { $env:MELONDS_NSML_DIRECT_MVL_BOOT_CALL_START_LOAD = "1" } else { Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_CALL_START_LOAD -ErrorAction SilentlyContinue }
        if ($DirectMvlBootCallCourseSelect) { $env:MELONDS_NSML_DIRECT_MVL_BOOT_CALL_COURSE_SELECT = "1" } else { Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_CALL_COURSE_SELECT -ErrorAction SilentlyContinue }
        if ($DirectMvlBootCallObjectCourseSelect) { $env:MELONDS_NSML_DIRECT_MVL_BOOT_CALL_OBJECT_COURSE_SELECT = "1" } else { Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_CALL_OBJECT_COURSE_SELECT -ErrorAction SilentlyContinue }
    } else {
        Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_STAGE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_PLAYER_ID -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_LOAD_SM -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_PATCH_LOAD_SM_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_CALL_UPDATE_SM -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_CALL_START_LOAD -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_CALL_COURSE_SELECT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_CALL_OBJECT_COURSE_SELECT -ErrorAction SilentlyContinue
    }
    if ($SafeStartLoadCall) {
        $env:MELONDS_NSML_SAFE_START_LOAD_CALL = "1"
        $env:MELONDS_NSML_SAFE_START_LOAD_CALL_FRAME = "$SafeStartLoadCallFrame"
        $env:MELONDS_NSML_SAFE_START_LOAD_CALL_PC = "$SafeStartLoadCallPC"
    } else {
        Remove-Item Env:\MELONDS_NSML_SAFE_START_LOAD_CALL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_START_LOAD_CALL_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_START_LOAD_CALL_PC -ErrorAction SilentlyContinue
    }
    if ($SafeLoadLevelCall) {
        $env:MELONDS_NSML_SAFE_LOAD_LEVEL_CALL = "1"
        $env:MELONDS_NSML_SAFE_LOAD_LEVEL_CALL_FRAME = "$SafeLoadLevelCallFrame"
        $env:MELONDS_NSML_SAFE_LOAD_LEVEL_CALL_PC = "$SafeLoadLevelCallPC"
        $env:MELONDS_NSML_SAFE_LOAD_LEVEL_PLAYER_ID = $(if ($Role -eq "client") { "1" } else { "0" })
    } else {
        Remove-Item Env:\MELONDS_NSML_SAFE_LOAD_LEVEL_CALL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_LOAD_LEVEL_CALL_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_LOAD_LEVEL_CALL_PC -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_LOAD_LEVEL_PLAYER_ID -ErrorAction SilentlyContinue
    }
    if ($SafeCourseSelectCall) {
        $env:MELONDS_NSML_SAFE_COURSE_SELECT_CALL = "1"
        $env:MELONDS_NSML_SAFE_COURSE_SELECT_CALL_FRAME = "$SafeCourseSelectCallFrame"
        $env:MELONDS_NSML_SAFE_COURSE_SELECT_CALL_PC = "$SafeCourseSelectCallPC"
    } else {
        Remove-Item Env:\MELONDS_NSML_SAFE_COURSE_SELECT_CALL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_COURSE_SELECT_CALL_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_COURSE_SELECT_CALL_PC -ErrorAction SilentlyContinue
    }
    if ($SafeCourseSelectFactoryCall) {
        $env:MELONDS_NSML_SAFE_COURSE_SELECT_FACTORY_CALL = "1"
        $env:MELONDS_NSML_SAFE_COURSE_SELECT_FACTORY_CALL_FRAME = "$SafeCourseSelectFactoryCallFrame"
        $roleSafeCourseSelectFactoryCallPC = $SafeCourseSelectFactoryCallPC
        if ($Role -eq "host" -and $HostSafeCourseSelectFactoryCallPC) {
            $roleSafeCourseSelectFactoryCallPC = $HostSafeCourseSelectFactoryCallPC
        } elseif ($Role -eq "client" -and $ClientSafeCourseSelectFactoryCallPC) {
            $roleSafeCourseSelectFactoryCallPC = $ClientSafeCourseSelectFactoryCallPC
        }
        $env:MELONDS_NSML_SAFE_COURSE_SELECT_FACTORY_CALL_PC = "$roleSafeCourseSelectFactoryCallPC"
    } else {
        Remove-Item Env:\MELONDS_NSML_SAFE_COURSE_SELECT_FACTORY_CALL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_COURSE_SELECT_FACTORY_CALL_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_COURSE_SELECT_FACTORY_CALL_PC -ErrorAction SilentlyContinue
    }
    if ($SafeUpdateLoadGameCall) {
        $env:MELONDS_NSML_SAFE_UPDATE_LOAD_GAME_CALL = "1"
        $env:MELONDS_NSML_SAFE_UPDATE_LOAD_GAME_CALL_FRAME = "$SafeUpdateLoadGameCallFrame"
        $roleSafeUpdateLoadGameCallPC = $SafeUpdateLoadGameCallPC
        if ($Role -eq "host" -and $HostSafeUpdateLoadGameCallPC) {
            $roleSafeUpdateLoadGameCallPC = $HostSafeUpdateLoadGameCallPC
        } elseif ($Role -eq "client" -and $ClientSafeUpdateLoadGameCallPC) {
            $roleSafeUpdateLoadGameCallPC = $ClientSafeUpdateLoadGameCallPC
        }
        $env:MELONDS_NSML_SAFE_UPDATE_LOAD_GAME_CALL_PC = "$roleSafeUpdateLoadGameCallPC"
    } else {
        Remove-Item Env:\MELONDS_NSML_SAFE_UPDATE_LOAD_GAME_CALL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_UPDATE_LOAD_GAME_CALL_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_UPDATE_LOAD_GAME_CALL_PC -ErrorAction SilentlyContinue
    }
    if ($ForceCourseSelectFactory -and (-not $ForceCourseSelectFactoryClientOnly -or $Role -eq "client")) {
        $env:MELONDS_NSML_FORCE_COURSE_SELECT_FACTORY = "1"
        $env:MELONDS_NSML_FORCE_COURSE_SELECT_FACTORY_FRAME = "$ForceCourseSelectFactoryFrame"
        if ($Role -eq "host") {
            $env:MELONDS_NSML_FORCE_COURSE_SELECT_FACTORY_PLAYER_ARG = "$HostForceCourseSelectFactoryPlayerArg"
        } else {
            $env:MELONDS_NSML_FORCE_COURSE_SELECT_FACTORY_PLAYER_ARG = "$ClientForceCourseSelectFactoryPlayerArg"
        }
    } else {
        Remove-Item Env:\MELONDS_NSML_FORCE_COURSE_SELECT_FACTORY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_COURSE_SELECT_FACTORY_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_COURSE_SELECT_FACTORY_PLAYER_ARG -ErrorAction SilentlyContinue
    }
    if ($CallTrace) {
        $env:MELONDS_NSML_CALL_TRACE = "1"
        $env:MELONDS_NSML_CALL_TRACE_LOG = "$Stdout.call-trace.csv"
        if ($CallTraceAddrs) { $env:MELONDS_NSML_CALL_TRACE_ADDRS = $CallTraceAddrs } else { Remove-Item Env:\MELONDS_NSML_CALL_TRACE_ADDRS -ErrorAction SilentlyContinue }
        if ($CallTraceStartFrame -gt 0) { $env:MELONDS_NSML_CALL_TRACE_START_FRAME = "$CallTraceStartFrame" } else { Remove-Item Env:\MELONDS_NSML_CALL_TRACE_START_FRAME -ErrorAction SilentlyContinue }
        if ($CallTraceEndFrame -gt 0) { $env:MELONDS_NSML_CALL_TRACE_END_FRAME = "$CallTraceEndFrame" } else { Remove-Item Env:\MELONDS_NSML_CALL_TRACE_END_FRAME -ErrorAction SilentlyContinue }
    } else {
        Remove-Item Env:\MELONDS_NSML_CALL_TRACE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_CALL_TRACE_LOG -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_CALL_TRACE_ADDRS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_CALL_TRACE_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_CALL_TRACE_END_FRAME -ErrorAction SilentlyContinue
    }
    if ($WriteTrace) {
        $env:MELONDS_NSML_WRITE_TRACE = "1"
        $env:MELONDS_NSML_WRITE_TRACE_LOG = "$Stdout.write-trace.csv"
        if ($WriteTraceAddrs) { $env:MELONDS_NSML_WRITE_TRACE_ADDRS = $WriteTraceAddrs } else { Remove-Item Env:\MELONDS_NSML_WRITE_TRACE_ADDRS -ErrorAction SilentlyContinue }
        if ($WriteTraceStartFrame -gt 0) { $env:MELONDS_NSML_WRITE_TRACE_START_FRAME = "$WriteTraceStartFrame" } else { Remove-Item Env:\MELONDS_NSML_WRITE_TRACE_START_FRAME -ErrorAction SilentlyContinue }
        if ($WriteTraceEndFrame -gt 0) { $env:MELONDS_NSML_WRITE_TRACE_END_FRAME = "$WriteTraceEndFrame" } else { Remove-Item Env:\MELONDS_NSML_WRITE_TRACE_END_FRAME -ErrorAction SilentlyContinue }
    } else {
        Remove-Item Env:\MELONDS_NSML_WRITE_TRACE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_WRITE_TRACE_LOG -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_WRITE_TRACE_ADDRS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_WRITE_TRACE_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_WRITE_TRACE_END_FRAME -ErrorAction SilentlyContinue
    }
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
    if ($StateSync) {
        $env:MELONDS_NSML_STATE_SYNC = "1"
        $env:MELONDS_NSML_STATE_SYNC_INTERVAL = "$StateSyncInterval"
        if ($StateApply) {
            $env:MELONDS_NSML_STATE_APPLY = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_STATE_APPLY -ErrorAction SilentlyContinue
        }
        if ($StateSyncExtended) {
            $env:MELONDS_NSML_STATE_SYNC_EXTENDED = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_STATE_SYNC_EXTENDED -ErrorAction SilentlyContinue
        }
        if ($StateApplyMode) {
            $env:MELONDS_NSML_STATE_APPLY_MODE = $StateApplyMode
        } else {
            Remove-Item Env:\MELONDS_NSML_STATE_APPLY_MODE -ErrorAction SilentlyContinue
        }
    } else {
        Remove-Item Env:\MELONDS_NSML_STATE_SYNC -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_APPLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_SYNC_INTERVAL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_SYNC_EXTENDED -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_APPLY_MODE -ErrorAction SilentlyContinue
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
    if ($StateSaveDir -and $StateSaveFrame -gt 0) {
        $roleStateSaveDir = Join-Path $StateSaveDir $Role
        New-Item -ItemType Directory -Force -Path $roleStateSaveDir | Out-Null
        $env:MELONDS_NSML_STATE_SAVE_DIR = (Resolve-Path $roleStateSaveDir).Path
        $env:MELONDS_NSML_STATE_SAVE_FRAME = "$StateSaveFrame"
    } else {
        Remove-Item Env:\MELONDS_NSML_STATE_SAVE_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_SAVE_FRAME -ErrorAction SilentlyContinue
    }
    if ($StateLoadDir) {
        $roleStateLoadDir = Join-Path $StateLoadDir $Role
        if (Test-Path $roleStateLoadDir) {
            $env:MELONDS_NSML_STATE_LOAD_DIR = (Resolve-Path $roleStateLoadDir).Path
        } else {
            $env:MELONDS_NSML_STATE_LOAD_DIR = (Resolve-Path $StateLoadDir).Path
        }
        if ($StateLoadFrame -lt 0) {
            $env:MELONDS_NSML_STATE_LOAD_FRAME = "1"
        } else {
            $env:MELONDS_NSML_STATE_LOAD_FRAME = "$StateLoadFrame"
        }
    } else {
        Remove-Item Env:\MELONDS_NSML_STATE_LOAD_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_LOAD_FRAME -ErrorAction SilentlyContinue
    }
    if ($LanMPTrace) {
        $env:MELONDS_NSML_LANMP_TRACE = $LanMPTracePath
        $env:MELONDS_NSML_LANMP_TRACE_DUMP_LEN = "$LanMPTraceDumpLen"
    } else {
        Remove-Item Env:\MELONDS_NSML_LANMP_TRACE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LANMP_TRACE_DUMP_LEN -ErrorAction SilentlyContinue
    }
    if ($NetRandomValue) {
        $env:MELONDS_NSML_NET_RANDOM_VALUE = $NetRandomValue
        $env:MELONDS_NSML_NET_RANDOM_FRAME = "$NetRandomFrame"
        if ($NetRandomAuto) {
            $env:MELONDS_NSML_NET_RANDOM_AUTO = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_NET_RANDOM_AUTO -ErrorAction SilentlyContinue
        }
    } else {
        Remove-Item Env:\MELONDS_NSML_NET_RANDOM_VALUE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_NET_RANDOM_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_NET_RANDOM_AUTO -ErrorAction SilentlyContinue
    }
    if ($PacketReplayFile) {
        $env:MELONDS_NSML_PACKET_REPLAY_FILE = (Resolve-Path $PacketReplayFile).Path
        $env:MELONDS_NSML_PACKET_REPLAY_LOG = "$Stdout.packet-replay.csv"
    } else {
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_FILE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_LOG -ErrorAction SilentlyContinue
    }
    if ($PacketCapture) {
        $env:MELONDS_NSML_PACKET_CAPTURE_LOG = $PacketCapturePath
        if ($PacketCaptureAllowPreGame) {
            $env:MELONDS_NSML_PACKET_BRIDGE_ALLOW_PRE_GAME = "1"
        }
    } else {
        Remove-Item Env:\MELONDS_NSML_PACKET_CAPTURE_LOG -ErrorAction SilentlyContinue
        if (-not $PacketBridge) {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_ALLOW_PRE_GAME -ErrorAction SilentlyContinue
        }
    }
    if ($PacketBridge) {
        $env:MELONDS_NSML_POC = "1"
        $env:MELONDS_NSML_ROLE = $Role
        $env:MELONDS_NSML_PORT = "$PacketBridgePort"
        if ($PacketBridgeAllowPreGame -and $Role -eq "client") {
            $env:MELONDS_NSML_LOCAL_INSTANCE = "1"
        } else {
            $env:MELONDS_NSML_LOCAL_INSTANCE = "0"
        }
        $env:MELONDS_NSML_PACKET_BRIDGE = "1"
        $env:MELONDS_NSML_PACKET_BRIDGE_ONLY = "1"
        if ($PacketBridgeAllowPreGame) {
            $env:MELONDS_NSML_PACKET_BRIDGE_ALLOW_PRE_GAME = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_ALLOW_PRE_GAME -ErrorAction SilentlyContinue
        }
        $roleReplayOffset = $PacketBridgeReplayTickOffset
        if ($Role -eq "host" -and $HostPacketBridgeReplayTickOffset -ge 0) {
            $roleReplayOffset = $HostPacketBridgeReplayTickOffset
        } elseif ($Role -eq "client" -and $ClientPacketBridgeReplayTickOffset -ge 0) {
            $roleReplayOffset = $ClientPacketBridgeReplayTickOffset
        }
        $env:MELONDS_NSML_PACKET_BRIDGE_REPLAY_TICK_OFFSET = "$roleReplayOffset"
        if ($PacketBridgeWait) {
            $env:MELONDS_NSML_PACKET_BRIDGE_WAIT = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_WAIT_TIMEOUT_MS = "$PacketBridgeWaitTimeoutMs"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_WAIT -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_WAIT_TIMEOUT_MS -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeDirectCapture) {
            $env:MELONDS_NSML_PACKET_BRIDGE_DIRECT_CAPTURE = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_DIRECT_CAPTURE -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeForceTick) {
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK_START_FRAME = "$PacketBridgeForceTickStartFrame"
            if ($PacketBridgeForceTickBase -ge 0) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK_BASE = "$PacketBridgeForceTickBase"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK_BASE -ErrorAction SilentlyContinue
            }
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK_START_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK_BASE -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeForceNetReady) {
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_START_FRAME = "$PacketBridgeForceNetReadyStartFrame"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_START_FRAME -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeForceLoadGameSM) {
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_START_FRAME = "$PacketBridgeForceLoadGameSMStartFrame"
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_STEP = "$PacketBridgeForceLoadGameSMStep"
            if ($PacketBridgeForceLoadGameSMTimer -ge 0) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_TIMER = "$PacketBridgeForceLoadGameSMTimer"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_TIMER -ErrorAction SilentlyContinue
            }
            if ($PacketBridgeForceLoadGameSMRunUpdate) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_RUN_UPDATE = "1"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_RUN_UPDATE -ErrorAction SilentlyContinue
            }
            if ($PacketBridgeForceLoadGameSMRunUpdateAll) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_RUN_UPDATE_ALL = "1"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_RUN_UPDATE_ALL -ErrorAction SilentlyContinue
            }
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_START_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_STEP -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_TIMER -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_RUN_UPDATE -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_RUN_UPDATE_ALL -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeLookupTickDelay -gt 0) {
            $env:MELONDS_NSML_PACKET_REPLAY_LOOKUP_TICK_DELAY = "$PacketBridgeLookupTickDelay"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_LOOKUP_TICK_DELAY -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeMaxPumpEvents -gt 0) {
            $env:MELONDS_NSML_PACKET_BRIDGE_MAX_PUMP_EVENTS = "$PacketBridgeMaxPumpEvents"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_MAX_PUMP_EVENTS -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeSuppressDisconnect) {
            $env:MELONDS_NSML_PACKET_BRIDGE_SUPPRESS_DISCONNECT = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_SUPPRESS_DISCONNECT -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeSuppressBlackout) {
            $env:MELONDS_NSML_PACKET_BRIDGE_SUPPRESS_BLACKOUT = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_SUPPRESS_BLACKOUT -ErrorAction SilentlyContinue
        }
        if ($PacketBridgePreserveNetPointers) {
            $env:MELONDS_NSML_PACKET_BRIDGE_PRESERVE_NET_POINTERS = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_PRESERVE_NET_POINTERS -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeBypassNetReset) {
            $env:MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_RESET = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_RESET -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeBypassNetDisconnect) {
            $env:MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT_MODE = $PacketBridgeBypassNetDisconnectMode
            if ($PacketBridgeBypassNetDisconnectStartFrame -gt 0) {
                $env:MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT_START_FRAME = "$PacketBridgeBypassNetDisconnectStartFrame"
            } elseif ($DropMPAfterFrame -gt 0) {
                $env:MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT_START_FRAME = "$DropMPAfterFrame"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT_START_FRAME -ErrorAction SilentlyContinue
            }
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT_MODE -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT_START_FRAME -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeForceTransferResult) {
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_RESULT = "1"
            if ($PacketBridgeForceTransferStartFrame -gt 0) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_START_FRAME = "$PacketBridgeForceTransferStartFrame"
            } elseif ($DropMPAfterFrame -gt 0) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_START_FRAME = "$DropMPAfterFrame"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_START_FRAME -ErrorAction SilentlyContinue
            }
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_RESULT_VALUE = "$PacketBridgeForceTransferResultValue"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_RESULT -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_START_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_RESULT_VALUE -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeMaxTickLead -ge 0) {
            $env:MELONDS_NSML_PACKET_BRIDGE_MAX_TICK_LEAD = "$PacketBridgeMaxTickLead"
            $env:MELONDS_NSML_PACKET_BRIDGE_THROTTLE_TIMEOUT_MS = "$PacketBridgeThrottleTimeoutMs"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_MAX_TICK_LEAD -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeMaxFrameLead -ge 0) {
            $env:MELONDS_NSML_PACKET_BRIDGE_MAX_FRAME_LEAD = "$PacketBridgeMaxFrameLead"
            $env:MELONDS_NSML_PACKET_BRIDGE_THROTTLE_TIMEOUT_MS = "$PacketBridgeThrottleTimeoutMs"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_MAX_FRAME_LEAD -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeMaxTickLead -lt 0 -and $PacketBridgeMaxFrameLead -lt 0) {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_THROTTLE_TIMEOUT_MS -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeStrictRemote -or $PacketBridgeStrictPlayers) {
            $env:MELONDS_NSML_PACKET_REPLAY_STRICT = "1"
            if ($PacketBridgeStrictStartFrame -gt 0) {
                $env:MELONDS_NSML_PACKET_REPLAY_STRICT_START_FRAME = "$PacketBridgeStrictStartFrame"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_STRICT_START_FRAME -ErrorAction SilentlyContinue
            }
            if ($PacketBridgeStrictRequireLead -gt 0) {
                $env:MELONDS_NSML_PACKET_REPLAY_STRICT_REQUIRE_LEAD = "$PacketBridgeStrictRequireLead"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_STRICT_REQUIRE_LEAD -ErrorAction SilentlyContinue
            }
            if ($PacketBridgeStrictPlayers) {
                $env:MELONDS_NSML_PACKET_REPLAY_STRICT_PLAYERS = $PacketBridgeStrictPlayers
            } elseif ($Role -eq "host") {
                $env:MELONDS_NSML_PACKET_REPLAY_STRICT_PLAYERS = "1"
            } else {
                $env:MELONDS_NSML_PACKET_REPLAY_STRICT_PLAYERS = "0"
            }
        } elseif (-not $PacketReplayFile) {
            Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_STRICT -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_STRICT_PLAYERS -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_STRICT_START_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_STRICT_REQUIRE_LEAD -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeLiveFallbackWindow -gt 0) {
            $env:MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_WINDOW = "$PacketBridgeLiveFallbackWindow"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_WINDOW -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeReplayReturnLookupTick) {
            $env:MELONDS_NSML_PACKET_REPLAY_RETURN_LOOKUP_TICK = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_RETURN_LOOKUP_TICK -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeReplayOps) {
            $env:MELONDS_NSML_PACKET_REPLAY_OPS = $PacketBridgeReplayOps
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_OPS -ErrorAction SilentlyContinue
        }
        Remove-Item Env:\MELONDS_NSML_WAIT_FOR_PEER -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SEED_WAIT_TIMEOUT_MS -ErrorAction SilentlyContinue
        if ($PacketBridgeStartFrame -gt 0) {
            $env:MELONDS_NSML_DEFER_NETWORK_UNTIL_START = "1"
            $env:MELONDS_NSML_NETPLAY_START_FRAME = "$PacketBridgeStartFrame"
        } else {
            Remove-Item Env:\MELONDS_NSML_DEFER_NETWORK_UNTIL_START -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_NETPLAY_START_FRAME -ErrorAction SilentlyContinue
        }
        if ($Role -eq "client") {
            $env:MELONDS_NSML_PEER = "127.0.0.1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PEER -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeTrace) {
            $env:MELONDS_NSML_PACKET_BRIDGE_TRACE = "1"
            $env:MELONDS_NSML_PACKET_REPLAY_LOG = "$Stdout.packet-replay.csv"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_TRACE -ErrorAction SilentlyContinue
            if (-not $PacketReplayFile) {
                Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_LOG -ErrorAction SilentlyContinue
            }
        }
    } else {
        Remove-Item Env:\MELONDS_NSML_POC -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_ROLE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PEER -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PORT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LOCAL_INSTANCE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_ONLY -ErrorAction SilentlyContinue
        if (-not ($PacketCapture -and $PacketCaptureAllowPreGame)) {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_ALLOW_PRE_GAME -ErrorAction SilentlyContinue
        }
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_REPLAY_TICK_OFFSET -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_WAIT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_WAIT_TIMEOUT_MS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_DIRECT_CAPTURE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK_BASE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_STEP -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_TIMER -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_RUN_UPDATE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_RUN_UPDATE_ALL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_LOOKUP_TICK_DELAY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_MAX_PUMP_EVENTS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_SUPPRESS_DISCONNECT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_SUPPRESS_BLACKOUT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_PRESERVE_NET_POINTERS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_RESET -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT_MODE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_RESULT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_RESULT_VALUE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_MAX_TICK_LEAD -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_THROTTLE_TIMEOUT_MS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_NET_RANDOM_VALUE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_NET_RANDOM_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_NET_RANDOM_AUTO -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_STRICT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_STRICT_PLAYERS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_STRICT_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_STRICT_REQUIRE_LEAD -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_WINDOW -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_RETURN_LOOKUP_TICK -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_OPS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_TRACE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_WAIT_FOR_PEER -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SEED_WAIT_TIMEOUT_MS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_DEFER_NETWORK_UNTIL_START -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_NETPLAY_START_FRAME -ErrorAction SilentlyContinue
    }
    if (-not $PacketBridge) {
        if ($PacketBridgeForceNetReady) {
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_START_FRAME = "$PacketBridgeForceNetReadyStartFrame"
        }
        if ($PacketBridgeForceLoadGameSM) {
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_START_FRAME = "$PacketBridgeForceLoadGameSMStartFrame"
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_STEP = "$PacketBridgeForceLoadGameSMStep"
            if ($PacketBridgeForceLoadGameSMTimer -ge 0) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_TIMER = "$PacketBridgeForceLoadGameSMTimer"
            }
            if ($PacketBridgeForceLoadGameSMRunUpdate) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_RUN_UPDATE = "1"
            }
            if ($PacketBridgeForceLoadGameSMRunUpdateAll) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_RUN_UPDATE_ALL = "1"
            }
        }
        if ($PacketBridgeTrace) {
            $env:MELONDS_NSML_PACKET_BRIDGE_TRACE = "1"
        }
    }
    if (-not $StateSync) {
        Remove-Item Env:\MELONDS_NSML_STATE_SYNC -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_APPLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_SYNC_INTERVAL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_SYNC_EXTENDED -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_APPLY_MODE -ErrorAction SilentlyContinue
    }
    if (-not $StateSaveDir) {
        Remove-Item Env:\MELONDS_NSML_STATE_SAVE_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_SAVE_FRAME -ErrorAction SilentlyContinue
    }
    if (-not $StateLoadDir) {
        Remove-Item Env:\MELONDS_NSML_STATE_LOAD_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_LOAD_FRAME -ErrorAction SilentlyContinue
    }
    $env:MELONDS_NSML_FIXED_RTC = "2020-01-01T00:00:00"
    $env:MELONDS_NSML_DISABLE_JIT = "1"
    if ($DropMPAfterFrame -gt 0) {
        $env:MELONDS_NSML_DROP_MP_AFTER_FRAME = "$DropMPAfterFrame"
    } else {
        Remove-Item Env:\MELONDS_NSML_DROP_MP_AFTER_FRAME -ErrorAction SilentlyContinue
    }
    if ($NoLanMP) {
        Remove-Item Env:\MELONDS_NSML_MP_INTERFACE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LAN_ROLE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LAN_PLAYERS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LAN_HOST -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LAN_PLAYER -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LAN_WAN_MODE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LAN_MP_RECV_TIMEOUT_MS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LAN_MP_MISC_RECV_TIMEOUT_MS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LAN_MP_STALE_MS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LAN_MP_SEND_DELAY_MS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LAN_MP_RELIABLE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_WIFI_MP_ACCEPT_ANY_CHANNEL -ErrorAction SilentlyContinue
    } else {
        $env:MELONDS_NSML_MP_INTERFACE = "lan"
        $env:MELONDS_NSML_LAN_ROLE = $Role
        $env:MELONDS_NSML_LAN_PLAYERS = "2"
        $env:MELONDS_NSML_LAN_HOST = "127.0.0.1"
        $env:MELONDS_NSML_LAN_PLAYER = "codex-$Role"
        if ($LanWanMode) {
            $env:MELONDS_NSML_LAN_WAN_MODE = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_LAN_WAN_MODE -ErrorAction SilentlyContinue
        }
        if ($LanMPRecvTimeoutMs -ge 0) {
            $env:MELONDS_NSML_LAN_MP_RECV_TIMEOUT_MS = "$LanMPRecvTimeoutMs"
        } else {
            Remove-Item Env:\MELONDS_NSML_LAN_MP_RECV_TIMEOUT_MS -ErrorAction SilentlyContinue
        }
        if ($LanMPMiscRecvTimeoutMs -ge 0) {
            $env:MELONDS_NSML_LAN_MP_MISC_RECV_TIMEOUT_MS = "$LanMPMiscRecvTimeoutMs"
        } else {
            Remove-Item Env:\MELONDS_NSML_LAN_MP_MISC_RECV_TIMEOUT_MS -ErrorAction SilentlyContinue
        }
        if ($LanMPStaleMs -ge 0) {
            $env:MELONDS_NSML_LAN_MP_STALE_MS = "$LanMPStaleMs"
        } else {
            Remove-Item Env:\MELONDS_NSML_LAN_MP_STALE_MS -ErrorAction SilentlyContinue
        }
        $roleSendDelay = $LanMPSendDelayMs
        if ($Role -eq "host" -and $HostLanMPSendDelayMs -ge 0) {
            $roleSendDelay = $HostLanMPSendDelayMs
        } elseif ($Role -eq "client" -and $ClientLanMPSendDelayMs -ge 0) {
            $roleSendDelay = $ClientLanMPSendDelayMs
        }
        if ($roleSendDelay -ge 0) {
            $env:MELONDS_NSML_LAN_MP_SEND_DELAY_MS = "$roleSendDelay"
        } else {
            Remove-Item Env:\MELONDS_NSML_LAN_MP_SEND_DELAY_MS -ErrorAction SilentlyContinue
        }
        if ($LanMPReliable) {
            $env:MELONDS_NSML_LAN_MP_RELIABLE = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_LAN_MP_RELIABLE -ErrorAction SilentlyContinue
        }
        if ($LanMPAcceptAnyChannel) {
            $env:MELONDS_NSML_WIFI_MP_ACCEPT_ANY_CHANNEL = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_WIFI_MP_ACCEPT_ANY_CHANNEL -ErrorAction SilentlyContinue
        }
    }
    if ($Role -eq "host") {
        $env:MELONDS_NSML_FIRMWARE_MAC = "00:09:BF:11:22:33"
    } else {
        $env:MELONDS_NSML_FIRMWARE_MAC = "00:09:BF:11:22:43"
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
    $hostProc = Start-MelonLANProcess -Role "host" -RoleRom $hostRom -RoleInput $hostInput -Stdout $hostOut -HashLog $hostHash -ScreenshotDir $hostScreens -GameStateTracePath $hostGameStateTrace -LanMPTracePath $hostLanMPTrace -PacketReplayFile $HostPacketReplayFile -PacketCapturePath $hostPacketCapture -RamDumpDir $hostRamDumps
    Start-Sleep -Milliseconds $HostStartupDelayMs
    $clientProc = Start-MelonLANProcess -Role "client" -RoleRom $clientRom -RoleInput $clientInput -Stdout $clientOut -HashLog $clientHash -ScreenshotDir $clientScreens -GameStateTracePath $clientGameStateTrace -LanMPTracePath $clientLanMPTrace -PacketReplayFile $ClientPacketReplayFile -PacketCapturePath $clientPacketCapture -RamDumpDir $clientRamDumps

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

$requiredPatterns = @(
    @{ Path = $hostOut; Pattern = "frame limit reached"; Name = "host frame limit" },
    @{ Path = $clientOut; Pattern = "frame limit reached"; Name = "client frame limit" }
)
if (-not $NoLanMP) {
    $requiredPatterns = @(
        @{ Path = $hostOut; Pattern = "LAN host start .* ok=1"; Name = "host LAN start" },
        @{ Path = $clientOut; Pattern = "LAN client start .* ok=1"; Name = "client LAN start" }
    ) + $requiredPatterns
}

foreach ($item in $requiredPatterns) {
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

function Test-DisconnectLikeScreenshot {
    param([string]$Path)

    Add-Type -AssemblyName System.Drawing
    $bitmap = [System.Drawing.Bitmap]::FromFile($Path)
    try {
        $sampleCount = 0
        $blackCount = 0
        $brightCount = 0
        for ($y = 0; $y -lt $bitmap.Height; $y += 4) {
            for ($x = 0; $x -lt $bitmap.Width; $x += 4) {
                $pixel = $bitmap.GetPixel($x, $y)
                $sampleCount++
                if ($pixel.R -lt 18 -and $pixel.G -lt 18 -and $pixel.B -lt 18) {
                    $blackCount++
                } elseif ($pixel.R -gt 150 -and $pixel.G -gt 150 -and $pixel.B -gt 150) {
                    $brightCount++
                }
            }
        }

        if ($sampleCount -eq 0) {
            return $false
        }

        $blackRatio = $blackCount / $sampleCount
        $brightRatio = $brightCount / $sampleCount
        return ($blackRatio -gt 0.82 -and $brightRatio -gt 0.004 -and $brightRatio -lt 0.20)
    } finally {
        $bitmap.Dispose()
    }
}

function Test-BlankLikeScreenshot {
    param([string]$Path)

    Add-Type -AssemblyName System.Drawing
    $bitmap = [System.Drawing.Bitmap]::FromFile($Path)
    try {
        $sampleCount = 0
        $blackCount = 0
        $brightCount = 0
        for ($y = 0; $y -lt $bitmap.Height; $y += 4) {
            for ($x = 0; $x -lt $bitmap.Width; $x += 4) {
                $pixel = $bitmap.GetPixel($x, $y)
                $sampleCount++
                if ($pixel.R -lt 18 -and $pixel.G -lt 18 -and $pixel.B -lt 18) {
                    $blackCount++
                } elseif ($pixel.R -gt 150 -and $pixel.G -gt 150 -and $pixel.B -gt 150) {
                    $brightCount++
                }
            }
        }

        if ($sampleCount -eq 0) {
            return $false
        }

        $blackRatio = $blackCount / $sampleCount
        $brightRatio = $brightCount / $sampleCount
        return ($blackRatio -gt 0.995 -and $brightRatio -lt 0.001)
    } finally {
        $bitmap.Dispose()
    }
}

function Test-ConnectionDialogScreenshot {
    param([string]$Path)

    Add-Type -AssemblyName System.Drawing
    $bitmap = [System.Drawing.Bitmap]::FromFile($Path)
    try {
        $sampleCount = 0
        $blackCount = 0
        $brightCount = 0
        $x0 = [Math]::Max(0, [int]($bitmap.Width * 0.08))
        $x1 = [Math]::Min($bitmap.Width - 1, [int]($bitmap.Width * 0.92))
        $y0 = [Math]::Max(0, [int]($bitmap.Height * 0.11))
        $y1 = [Math]::Min($bitmap.Height - 1, [int]($bitmap.Height * 0.43))
        for ($y = $y0; $y -le $y1; $y += 4) {
            for ($x = $x0; $x -le $x1; $x += 4) {
                $pixel = $bitmap.GetPixel($x, $y)
                $sampleCount++
                if ($pixel.R -lt 22 -and $pixel.G -lt 22 -and $pixel.B -lt 22) {
                    $blackCount++
                } elseif ($pixel.R -gt 170 -and $pixel.G -gt 170 -and $pixel.B -gt 170) {
                    $brightCount++
                }
            }
        }

        if ($sampleCount -eq 0) {
            return $false
        }

        $blackRatio = $blackCount / $sampleCount
        $brightRatio = $brightCount / $sampleCount
        return ($blackRatio -gt 0.55 -and $brightRatio -gt 0.01)
    } finally {
        $bitmap.Dispose()
    }
}

if (-not $SkipDisconnectScreenshotCheck) {
    foreach ($screenDir in @($hostScreens, $clientScreens)) {
        $screens = Get-ChildItem $screenDir -Filter "inst0_frame*.png" -ErrorAction SilentlyContinue
        foreach ($screen in $screens) {
            if ($screen.Name -notmatch "frame(\d+)\.png") {
                continue
            }

            $frame = [int]$matches[1]
            if ($frame -lt 3000) {
                continue
            }

            if (Test-DisconnectLikeScreenshot -Path $screen.FullName) {
                throw "disconnect-like screenshot detected at frame=${frame}: $($screen.FullName)"
            }
            if (Test-BlankLikeScreenshot -Path $screen.FullName) {
                throw "blank-like screenshot detected at frame=${frame}: $($screen.FullName)"
            }
            if (Test-ConnectionDialogScreenshot -Path $screen.FullName) {
                throw "connection-dialog screenshot detected at frame=${frame}: $($screen.FullName)"
            }
        }
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

        if (-not $SkipGameplayActorCheck) {
            if ($last.playerActor0Found -ne "0x1" -or $last.playerActor1Found -ne "0x1" -or $last.vsStarActorFound -ne "0x1") {
                throw "Mario vs Luigi gameplay actor check failed for $($item.Role): playerActor0=$($last.playerActor0Found) playerActor1=$($last.playerActor1Found) vsStarActor=$($last.vsStarActorFound). See $($item.Path)"
            }
        }
    }
}

Write-Host "NSMB Mario vs Luigi LAN route smoke passed: frames=$Frames"
