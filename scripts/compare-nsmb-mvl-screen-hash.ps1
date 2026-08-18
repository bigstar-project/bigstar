param(
    [Parameter(Mandatory = $true)] [string]$ReferenceLogDir,
    [Parameter(Mandatory = $true)] [string]$CandidateLogDir,
    [ValidateRange(0, 120)] [int]$PostCorrectionFrames = 2,
    [ValidateSet("host", "client")] [string[]]$Roles = @("host", "client"),
    [ValidateRange(0, 1000000)] [int]$StartFrame = 870,
    [ValidateRange(0, 1000000)] [int]$EndFrame = 0,
    [string]$OutputPath = "",
    [switch]$AllowConvergence,
    [switch]$NoFail
)

$ErrorActionPreference = "Stop"

function Resolve-RequiredPath {
    param([string]$Path, [string]$Description)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Description was not found: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Import-ScreenHashMap {
    param([string]$Path)
    $rows = @(Import-Csv -LiteralPath $Path)
    if ($rows.Count -eq 0 -or -not ($rows[0].PSObject.Properties.Name -contains "screenHash")) {
        throw "screenHash column was not found: $Path"
    }

    $map = @{}
    foreach ($row in $rows) {
        if ($row.instance -ne "0") {
            continue
        }
        $frame = [uint32]$row.frame
        if ($frame -lt $StartFrame -or ($EndFrame -gt 0 -and $frame -gt $EndFrame)) {
            continue
        }
        $hash = ([string]$row.screenHash).Trim().ToLowerInvariant()
        if ([string]::IsNullOrWhiteSpace($hash) -or $hash -eq "0") {
            throw "invalid screenHash at frame $frame in $Path"
        }
        $map[$frame] = $hash
    }
    return $map
}

function Find-CorrectionFrames {
    param([string]$Path)
    $frames = [System.Collections.Generic.HashSet[uint32]]::new()
    foreach ($line in Get-Content -LiteralPath $Path -Encoding UTF8) {
        if ($line -match "completed ROM-loop correction frame=(\d+)") {
            $frame = [uint32]$Matches[1]
            if ($frame -ge $StartFrame -and ($EndFrame -eq 0 -or $frame -le $EndFrame)) {
                [void]$frames.Add($frame)
            }
        }
    }
    return @($frames | Sort-Object)
}

$referenceRoot = Resolve-RequiredPath -Path $ReferenceLogDir -Description "reference log directory"
$candidateRoot = Resolve-RequiredPath -Path $CandidateLogDir -Description "candidate log directory"
$results = [System.Collections.Generic.List[object]]::new()
$roleSummaries = [System.Collections.Generic.List[object]]::new()
$correctionSummaries = [System.Collections.Generic.List[object]]::new()

foreach ($role in $Roles) {
    $referenceHashPath = Resolve-RequiredPath `
        -Path (Join-Path $referenceRoot "$role\$role.hash.csv") `
        -Description "$role reference hash log"
    $candidateHashPath = Resolve-RequiredPath `
        -Path (Join-Path $candidateRoot "$role\$role.hash.csv") `
        -Description "$role candidate hash log"
    $candidateStdoutPath = Resolve-RequiredPath `
        -Path (Join-Path $candidateRoot "$role\$role.stdout.txt") `
        -Description "$role candidate stdout"

    $reference = Import-ScreenHashMap -Path $referenceHashPath
    $candidate = Import-ScreenHashMap -Path $candidateHashPath
    $corrections = @(Find-CorrectionFrames -Path $candidateStdoutPath)
    if ($corrections.Count -eq 0) {
        $roleSummaries.Add([pscustomobject]@{
            role = $role
            corrections = 0
            selectedFrames = 0
            matched = 0
            mismatched = 0
            missingReference = 0
            missingCandidate = 0
            unconvergedCorrections = 0
            maxConvergenceFrames = 0
            bestReferenceOffset = 0
            bestOffsetMatches = 0
        })
        continue
    }

    $selectedFrames = [System.Collections.Generic.HashSet[uint32]]::new()
    foreach ($correctionFrame in $corrections) {
        for ($offset = 0; $offset -le $PostCorrectionFrames; $offset++) {
            $frame = [uint32]($correctionFrame + $offset)
            if ($EndFrame -eq 0 -or $frame -le $EndFrame) {
                [void]$selectedFrames.Add($frame)
            }
        }
    }

    $matched = 0
    $mismatched = 0
    $missingReference = 0
    $missingCandidate = 0
    foreach ($frame in @($selectedFrames | Sort-Object)) {
        $referenceHash = if ($reference.ContainsKey($frame)) { $reference[$frame] } else { "" }
        $candidateHash = if ($candidate.ContainsKey($frame)) { $candidate[$frame] } else { "" }
        $status = "match"
        if ([string]::IsNullOrEmpty($referenceHash)) {
            $status = "missing-reference"
            $missingReference++
        } elseif ([string]::IsNullOrEmpty($candidateHash)) {
            $status = "missing-candidate"
            $missingCandidate++
        } elseif ($referenceHash -ne $candidateHash) {
            $status = "mismatch"
            $mismatched++
        } else {
            $matched++
        }
        $results.Add([pscustomobject]@{
            role = $role
            frame = $frame
            status = $status
            referenceScreenHash = $referenceHash
            candidateScreenHash = $candidateHash
        })
    }

    $offsetScores = @(
        foreach ($offset in -3..3) {
            $offsetMatches = 0
            $offsetCompared = 0
            foreach ($frame in $selectedFrames) {
                $referenceFrame = [long]$frame + $offset
                if ($referenceFrame -lt 0) {
                    continue
                }
                if ($reference.ContainsKey([uint32]$referenceFrame) -and $candidate.ContainsKey($frame)) {
                    $offsetCompared++
                    if ($reference[[uint32]$referenceFrame] -eq $candidate[$frame]) {
                        $offsetMatches++
                    }
                }
            }
            [pscustomobject]@{ offset = $offset; matches = $offsetMatches; compared = $offsetCompared }
        }
    )
    $bestOffset = $offsetScores | Sort-Object -Property @{ Expression = "matches"; Descending = $true }, @{ Expression = { [Math]::Abs($_.offset) }; Descending = $false } | Select-Object -First 1
    $unconvergedCorrections = 0
    $maxConvergenceFrames = 0
    foreach ($correctionFrame in $corrections) {
        $lastNonMatchingOffset = $null
        for ($offset = 0; $offset -le $PostCorrectionFrames; $offset++) {
            $frame = [uint32]($correctionFrame + $offset)
            if ($EndFrame -gt 0 -and $frame -gt $EndFrame) {
                $lastNonMatchingOffset = $PostCorrectionFrames
                break
            }
            if (-not $reference.ContainsKey($frame) -or -not $candidate.ContainsKey($frame) -or
                $reference[$frame] -ne $candidate[$frame]) {
                $lastNonMatchingOffset = $offset
            }
        }
        $stableMatchingOffset = if ($null -eq $lastNonMatchingOffset) {
            0
        } elseif ($lastNonMatchingOffset -lt $PostCorrectionFrames) {
            $lastNonMatchingOffset + 1
        } else {
            $null
        }
        if ($null -eq $stableMatchingOffset) {
            $unconvergedCorrections++
        } else {
            $maxConvergenceFrames = [Math]::Max($maxConvergenceFrames, $stableMatchingOffset)
        }
        $correctionSummaries.Add([pscustomobject]@{
            role = $role
            correctionFrame = $correctionFrame
            stableMatchingOffset = if ($null -eq $stableMatchingOffset) { "" } else { $stableMatchingOffset }
            converged = $null -ne $stableMatchingOffset
        })
    }
    $roleSummaries.Add([pscustomobject]@{
        role = $role
        corrections = $corrections.Count
        selectedFrames = $selectedFrames.Count
        matched = $matched
        mismatched = $mismatched
        missingReference = $missingReference
        missingCandidate = $missingCandidate
        unconvergedCorrections = $unconvergedCorrections
        maxConvergenceFrames = $maxConvergenceFrames
        bestReferenceOffset = $bestOffset.offset
        bestOffsetMatches = $bestOffset.matches
    })
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $candidateRoot "screen-hash-comparison.csv"
}
$outputDirectory = Split-Path -Parent $OutputPath
if ($outputDirectory) {
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}
$results | Export-Csv -LiteralPath $OutputPath -NoTypeInformation -Encoding UTF8
$convergenceOutputPath = [System.IO.Path]::Combine(
    [System.IO.Path]::GetDirectoryName($OutputPath),
    ([System.IO.Path]::GetFileNameWithoutExtension($OutputPath) + ".convergence.csv"))
$correctionSummaries | Export-Csv -LiteralPath $convergenceOutputPath -NoTypeInformation -Encoding UTF8
$roleSummaries | Format-Table -AutoSize

if (($roleSummaries | Measure-Object -Property corrections -Sum).Sum -eq 0) {
    throw "no ROM-loop correction frames were found in the selected roles"
}

$failures = if ($AllowConvergence) {
    @($roleSummaries | Where-Object { $_.unconvergedCorrections -gt 0 })
} else {
    @($roleSummaries | Where-Object {
        $_.mismatched -gt 0 -or $_.missingReference -gt 0 -or $_.missingCandidate -gt 0
    })
}
if ($failures.Count -gt 0 -and -not $NoFail) {
    throw "screen hash comparison failed for $($failures.Count) role(s). See $OutputPath"
}

Write-Host "NSMB MvL screen hash comparison completed: $OutputPath convergence=$convergenceOutputPath"
