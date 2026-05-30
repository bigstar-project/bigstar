param(
    [int]$Frames = 180,
    [string]$Exe = "build\debug-windows-x86_64\melonDS.exe",
    [string]$Rom = "roms\nsmb.nds",
    [string]$InputScript = "tests\nsmb_smoke.inputs",
    [string]$LogDir = "logs"
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stdout = Join-Path $LogDir "nsmb-smoke.stdout.txt"
$hashLog = Join-Path $LogDir "nsmb-smoke.hash.csv"
Remove-Item -Force $stdout, $hashLog -ErrorAction SilentlyContinue

$env:MELONDS_NSML_TEST = "1"
$env:MELONDS_NSML_TEST_FRAMES = "$Frames"
$env:MELONDS_NSML_INPUT_SCRIPT = (Resolve-Path $InputScript).Path
$env:MELONDS_NSML_HASH_LOG = (Join-Path (Resolve-Path $LogDir).Path "nsmb-smoke.hash.csv")
$env:MELONDS_NSML_HASH_INTERVAL = "30"

& (Resolve-Path $Exe).Path (Resolve-Path $Rom).Path *> $stdout
$exitCode = $LASTEXITCODE

if ($exitCode -ne 0) {
    throw "melonDS exited with code $exitCode. See $stdout"
}

if (-not (Test-Path $hashLog)) {
    throw "hash log was not created: $hashLog"
}

$hashLines = Get-Content $hashLog
if ($hashLines.Count -lt 2) {
    throw "hash log did not contain frame hashes: $hashLog"
}

if (-not (Select-String -Path $stdout -Pattern "NSMB Test: frame limit reached" -Quiet)) {
    throw "frame-limit completion marker was not found in $stdout"
}

Write-Host "NSMB smoke passed: frames=$Frames hashRows=$($hashLines.Count - 1)"
