@echo off
setlocal

set ROOT=%~dp0
cd /d "%ROOT%"
echo [INFO] Project: "%ROOT%"

set MINGW_BIN=%USERPROFILE%\Desktop\mingw32\bin
if exist "%MINGW_BIN%\gcc.exe" (
  where gcc >nul 2>nul
  if %errorlevel% neq 0 (
    echo [INFO] Prepending MinGW: "%MINGW_BIN%"
    set "PATH=%MINGW_BIN%;%PATH%"
  )
)

if not exist "bench.exe" (
  where gcc >nul 2>nul
  if %errorlevel%==0 (
    echo [INFO] Building bench.exe with gcc...
    gcc -O2 -std=c11 -Wall -Wextra -o bench.exe ^
      main.c bench.c kv_mutex.c kv_rwlock.c kv_skiplock.c kv_lf_hash.c kv_lf_skiplist.c -lpsapi
    if %errorlevel% neq 0 goto :fail
  ) else (
    echo [ERROR] gcc not found.
    goto :fail
  )
)

echo [INFO] run_all_benchmarks.py...
python scripts\run_all_benchmarks.py
if errorlevel 1 goto :fail

echo [INFO] make_plots.py...
python scripts\make_plots.py
if errorlevel 1 goto :fail

echo [OK] results.csv and output\*.png updated.
exit /b 0

:fail
echo [ERROR] Build or run failed.
exit /b 1
