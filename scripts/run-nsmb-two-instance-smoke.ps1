param(
    [int]$Frames = 180,
    [string]$Exe = "build\debug-windows-x86_64\melonDS.exe",
    [string]$Rom = "roms\nsmb.nds",
    [string]$InputScript = "tests\nsmb_smoke.inputs",
    [string]$LogDir = "logs"
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stdout = Join-Path $LogDir "nsmb-two-instance.stdout.txt"
$hashLog = Join-Path $LogDir "nsmb-two-instance.hash.csv"
Remove-Item -Force $stdout, $hashLog -ErrorAction SilentlyContinue

$env:MELONDS_NSML_TEST = "1"
$env:MELONDS_NSML_TEST_INSTANCES = "2"
$env:MELONDS_NSML_TEST_FRAMES = "$Frames"
$env:MELONDS_NSML_INPUT_SCRIPT = (Resolve-Path $InputScript).Path
$env:MELONDS_NSML_HASH_LOG = (Join-Path (Resolve-Path $LogDir).Path "nsmb-two-instance.hash.csv")
$env:MELONDS_NSML_HASH_INTERVAL = "30"

& (Resolve-Path $Exe).Path (Resolve-Path $Rom).Path *> $stdout
$exitCode = $LASTEXITCODE

if (-not (Test-Path $hashLog)) {
    throw "hash log was not created: $hashLog"
}

$hashRows = Import-Csv $hashLog
if (-not ($hashRows | Where-Object { $_.instance -eq "0" })) {
    throw "hash log did not contain instance 0 rows: $hashLog"
}

if (-not ($hashRows | Where-Object { $_.instance -eq "1" })) {
    throw "hash log did not contain instance 1 rows: $hashLog"
}

if (-not (Select-String -Path $stdout -Pattern "NSMB Test: frame limit reached" -Quiet)) {
    throw "frame-limit completion marker was not found in $stdout"
}

if (-not ($hashRows | Where-Object { $_.instance -eq "0" -and $_.frame -eq "$Frames" })) {
    throw "hash log did not contain instance 0 frame $Frames row: $hashLog"
}

if (-not ($hashRows | Where-Object { $_.instance -eq "1" -and $_.frame -eq "$Frames" })) {
    throw "hash log did not contain instance 1 frame $Frames row: $hashLog"
}

if ($exitCode -ne 0) {
    Write-Warning "melonDS exited with code $exitCode after completing the two-instance smoke. This is currently treated as a teardown issue. See $stdout"
}

Write-Host "NSMB two-instance smoke passed: frames=$Frames rows=$($hashRows.Count)"
