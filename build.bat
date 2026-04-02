@echo off
setlocal
set SRC=main.c bench.c kv_mutex.c kv_rwlock.c kv_skiplock.c kv_lf_hash.c kv_lf_skiplist.c kv_wrongget.c
where cl >nul 2>&1 && (
  cl /nologo /O2 /W3 /std:c17 /Fe:bench.exe %SRC% psapi.lib
  exit /b %ERRORLEVEL%
)
where gcc >nul 2>&1 && (
  gcc -O2 -std=c11 -Wall -Wextra -o bench.exe %SRC% -lpsapi
  if errorlevel 1 exit /b %ERRORLEVEL%
  echo If bench.exe fails with libmcfgthread-2.dll: call prepend_mingw_runtime.cmd or copy the DLL next to bench.exe.
  exit /b 0
)
echo No compiler: install Visual Studio Build Tools or MinGW-w64.
exit /b 1
