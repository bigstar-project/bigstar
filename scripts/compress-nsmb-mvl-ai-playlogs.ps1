param(
    [string]$Session = "",
    [switch]$KeepOriginal
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

function Resolve-SessionPath {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        $sessions = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot "logs") -Recurse -Filter "recording-session.json" -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending)
        if ($sessions.Count -eq 0) {
            throw "recording-session.json not found under logs; pass -Session explicitly"
        }
        return $sessions[0].FullName
    }
    if ([System.IO.Path]::IsPathRooted($Value)) {
        return (Resolve-Path -LiteralPath $Value).Path
    }
    return (Resolve-Path -LiteralPath (Join-Path $repoRoot $Value)).Path
}

function Compress-Jsonl {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) { return "" }
    $source = if ([System.IO.Path]::IsPathRooted($Path)) { $Path } else { Join-Path $repoRoot $Path }
    if ($source.EndsWith(".gz", [System.StringComparison]::OrdinalIgnoreCase)) { return $source }
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { return "" }

    $target = "$source.gz"
    $inputStream = [System.IO.File]::OpenRead($source)
    try {
        $outputStream = [System.IO.File]::Create($target)
        try {
            $gzipStream = [System.IO.Compression.GZipStream]::new(
                $outputStream,
                [System.IO.Compression.CompressionLevel]::Optimal
            )
            try {
                $inputStream.CopyTo($gzipStream)
            } finally {
                $gzipStream.Dispose()
            }
        } finally {
            $outputStream.Dispose()
        }
    } finally {
        $inputStream.Dispose()
    }

    if (-not $KeepOriginal) {
        Remove-Item -LiteralPath $source
    }
    return $target
}

function Update-RecordingManifest {
    param(
        [string]$ManifestPath,
        [string]$GzipPath
    )

    if ([string]::IsNullOrWhiteSpace($ManifestPath)) { return }
    if ([string]::IsNullOrWhiteSpace($GzipPath)) { return }
    if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) { return }

    $manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $manifestDir = Split-Path -Parent $ManifestPath
    $gzipDir = Split-Path -Parent $GzipPath
    $manifest.playLog = if ($gzipDir -eq $manifestDir) {
        Split-Path -Leaf $GzipPath
    } else {
        $GzipPath
    }
    ConvertTo-Json -InputObject $manifest -Depth 32 | Set-Content -LiteralPath $ManifestPath -Encoding UTF8
}

$sessionPath = Resolve-SessionPath $Session
$sessionData = Get-Content -LiteralPath $sessionPath -Raw -Encoding UTF8 | ConvertFrom-Json

$hostGzip = Compress-Jsonl -Path ([string]$sessionData.hostAIPlayLog)
$clientGzip = Compress-Jsonl -Path ([string]$sessionData.clientAIPlayLog)
$hostObservationV2Gzip = Compress-Jsonl -Path ([string]$sessionData.hostAIObservationV2Log)
$clientObservationV2Gzip = Compress-Jsonl -Path ([string]$sessionData.clientAIObservationV2Log)

$sessionDir = Split-Path -Parent $sessionPath
$hostManifest = Join-Path (Join-Path $sessionDir "host") "recording.json"
$clientManifest = Join-Path (Join-Path $sessionDir "client") "recording.json"

Update-RecordingManifest -ManifestPath $hostManifest -GzipPath $hostGzip
Update-RecordingManifest -ManifestPath $clientManifest -GzipPath $clientGzip

if (-not [string]::IsNullOrWhiteSpace($hostGzip)) {
    $sessionData.hostAIPlayLog = $hostGzip
}
if (-not [string]::IsNullOrWhiteSpace($clientGzip)) {
    $sessionData.clientAIPlayLog = $clientGzip
}
if (-not [string]::IsNullOrWhiteSpace($hostObservationV2Gzip)) {
    $sessionData.hostAIObservationV2Log = $hostObservationV2Gzip
}
if (-not [string]::IsNullOrWhiteSpace($clientObservationV2Gzip)) {
    $sessionData.clientAIObservationV2Log = $clientObservationV2Gzip
}
$sessionData | Add-Member -NotePropertyName gzipPlayLog -NotePropertyValue $true -Force
ConvertTo-Json -InputObject $sessionData -Depth 32 | Set-Content -LiteralPath $sessionPath -Encoding UTF8

Write-Host "Compressed AI playlogs: host=$(if ($hostGzip) { $hostGzip } else { 'off' }) client=$(if ($clientGzip) { $clientGzip } else { 'off' })"
Write-Host "Compressed AI observation v2 logs: host=$(if ($hostObservationV2Gzip) { $hostObservationV2Gzip } else { 'off' }) client=$(if ($clientObservationV2Gzip) { $clientObservationV2Gzip } else { 'off' })"
