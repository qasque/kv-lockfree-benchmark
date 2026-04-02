$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Log = Join-Path $Root "overnight_log.txt"

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

function Write-Log($msg) {
    $line = "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') $msg"
    Add-Content -Path $Log -Value $line -Encoding UTF8
    Write-Host $line
}

Set-Location $Root
if ($MingwBin -and (Test-Path $MingwBin)) {
    $env:PATH = "$MingwBin;$env:PATH"
}

$TotalSec = 7 * 3600
$ReserveSec = 30 * 60 + 10 * 60
$PerImpl = [int]([math]::Floor(($TotalSec - $ReserveSec) / 5))
if ($PerImpl -lt 300) { $PerImpl = 300 }
$env:LONG_SECONDS = "$PerImpl"

"--- overnight start $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ---" | Out-File -FilePath $Log -Encoding UTF8
Write-Log "LONG_SECONDS per impl = $PerImpl (~$([math]::Round($PerImpl/3600,2)) h each, 5 impls)"

try {
    Write-Log "run_all_benchmarks.py"
    & python (Join-Path $Root "scripts\run_all_benchmarks.py")
    if ($LASTEXITCODE -ne 0) { throw "run_all_benchmarks exit $LASTEXITCODE" }

    Write-Log "make_plots.py"
    & python (Join-Path $Root "scripts\make_plots.py")
    if ($LASTEXITCODE -ne 0) { throw "make_plots exit $LASTEXITCODE" }

    Write-Log "run_long_snapshots.py"
    & python (Join-Path $Root "scripts\run_long_snapshots.py")
    if ($LASTEXITCODE -ne 0) { throw "run_long_snapshots exit $LASTEXITCODE" }

    Write-Log "make_snapshot_plots.py"
    & python (Join-Path $Root "scripts\make_snapshot_plots.py")
    if ($LASTEXITCODE -ne 0) { throw "make_snapshot_plots exit $LASTEXITCODE" }

    Write-Log "shutdown /s /t 120"
    shutdown /s /t 120 /c "Pipeline finished. Shutdown in 2 min. Cancel: shutdown /a"
    Write-Log "done"
} catch {
    Write-Log "ERROR: $_"
    exit 1
}
