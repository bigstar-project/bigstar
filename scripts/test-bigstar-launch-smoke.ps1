param(
    [switch]$BuildTauriBundle,
    [string]$BridgeExe = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$guiDir = Join-Path $repo "tools\bigstar"
$guiManifest = Join-Path $guiDir "src-tauri\Cargo.toml"
$bridgeManifest = Join-Path $repo "tools\bigstar-net-bridge\Cargo.toml"
$melonReleaseExe = Join-Path $repo "build\release-windows-x86_64\melonDS.exe"
$prepareSidecarsScript = Join-Path $repo "scripts\prepare-bigstar-tauri-sidecars.ps1"

function Resolve-GuiDebugExe {
    Invoke-NativeChecked cargo build --manifest-path $guiManifest
    $exe = Join-Path $guiDir "src-tauri\target\debug\bigstar.exe"
    if (!(Test-Path $exe)) {
        throw "bigstar debug exe was not produced at $exe"
    }
    return (Resolve-Path $exe).Path
}

function Invoke-NativeChecked {
    & $args[0] @($args | Select-Object -Skip 1)
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $($args -join ' ')"
    }
}

function Invoke-GuiPreflightChecked($Path) {
    $process = Start-Process `
        -FilePath $Path `
        -ArgumentList "--preflight" `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
    if ($process.ExitCode -ne 0) {
        throw "GUI preflight failed with exit code $($process.ExitCode): $Path"
    }
}

function Set-LibclangPathIfAvailable {
    if ($env:LIBCLANG_PATH -and (Test-Path (Join-Path $env:LIBCLANG_PATH "libclang.dll"))) {
        return
    }

    $candidates = @(
        "C:\Program Files\LLVM\bin",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path (Join-Path $candidate "libclang.dll")) {
            $env:LIBCLANG_PATH = $candidate
            return
        }
    }
}

function Test-BridgeSupportsSignalingSmoke($Path) {
    if (!(Test-Path $Path)) {
        return $false
    }

    $output = & $Path 2>&1 | Out-String
    return $output.Contains("webrtc-signaling-udp-pair-smoke")
}

function Resolve-BridgeExe {
    if ($BridgeExe) {
        $resolved = (Resolve-Path $BridgeExe).Path
        if (!(Test-BridgeSupportsSignalingSmoke $resolved)) {
            throw "Bridge executable does not support webrtc-signaling-udp-pair-smoke: $resolved"
        }
        return $resolved
    }

    $candidates = @(
        (Join-Path $repo "tools\bigstar-net-bridge\target\debug\bigstar-net-bridge.exe"),
        (Join-Path $repo "tools\bigstar-net-bridge\target\release\bigstar-net-bridge.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-BridgeSupportsSignalingSmoke $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    Set-LibclangPathIfAvailable
    Invoke-NativeChecked cargo build --features webrtc --manifest-path $bridgeManifest
    $built = Join-Path $repo "tools\bigstar-net-bridge\target\debug\bigstar-net-bridge.exe"
    if (!(Test-Path $built)) {
        throw "bigstar-net-bridge.exe was not produced at $built"
    }
    if (!(Test-BridgeSupportsSignalingSmoke $built)) {
        throw "Built bigstar-net-bridge.exe does not support webrtc-signaling-udp-pair-smoke"
    }
    return (Resolve-Path $built).Path
}

function Resolve-ReleaseBridgeExe {
    $release = Join-Path $repo "tools\bigstar-net-bridge\target\release\bigstar-net-bridge.exe"
    if (Test-BridgeSupportsSignalingSmoke $release) {
        return (Resolve-Path $release).Path
    }

    Set-LibclangPathIfAvailable
    Invoke-NativeChecked cargo build --release --features webrtc --manifest-path $bridgeManifest
    if (!(Test-BridgeSupportsSignalingSmoke $release)) {
        throw "Release bigstar-net-bridge.exe does not support webrtc-signaling-udp-pair-smoke"
    }
    return (Resolve-Path $release).Path
}

Push-Location $repo
try {
    Invoke-NativeChecked cargo test --manifest-path $guiManifest

    $bridge = Resolve-BridgeExe
    Invoke-NativeChecked $bridge webrtc-signaling-udp-pair-smoke
    Invoke-GuiPreflightChecked (Resolve-GuiDebugExe)

    if ($BuildTauriBundle) {
        Invoke-NativeChecked powershell -NoProfile -ExecutionPolicy Bypass -File $prepareSidecarsScript -BridgeExe (Resolve-ReleaseBridgeExe)
        Push-Location $guiDir
        try {
            Invoke-NativeChecked corepack pnpm build
        } finally {
            Pop-Location
        }
        $targetMelon = Join-Path $guiDir "src-tauri\target\release\melonDS.exe"
        if ((Get-FileHash $targetMelon).Hash -ne (Get-FileHash $melonReleaseExe).Hash) {
            throw "Tauri release melonDS sidecar does not match $melonReleaseExe"
        }
        Invoke-GuiPreflightChecked (Join-Path $guiDir "src-tauri\target\release\bigstar.exe")
    }

    Write-Host "NSMB MvL GUI launch smoke passed"
} finally {
    Pop-Location
}
