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
        [int]$Wins,
        [int]$BigStars,
        [string]$Lives,
        [string]$CourseMode
    )

    $bigStarField = switch ($BigStars) {
        3 { 4 }
        5 { 4 }
        10 { 8 }
        default { throw "MvlBigStars must be 3, 5, or 10: $BigStars" }
    }
    $lifeField = switch ($Lives.ToLowerInvariant()) {
        "3" { 3 }
        "5" { 5 }
        "endless" { 0xff }
        default { throw "MvlLives must be 3, 5, or endless: $Lives" }
    }

    if ($Wins -lt 1 -or $Wins -gt 3) {
        throw "MvlWins must be 1, 2, or 3: $Wins"
    }

    # Direct MvL skips the normal settings/result flow, so match wins are
    # enforced by the runtime restart controller. Keep the per-round rule byte
    # on the stable post-course-select value. The GUI exposes the normal MvL
    # course choices we can support in direct boot: random and choose each time.
    $ruleHighNibble = 0xb0

    # Default 0xB4FF00 corresponds to Wins=2, BigStar=5, Lives=Endless, Course=Choose Each Time.
    $packedRules = $ruleHighNibble -bor ($bigStarField -band 0xf)
    $settings = (($packedRules -band 0xff) -shl 16) -bor (($lifeField -band 0xff) -shl 8)
    return "0x$('{0:x6}' -f $settings)"
}

if (!(Test-Path $SourceRom)) {
    throw "Source ROM not found: $SourceRom"
}

$effectiveSceneSettings = if ($MvlSceneSettings) { $MvlSceneSettings } else { Convert-ToMvlSceneSettings -Wins $MvlWins -BigStars $MvlBigStars -Lives $MvlLives -CourseMode $MvlCourseMode }
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
