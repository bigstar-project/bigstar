param(
    [string]$MelonDSExe = "build\release-windows-x86_64\melonDS.exe",
    [string]$BridgeExe = "tools\nsmb-net-bridge\target\release\nsmb-net-bridge.exe",
    [string]$GuiDir = "tools\nsmb-mvl-gui",
    [string]$TargetTriple = "x86_64-pc-windows-msvc"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $repoRoot (Join-Path $GuiDir "src-tauri\binaries")
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

function Copy-Sidecar {
    param(
        [string]$Source,
        [string]$Name
    )

    $sourcePath = Join-Path $repoRoot $Source
    if (-not (Test-Path $sourcePath)) {
        throw "Missing sidecar source: $sourcePath"
    }

    $destPath = Join-Path $outDir "$Name-$TargetTriple.exe"
    Copy-Item -Force -Path $sourcePath -Destination $destPath
    Write-Host "$Name sidecar: $destPath"
}

Copy-Sidecar -Source $MelonDSExe -Name "melonDS"
Copy-Sidecar -Source $BridgeExe -Name "nsmb-net-bridge"
