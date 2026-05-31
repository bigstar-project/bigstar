param(
    [switch]$BuildTauriBundle,
    [string]$BridgeExe = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$guiDir = Join-Path $repo "tools\nsmb-mvl-gui"
$guiManifest = Join-Path $guiDir "src-tauri\Cargo.toml"
$bridgeManifest = Join-Path $repo "tools\nsmb-net-bridge\Cargo.toml"

function Resolve-GuiDebugExe {
    Invoke-NativeChecked cargo build --manifest-path $guiManifest
    $exe = Join-Path $guiDir "src-tauri\target\debug\nsmb-mvl-gui.exe"
    if (!(Test-Path $exe)) {
        throw "nsmb-mvl-gui debug exe was not produced at $exe"
    }
    return (Resolve-Path $exe).Path
}

function Invoke-NativeChecked {
    & $args[0] @($args | Select-Object -Skip 1)
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $($args -join ' ')"
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
        (Join-Path $repo "tools\nsmb-net-bridge\target\debug\nsmb-net-bridge.exe"),
        (Join-Path $repo "tools\nsmb-net-bridge\target\release\nsmb-net-bridge.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-BridgeSupportsSignalingSmoke $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    Set-LibclangPathIfAvailable
    Invoke-NativeChecked cargo build --features webrtc --manifest-path $bridgeManifest
    $built = Join-Path $repo "tools\nsmb-net-bridge\target\debug\nsmb-net-bridge.exe"
    if (!(Test-Path $built)) {
        throw "nsmb-net-bridge.exe was not produced at $built"
    }
    if (!(Test-BridgeSupportsSignalingSmoke $built)) {
        throw "Built nsmb-net-bridge.exe does not support webrtc-signaling-udp-pair-smoke"
    }
    return (Resolve-Path $built).Path
}

Push-Location $repo
try {
    Invoke-NativeChecked cargo test --manifest-path $guiManifest

    $bridge = Resolve-BridgeExe
    Invoke-NativeChecked $bridge webrtc-signaling-udp-pair-smoke
    Invoke-NativeChecked (Resolve-GuiDebugExe) --preflight

    if ($BuildTauriBundle) {
        Push-Location $guiDir
        try {
            Invoke-NativeChecked corepack pnpm build
        } finally {
            Pop-Location
        }
        Invoke-NativeChecked (Join-Path $guiDir "src-tauri\target\release\nsmb-mvl-gui.exe") --preflight
    }

    Write-Host "NSMB MvL GUI launch smoke passed"
} finally {
    Pop-Location
}
