param(
    [string]$Python = 'python',
    [string]$Compiler = 'clang++'
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $root '../..')).Path
$output = Join-Path $repo 'build/rule-cpu'
New-Item -ItemType Directory -Path (Join-Path $root 'native') -Force | Out-Null
& $Compiler -shared -O2 -fno-fast-math -ffp-contract=off (Join-Path $root 'nsmb_interception.cpp') -o (Join-Path $root 'native/nsmb_interception.dll')
if ($LASTEXITCODE -ne 0) { throw 'CPU native compilation failed' }
$sources = @{}
Get-ChildItem -LiteralPath $root -File | Where-Object { $_.Extension -in '.py', '.cpp', '.json' } | ForEach-Object {
    $sources[$_.Name] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower()
}
$manifest = @{abi=1; sources=$sources; dll_sha256=(Get-FileHash -LiteralPath (Join-Path $root 'native/nsmb_interception.dll') -Algorithm SHA256).Hash.ToLower()}
[IO.File]::WriteAllText((Join-Path $root 'native/manifest.json'), ($manifest | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
$dataArgs = @()
Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object { $_.Extension -in '.py', '.json', '.cpp' } | ForEach-Object {
    $relative = $_.FullName.Substring($root.Length + 1)
    $directory = [IO.Path]::GetDirectoryName($relative)
    if (-not $directory) { $directory = '.' }
    $dataArgs += @('--add-data', "$($_.FullName);$directory")
}
& $Python -m PyInstaller --noconfirm --clean --onefile --console --name bigstar-rule-cpu --distpath $output --workpath (Join-Path $output 'work') --specpath $output --paths $root --collect-submodules nsmb_mvl_rule_versions @dataArgs --add-binary "$(Join-Path $root 'native/nsmb_interception.dll');native" (Join-Path $root 'nsmb_mvl_rule_watch_worker.py')
if ($LASTEXITCODE -ne 0) { throw 'CPU worker packaging failed' }
& (Join-Path $output 'bigstar-rule-cpu.exe') --check
if ($LASTEXITCODE -ne 0) { throw 'CPU worker self-check failed' }
