$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot

$MingwBin = $null
if ($env:MINGW_HOME) {
    $c = Join-Path $env:MINGW_HOME "bin"
    if (Test-Path $c) { $MingwBin = $c }
}
if (-not $MingwBin -and $env:BENCH_MINGW_BIN -and (Test-Path $env:BENCH_MINGW_BIN)) {
    $MingwBin = $env:BENCH_MINGW_BIN
}
if (-not $MingwBin) {
    $try = Join-Path $env:USERPROFILE "Desktop\mingw32\bin"
    if (Test-Path $try) { $MingwBin = $try }
}
if (-not $MingwBin) {
    $try = Join-Path $env:USERPROFILE "Desktop\mingw64\bin"
    if (Test-Path $try) { $MingwBin = $try }
}

$TotalMinutes = 30
$ImplCount = 5
$SecondsPerImpl = [int](($TotalMinutes * 60) / $ImplCount)
if ($SecondsPerImpl -lt 60) { $SecondsPerImpl = 60 }

Set-Location $Root
if ($MingwBin -and (Test-Path $MingwBin)) {
    $env:PATH = "$MingwBin;$env:PATH"
}

$env:PYTHONUNBUFFERED = "1"
$env:LONG_SECONDS = "$SecondsPerImpl"
$env:LONG_THREADS = [string][Environment]::ProcessorCount
$env:LONG_INTERVAL = "15"
$env:LONG_PROFILE = "mixed"
$env:LONG_BUCKETS = "1024"
$env:LONG_PRIORITY = "high"

$env:VALIDATE_THREADS = [string][Environment]::ProcessorCount
$env:VALIDATE_SECONDS = "3"
$env:VALIDATE_PROFILE = "mixed"

Write-Host "==============================================="
Write-Host " Validate + ~30 min long snapshot run"
Write-Host "==============================================="
Write-Host "CPU threads      :" $env:LONG_THREADS
Write-Host "Seconds per impl :" $env:LONG_SECONDS
Write-Host "Profile          :" $env:LONG_PROFILE
Write-Host "Priority         :" $env:LONG_PRIORITY
Write-Host ""

Write-Host "[1/3] validate_benchmarks.py"
python "$Root\scripts\validate_benchmarks.py"
if ($LASTEXITCODE -ne 0) { throw "validate_benchmarks.py failed" }

Write-Host ""
Write-Host "[2/3] run_long_snapshots.py"
python "$Root\scripts\run_long_snapshots.py"
if ($LASTEXITCODE -ne 0) { throw "run_long_snapshots.py failed" }

Write-Host ""
Write-Host "[3/3] make_snapshot_plots.py"
python "$Root\scripts\make_snapshot_plots.py"
if ($LASTEXITCODE -ne 0) { throw "make_snapshot_plots.py failed" }

Write-Host ""
Write-Host "Done. Updated output\snapshots_long_*.csv and comparison PNG."
Write-Host "Press Enter to close."
[void][System.Console]::ReadLine()
