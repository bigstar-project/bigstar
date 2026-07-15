param(
    [ValidateSet("fast", "rollback", "diagnostics", "standard", "full")]
    [string]$Tier = "fast",
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$Rom = "roms\nsmb-us.nds",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-netaid.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-netaid.tmp.nds",
    [string]$LogDir = "",
    [switch]$SkipGolden
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$routeSmoke = Join-Path $PSScriptRoot "run-nsmb-mvl-lan-route-smoke.ps1"

if (-not $LogDir) {
    $LogDir = "logs\nsmb-netplay-refactor-$Tier"
}

if ($Tier -eq "full") {
    & $PSCommandPath `
        -Tier standard `
        -Exe $Exe `
        -Rom $Rom `
        -HostRom $HostRom `
        -ClientRom $ClientRom `
        -SkipGolden:$SkipGolden `
        -LogDir "$LogDir-standard"

    & $PSCommandPath `
        -Tier rollback `
        -Exe $Exe `
        -Rom $Rom `
        -HostRom $HostRom `
        -ClientRom $ClientRom `
        -SkipGolden:$SkipGolden `
        -LogDir "$LogDir-rollback"

    & (Join-Path $PSScriptRoot "run-nsmb-mvl-split-local-result-smoke.ps1") `
        -Exe $Exe `
        -GenerateMvlSourceRom $Rom `
        -HostRom $HostRom `
        -ClientRom $ClientRom `
        -Frames 6000 `
        -LowDelayWan `
        -SkipRomEnsure `
        -LogDir "$LogDir-result"

    Write-Host "NsmbNetplayPoC refactor full test passed: log=$LogDir"
    return
}

function Assert-Sha256 {
    param(
        [string]$Path,
        [string]$Expected,
        [string]$Name
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "missing golden input/output for ${Name}: $Path"
    }
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($actual -ne $Expected) {
        throw "golden SHA256 mismatch for ${Name}: expected=$Expected actual=$actual path=$Path"
    }
}

function Resolve-RepoPath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path $repoRoot $Path
}

