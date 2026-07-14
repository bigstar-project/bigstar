param(
    [string]$Session = "",
    [int]$StartAt = 0,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

function Resolve-SessionPath {
    param([string]$Value)

    if (-not [string]::IsNullOrWhiteSpace($Value)) {
        if ([System.IO.Path]::IsPathRooted($Value)) {
            return (Resolve-Path -LiteralPath $Value).Path
        }
        return (Resolve-Path -LiteralPath (Join-Path $repoRoot $Value)).Path
    }

    $sessions = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot "logs") -Recurse -Filter "recording-session.json" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending)
    if ($sessions.Count -eq 0) {
        throw "recording-session.json not found under logs; pass -Session explicitly"
    }
    return $sessions[0].FullName
}

if ($StartAt -lt 0) {
    throw "-StartAt must be >= 0"
}

$sessionPath = Resolve-SessionPath $Session
$sessionData = Get-Content -LiteralPath $sessionPath -Raw -Encoding UTF8 | ConvertFrom-Json
$commands = @($sessionData.postCommands)
if ($commands.Count -eq 0) {
    throw "session has no postCommands: $sessionPath"
}
if ($StartAt -ge $commands.Count) {
    throw "-StartAt is outside postCommands range: StartAt=$StartAt count=$($commands.Count)"
}

$plan = [ordered]@{
    schema = "nsmb_mvl_ai_recording_postcommands_run_v1"
    session = $sessionPath
    commandCount = $commands.Count
    startAt = $StartAt
    commands = $commands
}

if ($DryRun) {
    $plan | ConvertTo-Json -Depth 6
    return
}

Push-Location $repoRoot
try {
    for ($i = $StartAt; $i -lt $commands.Count; $i++) {
        $command = [string]$commands[$i]
        Write-Host "[$($i + 1)/$($commands.Count)] $command"
        $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
        powershell -NoProfile -ExecutionPolicy Bypass -Command $command
        $stopwatch.Stop()
        Write-Host ("[$($i + 1)/$($commands.Count)] completed in {0:n1}s" -f $stopwatch.Elapsed.TotalSeconds)
        if ($LASTEXITCODE -ne 0) {
            throw "postCommand failed at index $i with exit code $LASTEXITCODE"
        }
    }
} finally {
    Pop-Location
}

Write-Host "NSMB MvL recording postCommands completed: session=$sessionPath"
