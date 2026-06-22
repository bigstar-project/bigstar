param(
    [string]$SourceRom = "roms\nsmb-us.nds",
    [string]$HostRom = "roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-netaid.tmp.nds",
    [string]$ClientRom = "roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-netaid.tmp.nds",
    [int]$MvlStage = 0,
    [string]$MvlSceneSettings = "",
    [ValidateSet(1, 2, 3)] [int]$MvlWins = 2,
    [ValidateSet(3, 5, 10)] [int]$MvlBigStars = 5,
    [ValidateSet("3", "5", "endless", "Endless")] [string]$MvlLives = "endless",
    [ValidateSet("random", "select")] [string]$MvlCourseMode = "random",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$manifestVersion = 1
$romFormat = "nsmb-mvl-stable-script"

function Resolve-RepoPath {
    param(
        [string]$Path,
        [switch]$MustExist
    )

    $resolved = if ([System.IO.Path]::IsPathRooted($Path)) {
        [System.IO.Path]::GetFullPath($Path)
    } else {
        [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
    }
    if ($MustExist -and !(Test-Path -LiteralPath $resolved)) {
        throw "Path not found: $resolved"
    }
    return $resolved
}

function Get-FileSha256 {
    param([string]$Path)

    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Get-TextSha256 {
    param([string]$Text)

    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
        return ([System.BitConverter]::ToString($sha.ComputeHash($bytes))).Replace("-", "").ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
}

function Get-RepoRelativePath {
    param([string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $root = [System.IO.Path]::GetFullPath($repoRoot).TrimEnd("\", "/")
    if ($fullPath.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $fullPath.Substring($root.Length).TrimStart("\", "/").Replace("\", "/")
    }
    return $fullPath.Replace("\", "/")
}

function Get-StableRomGeneratorId {
    $paths = New-Object System.Collections.Generic.List[string]
    foreach ($relative in @(
        "Cargo.lock",
        "tools\nsmb-mvl-rom\Cargo.toml",
        "scripts\generate-nsmb-mvl-stable-roms.ps1"
    )) {
        $path = Join-Path $repoRoot $relative
        if (Test-Path -LiteralPath $path) {
            $paths.Add($path)
        }
    }

    $srcRoot = Join-Path $repoRoot "tools\nsmb-mvl-rom\src"
    if (Test-Path -LiteralPath $srcRoot) {
        Get-ChildItem -LiteralPath $srcRoot -Recurse -File |
            Sort-Object FullName |
            ForEach-Object { $paths.Add($_.FullName) }
    }

    $lines = foreach ($path in ($paths | Sort-Object)) {
        $relativePath = Get-RepoRelativePath $path
        "$relativePath=$((Get-FileSha256 $path))"
    }
    return Get-TextSha256 ($lines -join "`n")
}

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

function Get-RomManifestPath {
    param([string]$RomPath)

    return "$RomPath.mvl-manifest.json"
}

function Get-ObjectField {
    param(
        $Object,
        [string]$Name
    )

    if ($Object -is [System.Collections.IDictionary]) {
        return $Object[$Name]
    }
    return $Object.$Name
}

function Test-OptionsMatch {
    param(
        $Actual,
        $Expected
    )

    return (Get-ObjectField $Actual "mvl_stage") -eq (Get-ObjectField $Expected "mvl_stage") `
        -and (Get-ObjectField $Actual "mvl_scene_settings") -eq (Get-ObjectField $Expected "mvl_scene_settings") `
        -and (Get-ObjectField $Actual "mvl_wins") -eq (Get-ObjectField $Expected "mvl_wins") `
        -and (Get-ObjectField $Actual "mvl_big_stars") -eq (Get-ObjectField $Expected "mvl_big_stars") `
        -and (Get-ObjectField $Actual "mvl_lives") -eq (Get-ObjectField $Expected "mvl_lives") `
        -and (Get-ObjectField $Actual "mvl_course_mode") -eq (Get-ObjectField $Expected "mvl_course_mode")
}

function Test-InputsMatch {
    param(
        $Actual,
        $Expected
    )

    return (Get-ObjectField $Actual "manifest_version") -eq (Get-ObjectField $Expected "manifest_version") `
        -and (Get-ObjectField $Actual "rom_format") -eq (Get-ObjectField $Expected "rom_format") `
        -and (Get-ObjectField $Actual "generator_id") -eq (Get-ObjectField $Expected "generator_id") `
        -and (Get-ObjectField $Actual "source_rom_sha256") -eq (Get-ObjectField $Expected "source_rom_sha256") `
        -and (Test-OptionsMatch -Actual (Get-ObjectField $Actual "options") -Expected (Get-ObjectField $Expected "options"))
}

function Read-RomManifest {
    param([string]$RomPath)

    $manifestPath = Get-RomManifestPath $RomPath
    if (!(Test-Path -LiteralPath $manifestPath)) {
        return $null
    }
    try {
        return Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    } catch {
        return $null
    }
}

function Test-ReusableRoms {
    param(
        [string]$HostPath,
        [string]$ClientPath,
        $ExpectedInputs
    )

    if (!(Test-Path -LiteralPath $HostPath) -or !(Test-Path -LiteralPath $ClientPath)) {
        return $false
    }

    $hostManifest = Read-RomManifest $HostPath
    $clientManifest = Read-RomManifest $ClientPath
    if ($null -eq $hostManifest -or $null -eq $clientManifest) {
        return $false
    }
    if (!(Test-InputsMatch -Actual $hostManifest.inputs -Expected $ExpectedInputs)) {
        return $false
    }
    if (!(Test-InputsMatch -Actual $clientManifest.inputs -Expected $ExpectedInputs)) {
        return $false
    }

    $hostSha = Get-FileSha256 $HostPath
    $clientSha = Get-FileSha256 $ClientPath
    return $hostManifest.identity.host_rom_sha256 -eq $hostSha `
        -and $hostManifest.identity.client_rom_sha256 -eq $clientSha `
        -and $clientManifest.identity.host_rom_sha256 -eq $hostSha `
        -and $clientManifest.identity.client_rom_sha256 -eq $clientSha `
        -and $hostManifest.identity.rom_pair_id -eq $clientManifest.identity.rom_pair_id
}

function Write-RomManifest {
    param(
        [string]$RomPath,
        $Inputs,
        $Identity
    )

    $manifest = [ordered]@{
        inputs = $Inputs
        identity = $Identity
    }
    $manifestPath = Get-RomManifestPath $RomPath
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
}

$sourceRomPath = Resolve-RepoPath $SourceRom -MustExist
$hostRomPath = Resolve-RepoPath $HostRom
$clientRomPath = Resolve-RepoPath $ClientRom

$hostParent = [System.IO.Path]::GetDirectoryName($hostRomPath)
$clientParent = [System.IO.Path]::GetDirectoryName($clientRomPath)
if (![string]::IsNullOrWhiteSpace($hostParent)) {
    New-Item -ItemType Directory -Force -Path $hostParent | Out-Null
}
if (![string]::IsNullOrWhiteSpace($clientParent)) {
    New-Item -ItemType Directory -Force -Path $clientParent | Out-Null
}

$effectiveSceneSettings = if ($MvlSceneSettings) { $MvlSceneSettings } else { Convert-ToMvlSceneSettings -Stage $MvlStage }
$effectiveLives = $MvlLives.ToLowerInvariant()
$generatorId = Get-StableRomGeneratorId
$sourceRomSha256 = Get-FileSha256 $sourceRomPath
$expectedInputs = [ordered]@{
    manifest_version = $manifestVersion
    rom_format = $romFormat
    generator_id = $generatorId
    source_rom_sha256 = $sourceRomSha256
    options = [ordered]@{
        mvl_stage = $MvlStage
        mvl_scene_settings = $effectiveSceneSettings
        mvl_wins = $MvlWins
        mvl_big_stars = $MvlBigStars
        mvl_lives = $effectiveLives
        mvl_course_mode = $MvlCourseMode
    }
}

if (!$Force -and (Test-ReusableRoms -HostPath $hostRomPath -ClientPath $clientRomPath -ExpectedInputs $expectedInputs)) {
    Write-Host "stable MvL ROMs are up to date: host=$hostRomPath client=$clientRomPath"
    return
}

$previousErrorActionPreference = $ErrorActionPreference
try {
    $ErrorActionPreference = "Continue"
    & cargo run --release --manifest-path (Join-Path $repoRoot "tools\nsmb-mvl-rom\Cargo.toml") -- `
        generate-stable `
        --source-rom $sourceRomPath `
        --host-rom $hostRomPath `
        --client-rom $clientRomPath `
        --stage $MvlStage `
        --wins $MvlWins `
        --big-stars $MvlBigStars `
        --lives $effectiveLives `
        --course-mode $MvlCourseMode `
        --scene-settings $effectiveSceneSettings
} finally {
    $ErrorActionPreference = $previousErrorActionPreference
}

if ($LASTEXITCODE -ne 0) {
    throw "stable MvL ROM generation failed with exit code $LASTEXITCODE"
}

$hostSha256 = Get-FileSha256 $hostRomPath
$clientSha256 = Get-FileSha256 $clientRomPath
$romPairId = Get-TextSha256 "$generatorId`n$hostSha256`n$clientSha256"
$identity = [ordered]@{
    rom_pair_id = $romPairId
    generator_id = $generatorId
    host_rom_sha256 = $hostSha256
    client_rom_sha256 = $clientSha256
}

Write-RomManifest -RomPath $hostRomPath -Inputs $expectedInputs -Identity $identity
Write-RomManifest -RomPath $clientRomPath -Inputs $expectedInputs -Identity $identity
Write-Host "wrote stable MvL ROMs: host=$hostRomPath client=$clientRomPath romPairId=$romPairId"
