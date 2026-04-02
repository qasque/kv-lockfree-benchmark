@echo off
cd /d "%~dp0"
call "%~dp0prepend_mingw_runtime.cmd"
bench.exe %*
