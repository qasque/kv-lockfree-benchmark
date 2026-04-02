$ErrorActionPreference = "Continue"

$Root = Split-Path -Parent $PSScriptRoot
$MingwBin = $env:BENCH_MINGW_BIN
if (-not $MingwBin) {
    $try = Join-Path $env:USERPROFILE "Desktop\mingw32\bin"
    if (Test-Path $try) { $MingwBin = $try }
}
if (-not $MingwBin) {
    $try = Join-Path $env:USERPROFILE "Desktop\mingw64\bin"
    if (Test-Path $try) { $MingwBin = $try }
}

Set-Location $Root
if ($MingwBin -and (Test-Path $MingwBin)) {
    $env:PATH = "$MingwBin;$env:PATH"
}

$mcfgAdded = $false
$mcfgDirs = @()
if ($env:MINGW_HOME) { $mcfgDirs += (Join-Path $env:MINGW_HOME "bin") }
$mcfgDirs += @(
    (Join-Path $env:USERPROFILE "Desktop\mingw64\bin")
    (Join-Path $env:USERPROFILE "Desktop\mingw32\bin")
    "C:\msys64\mingw64\bin"
    "C:\msys64\ucrt64\bin"
)
foreach ($d in $mcfgDirs) {
    if (-not (Test-Path $d)) { continue }
    if (Test-Path (Join-Path $d "libmcfgthread-2.dll")) {
        $env:PATH = "$d;$env:PATH"
        Write-Host "[bench] MinGW runtime: $d"
        $mcfgAdded = $true
        break
    }
}
if (-not $mcfgAdded) {
    $gcc = Get-Command gcc -ErrorAction SilentlyContinue
    if ($gcc) {
        $gdir = Split-Path $gcc.Source -Parent
        if (Test-Path (Join-Path $gdir "libmcfgthread-2.dll")) {
            $env:PATH = "$gdir;$env:PATH"
            Write-Host "[bench] MinGW runtime: $gdir"
        }
    }
}

$env:PYTHONUNBUFFERED = "1"
$env:BENCH_TOTAL_MINUTES = "30"
$env:BENCH_VISIBLE = "1"

$runs = 5 * 4 * 7
$secEach = [math]::Max(1, [int](30 * 60 / $runs))
Write-Host ""
Write-Host "Project root: $Root"
Write-Host "Runs: $runs, ~$secEach s each (~30 min bench time). Stream output on. Ctrl+C to stop."
Write-Host ""

python (Join-Path $Root "scripts\run_all_benchmarks.py")
exit $LASTEXITCODE
