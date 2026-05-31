param(
    [string]$SourceRom = "roms\nsmb-us.nds",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-netaid.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-netaid.tmp.nds",
    [int]$MvlStage = 0,
    [string]$MvlSceneSettings = "",
    [ValidateSet(1, 2, 3)] [int]$MvlWins = 2,
    [ValidateSet(3, 5, 10)] [int]$MvlBigStars = 5,
    [ValidateSet("3", "5", "endless", "Endless")] [string]$MvlLives = "endless",
    [ValidateSet("random", "select")] [string]$MvlCourseMode = "random"
)

$ErrorActionPreference = "Stop"

function Convert-ToMvlSceneSettings {
    param(
        [int]$Stage
    )

    if ($Stage -lt 0 -or $Stage -gt 4) {
        throw "MvlStage must be between 0 and 4: $Stage"
    }
    $settings = ((0xb4 + $Stage) -shl 16) -bor 0xff00
    return "0x$('{0:x6}' -f $settings)"
}

if (!(Test-Path $SourceRom)) {
    throw "Source ROM not found: $SourceRom"
}

$effectiveSceneSettings = if ($MvlSceneSettings) { $MvlSceneSettings } else { Convert-ToMvlSceneSettings -Stage $MvlStage }
$effectiveLives = $MvlLives.ToLowerInvariant()

& cargo run --release --manifest-path tools\nsmb-mvl-rom\Cargo.toml -- `
    generate-stable `
    --source-rom $SourceRom `
    --host-rom $HostRom `
    --client-rom $ClientRom `
    --stage $MvlStage `
    --wins $MvlWins `
    --big-stars $MvlBigStars `
    --lives $effectiveLives `
    --course-mode $MvlCourseMode `
    --scene-settings $effectiveSceneSettings