function Set-MelonTomlValue {
    param(
        [string]$Text,
        [string]$KeyPath,
        [string]$Value
    )

    $separator = $KeyPath.LastIndexOf('.')
    if ($separator -lt 0) {
        if ($Text -match "(?m)^$([regex]::Escape($KeyPath))\s*=") {
            return ($Text -replace "(?m)^$([regex]::Escape($KeyPath))\s*=.*$", "$KeyPath = $Value")
        }
        return "$Text`n$KeyPath = $Value"
    }

    $section = $KeyPath.Substring(0, $separator)
    $key = $KeyPath.Substring($separator + 1)
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

$params = @{
    RunRole = "both"
    Exe = $Exe
    Rom = $Rom
    HostRom = $HostRom
    ClientRom = $ClientRom
    ScreenshotInterval = 0
    NoHashLog = $true
    NoLanMP = $true
    InputNetplay = $true
    InputDelayFrames = 4
    InputMaxFrameLead = 4
    PacketBridgeJitHelperPatch = $true
    PacketBridgeJitHelperPatchFrame = 840
    PacketBridgeStartFrame = 840
    ClearMvlCameraInitHold = $true
    ClearMvlCameraInitHoldStartFrame = 840
    WaitForPeerAtNetplayStart = $true
    AllowJit = $true
    InputUnreliable = $true
    InputBundleHistory = 8
    MvlMatchSeed = "0x00000100"
    GameStateTrace = $true
    CheckHostClientGameplaySync = $true
    RequireClientRemotePlayer0Movement = $true
    LogDir = $LogDir
}

if ($Tier -eq "fast" -or $Tier -eq "rollback" -or $Tier -eq "diagnostics") {
    $params.InputScript = "tests\nsmb_us_direct_mvl_refactor_fast.inputs"
    $params.Frames = 1250
    $params.GameStateTraceInterval = 10
    $params.InputDropModulo = 11
    $params.InputDropOffset = 3
    $params.NoDrawScreen = $true
    $params.NoAudioSync = $true
    $params.NoFrameLimit = $true
    $params.FixedFrameTime = $true
    if ($Tier -eq "rollback") {
        $params.InputMaxFrameLead = 32
        $params.InputNetplayTrace = $true
        $params.Rollback = $true
        $params.RollbackBackend = "tinycorepreimage"
        $params.RollbackResimulate = $true
    } elseif ($Tier -eq "diagnostics") {
        $params.HangDiagnostics = $true
        $params.DiagnosticEvents = $true
        $params.HangWatchdogIntervalMs = 100
        $params.StallTimeoutMs = 10000
        $params.FrameHeartbeatInterval = 10
        $params.GameplayHeartbeatInterval = 100
    }
} else {
    $params.InputScript = "tests\nsmb_us_direct_mvl_both_different.inputs"
    $params.Frames = 3000
    $params.GameStateTraceInterval = 20
    $params.ScreenshotInterval = 300
}

$rendererConfigPath = Join-Path (Split-Path -Parent (Resolve-RepoPath $Exe)) "melonDS.toml"
$originalRendererConfig = $null
$originalRendererConfigBytes = $null
if ($Tier -eq "standard") {
    if (-not (Test-Path -LiteralPath $rendererConfigPath -PathType Leaf)) {
        throw "standard screenshot tier requires melonDS config: $rendererConfigPath"
    }
    $originalRendererConfigBytes = [System.IO.File]::ReadAllBytes($rendererConfigPath)
    $originalRendererConfig = Get-Content -LiteralPath $rendererConfigPath -Raw -Encoding UTF8
    $softwareRendererConfig = $originalRendererConfig
    $softwareRendererConfig = Set-MelonTomlValue -Text $softwareRendererConfig -KeyPath "Screen.UseGL" -Value "false"
    $softwareRendererConfig = Set-MelonTomlValue -Text $softwareRendererConfig -KeyPath "3D.Renderer" -Value "0"
    $softwareRendererConfig = Set-MelonTomlValue -Text $softwareRendererConfig -KeyPath "3D.Soft.Threaded" -Value "true"
    Set-Content -LiteralPath $rendererConfigPath -Value $softwareRendererConfig -Encoding UTF8
}

Push-Location $repoRoot
try {
    & $routeSmoke @params
} finally {
    if ($null -ne $originalRendererConfigBytes) {
        [System.IO.File]::WriteAllBytes($rendererConfigPath, $originalRendererConfigBytes)
    }
    Pop-Location
}

if ($Tier -eq "rollback") {
    $resolvedLogDir = if ([System.IO.Path]::IsPathRooted($LogDir)) {
        $LogDir
    } else {
        Join-Path $repoRoot $LogDir
    }
    $rollbackLogs = @(
        (Join-Path $resolvedLogDir "host.stdout.txt"),
        (Join-Path $resolvedLogDir "client.stdout.txt")
    )
    $rollbackText = ($rollbackLogs | ForEach-Object {
        Get-Content -LiteralPath $_ -Raw -Encoding UTF8
    }) -join "`n"
    if ($rollbackText -notmatch 'checkpointSaves=[1-9][0-9]*') {
        throw "rollback smoke did not save any gameplay checkpoints"
    }
    if ($rollbackText -match 'cannot resimulate|checkpoint missing|restore failed') {
        throw "rollback smoke reported a checkpoint/restore integrity error"
    }
    Write-Host "NsmbNetplayPoC rollback checkpoint coverage passed"
}

if ($Tier -eq "diagnostics") {
    $resolvedLogDir = if ([System.IO.Path]::IsPathRooted($LogDir)) {
        $LogDir
    } else {
        Join-Path $repoRoot $LogDir
    }
    foreach ($role in @("host", "client")) {
        $watchdogPath = Join-Path $resolvedLogDir "$role.stdout.txt.watchdog.jsonl"
        $phasePath = Join-Path $resolvedLogDir "$role.stdout.txt.phase-events.jsonl"
        $heartbeatPath = Join-Path $resolvedLogDir "$role.stdout.txt.heartbeat"
        $eventsPath = Join-Path $resolvedLogDir "$role.stdout.txt.events.jsonl"
        $stdoutPath = Join-Path $resolvedLogDir "$role.stdout.txt"
        foreach ($path in @($watchdogPath, $phasePath, $heartbeatPath, $eventsPath)) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                throw "diagnostics smoke did not create log: $path"
            }
        }

        $watchdogLines = @(Get-Content -LiteralPath $watchdogPath -Encoding UTF8)
        $phaseLines = @(Get-Content -LiteralPath $phasePath -Encoding UTF8)
        if ($watchdogLines.Count -lt 1 -or $phaseLines.Count -lt 100) {
            throw "diagnostics smoke has insufficient coverage: role=$role watchdog=$($watchdogLines.Count) phase=$($phaseLines.Count)"
        }
        $eventLines = @(Get-Content -LiteralPath $eventsPath -Encoding UTF8)
        if ($eventLines.Count -lt 1) {
            throw "diagnostic event smoke has insufficient coverage: role=$role"
        }
        $events = @($eventLines | ForEach-Object { $_ | ConvertFrom-Json })
        if ($events[0].event -ne "diagnostic_started" -or $events[0].role -ne $role) {
            throw "diagnostic event schema mismatch: role=$role first=$($eventLines[0])"
        }

        $watchdogFirst = $watchdogLines[0] | ConvertFrom-Json
        $watchdogLast = $watchdogLines[-1] | ConvertFrom-Json
        $phaseFirst = $phaseLines[0] | ConvertFrom-Json
        $phaseLast = $phaseLines[-1] | ConvertFrom-Json
        if ($watchdogFirst.event -ne "watchdog" -or $watchdogFirst.role -ne $role -or
            $null -eq $watchdogLast.arm9PC -or $null -eq $watchdogLast.remoteWaitActive) {
            throw "diagnostics watchdog schema mismatch: role=$role"
        }
        if (-not $phaseFirst.event -or -not $phaseFirst.phase -or
            $null -eq $phaseLast.lastSent -or $null -eq $phaseLast.remoteWaitActive) {
            throw "diagnostics phase schema mismatch: role=$role"
        }

        $heartbeatFrames = @(
            Get-Content -LiteralPath $heartbeatPath -Encoding UTF8 |
                Where-Object { $_ -match '^\d+$' } |
                ForEach-Object { [int]$_ }
        )
        if ($heartbeatFrames.Count -lt 5 -or $heartbeatFrames[-1] -lt 1200) {
            throw "diagnostics heartbeat has insufficient coverage: role=$role count=$($heartbeatFrames.Count) last=$($heartbeatFrames[-1])"
        }
        for ($index = 0; $index -lt $heartbeatFrames.Count; $index++) {
            if (($heartbeatFrames[$index] % 10) -ne 0 -or
                ($index -gt 0 -and $heartbeatFrames[$index] -le $heartbeatFrames[$index - 1])) {
                throw "diagnostics heartbeat sequence mismatch: role=$role index=$index frame=$($heartbeatFrames[$index])"
            }
        }

        $stdout = Get-Content -LiteralPath $stdoutPath -Raw -Encoding UTF8
        $gameplayHeartbeatMatches = [regex]::Matches(
            $stdout,
            'NSMB GameplayHeartbeat: role=.*? frame=(\d+) ')
        if ($gameplayHeartbeatMatches.Count -lt 3) {
            throw "diagnostics gameplay heartbeat has insufficient coverage: role=$role count=$($gameplayHeartbeatMatches.Count)"
        }
        foreach ($match in $gameplayHeartbeatMatches) {
            $gameplayFrame = [int]$match.Groups[1].Value
            if ($gameplayFrame -lt 900 -or ($gameplayFrame % 100) -ne 0) {
                throw "diagnostics gameplay heartbeat sequence mismatch: role=$role frame=$gameplayFrame"
            }
        }
    }
    Write-Host "NsmbNetplayPoC hang diagnostics and frame heartbeat coverage passed"
}

