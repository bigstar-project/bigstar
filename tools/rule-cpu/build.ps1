param([string]$Cargo = 'cargo')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $root '../..')).Path
$output = Join-Path $repo 'build/rule-cpu'
Push-Location $root
try {
    & $Cargo build --release --locked
    if ($LASTEXITCODE -ne 0) { throw 'CPU Rust compilation failed' }
    $metadata = & $Cargo metadata --no-deps --format-version 1 --locked
    if ($LASTEXITCODE -ne 0) { throw 'CPU Cargo metadata failed' }
    $target = ($metadata | ConvertFrom-Json).target_directory
    $binary = Join-Path $target 'release/bigstar-rule-cpu.exe'
    & $binary --check
    if ($LASTEXITCODE -ne 0) { throw 'CPU worker self-check failed' }
    New-Item -ItemType Directory -Path $output -Force | Out-Null
    Copy-Item -LiteralPath $binary -Destination (Join-Path $output 'bigstar-rule-cpu.exe') -Force
} finally {
    Pop-Location
}
