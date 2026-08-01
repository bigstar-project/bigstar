param(
    [int]$Frames = 900,
    [int]$ActiveFpsStartFrame = 300,
    [string]$Exe = "build\release-windows-x86_64\melonDS.exe",
    [string]$BaseRom = "roms\nsmb-us.nds",
    [string]$PatchedRom = "roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-netaid.tmp.nds",
    [string]$InputScript = "tests\nsmb_smoke.inputs",
    [string]$LogDir = "logs\nsmb-rom-jit-matrix",
    [ValidateSet("All", "BaseNoJit", "PatchedNoJit", "BaseJit", "PatchedJit")]
    [string[]]$Cases = @("All")
)

$ErrorActionPreference = "Stop"

if ($Frames -le 0) {
    throw "Frames must be positive"
}
if ($ActiveFpsStartFrame -lt 0 -or $ActiveFpsStartFrame -ge $Frames) {
    throw "ActiveFpsStartFrame must be between 0 and Frames - 1"
}

$resolvedExe = (Resolve-Path -LiteralPath $Exe).Path
$resolvedBaseRom = (Resolve-Path -LiteralPath $BaseRom).Path
$resolvedPatchedRom = (Resolve-Path -LiteralPath $PatchedRom).Path
$resolvedInputScript = (Resolve-Path -LiteralPath $InputScript).Path
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$resolvedLogDir = (Resolve-Path -LiteralPath $LogDir).Path
$exeSha256 = (Get-FileHash -LiteralPath $resolvedExe -Algorithm SHA256).Hash
$inputSha256 = (Get-FileHash -LiteralPath $resolvedInputScript -Algorithm SHA256).Hash

$caseDefinitions = @(
    @{ Key = "BaseNoJit"; Name = "base-nojit"; Rom = $resolvedBaseRom; Jit = $false },
    @{ Key = "PatchedNoJit"; Name = "patched-nojit"; Rom = $resolvedPatchedRom; Jit = $false },
    @{ Key = "BaseJit"; Name = "base-jit"; Rom = $resolvedBaseRom; Jit = $true },
    @{ Key = "PatchedJit"; Name = "patched-jit"; Rom = $resolvedPatchedRom; Jit = $true }
)
$selectedCases = if ($Cases -contains "All") {
    $caseDefinitions
} else {
    $caseDefinitions | Where-Object { $Cases -contains $_.Key }
}
if (-not $selectedCases) {
    throw "No benchmark cases selected"
}

$rows = [System.Collections.Generic.List[object]]::new()
foreach ($case in $selectedCases) {
    # Avoid silently inheriting netplay/diagnostic switches from the caller.
    Get-ChildItem Env: |
        Where-Object { $_.Name -like "MELONDS_NSML_*" } |
        ForEach-Object { Remove-Item -LiteralPath ("Env:" + $_.Name) }

    $env:MELONDS_NSML_TEST = "1"
    $env:MELONDS_NSML_TEST_INSTANCES = "1"
    $env:MELONDS_NSML_TEST_FRAMES = $Frames.ToString()
    $env:MELONDS_NSML_INPUT_SCRIPT = $resolvedInputScript
    $env:MELONDS_NSML_FIXED_RTC = "2020-01-01T00:00:00"
    $env:MELONDS_NSML_DISABLE_HASH = "1"
    $env:MELONDS_NSML_DISABLE_FRAME_LIMIT = "1"
    $env:MELONDS_NSML_QUIET_LOG = "1"
    $env:MELONDS_NSML_ACTIVE_FPS_START_FRAME = $ActiveFpsStartFrame.ToString()
    if ($case.Jit) {
        $env:MELONDS_NSML_ALLOW_JIT = "1"
    } else {
        $env:MELONDS_NSML_DISABLE_JIT = "1"
    }

    $caseDir = Join-Path $resolvedLogDir $case.Name
    New-Item -ItemType Directory -Force -Path $caseDir | Out-Null
    $stdout = Join-Path $caseDir "stdout.txt"
    $stderr = Join-Path $caseDir "stderr.txt"
    Remove-Item -LiteralPath $stdout, $stderr -Force -ErrorAction SilentlyContinue

    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process `
        -FilePath $resolvedExe `
        -ArgumentList @($case.Rom) `
        -Wait `
        -PassThru `
        -WindowStyle Hidden `
        -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr
    $stopwatch.Stop()
    if ($process.ExitCode -ne 0) {
        throw "$($case.Name) exited with code $($process.ExitCode); see $stdout and $stderr"
    }

    $output = Get-Content -LiteralPath $stdout -Raw
    $jitMatch = [regex]::Match($output, "NSMB Test: JIT (?<jit>enabled|disabled)")
    $totalMatch = [regex]::Match(
        $output,
        "frame limit reached at frame=(?<frames>\d+) instances=(?<instances>\d+) elapsedMs=(?<elapsed>\d+) fps=(?<fps>[0-9.]+)")
    $activeMatch = [regex]::Match(
        $output,
        "active fps startFrame=(?<start>\d+) frames=(?<frames>\d+) elapsedMs=(?<elapsed>\d+) fps=(?<fps>[0-9.]+)")
    $timingMatch = [regex]::Match(
        $output,
        "active frame timing .*?avgFrameMs=(?<avg>[0-9.]+) maxFrameMs=(?<max>[0-9.]+) maxFrame=(?<frame>\d+) over16ms=(?<over16>\d+) over25ms=(?<over25>\d+) over33ms=(?<over33>\d+)")
    if (-not $jitMatch.Success -or -not $totalMatch.Success -or -not $activeMatch.Success -or -not $timingMatch.Success) {
        throw "Could not parse benchmark markers for $($case.Name); see $stdout"
    }

    $rows.Add([pscustomobject]@{
        Case = $case.Name
        Rom = $case.Rom
        RomSha256 = (Get-FileHash -LiteralPath $case.Rom -Algorithm SHA256).Hash
        ExeSha256 = $exeSha256
        InputSha256 = $inputSha256
        Frames = $Frames
        ActiveFpsStartFrame = $ActiveFpsStartFrame
        Jit = $jitMatch.Groups["jit"].Value
        TotalFps = [double]$totalMatch.Groups["fps"].Value
        ActiveFps = [double]$activeMatch.Groups["fps"].Value
        AvgFrameMs = [double]$timingMatch.Groups["avg"].Value
        MaxFrameMs = [double]$timingMatch.Groups["max"].Value
        Over16Ms = [int]$timingMatch.Groups["over16"].Value
        Over25Ms = [int]$timingMatch.Groups["over25"].Value
        Over33Ms = [int]$timingMatch.Groups["over33"].Value
        WallMs = $stopwatch.ElapsedMilliseconds
    })
}

$summaryPath = Join-Path $resolvedLogDir "summary.csv"
$rows | Export-Csv -LiteralPath $summaryPath -NoTypeInformation -Encoding UTF8
$rows | Format-Table Case, Jit, TotalFps, ActiveFps, AvgFrameMs, MaxFrameMs, Over16Ms, Over25Ms, Over33Ms -AutoSize
Write-Host "summary: $summaryPath"