if (($Tier -eq "fast" -or $Tier -eq "rollback" -or $Tier -eq "diagnostics") -and -not $SkipGolden) {
    $goldenPath = Join-Path $repoRoot "tests\nsmb_netplay_refactor_fast.golden"
    $golden = Get-Content -LiteralPath $goldenPath -Raw -Encoding UTF8 | ConvertFrom-StringData
    $resolvedLogDir = if ([System.IO.Path]::IsPathRooted($LogDir)) {
        $LogDir
    } else {
        Join-Path $repoRoot $LogDir
    }

    Assert-Sha256 -Path (Resolve-RepoPath $Rom) -Expected $golden.baseRom -Name "base ROM"
    Assert-Sha256 -Path (Resolve-RepoPath $HostRom) -Expected $golden.hostRom -Name "host ROM"
    Assert-Sha256 -Path (Resolve-RepoPath $ClientRom) -Expected $golden.clientRom -Name "client ROM"
    Assert-Sha256 -Path (Join-Path $resolvedLogDir "host.game-state.csv") -Expected $golden.hostGameState -Name "host semantic trace"
    Assert-Sha256 -Path (Join-Path $resolvedLogDir "client.game-state.csv") -Expected $golden.clientGameState -Name "client semantic trace"
    Assert-Sha256 -Path (Join-Path $resolvedLogDir "host.inputs") -Expected $golden.hostInputs -Name "host applied input"
    Assert-Sha256 -Path (Join-Path $resolvedLogDir "client.inputs") -Expected $golden.clientInputs -Name "client applied input"
    Write-Host "NsmbNetplayPoC refactor fast golden comparison passed"
}

Write-Host "NsmbNetplayPoC refactor $Tier test passed: log=$LogDir"
