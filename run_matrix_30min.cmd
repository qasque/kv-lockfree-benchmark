@echo off
chcp 65001 >nul
title Benchmark matrix ~30 min
cd /d "%~dp0"

call "%~dp0prepend_mingw_runtime.cmd"

set PYTHONUNBUFFERED=1
set BENCH_TOTAL_MINUTES=30
set BENCH_VISIBLE=1

echo.
echo  Full matrix 5 x 4 x 7 = 140 bench.exe runs
echo  Target ~30 min wall time. Ctrl+C may leave results.csv incomplete.
echo.

python scripts\run_all_benchmarks.py
set EC=%ERRORLEVEL%
echo.
if %EC% equ 130 echo Interrupted by user.
if not %EC% equ 0 echo Exit code: %EC%
pause
